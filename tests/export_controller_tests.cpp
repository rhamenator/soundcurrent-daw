// SPDX-License-Identifier: GPL-3.0-only
#include "export_controller.hpp"
#include "export_fixture.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>
using namespace export_fixture;
using namespace soundcurrent::daw::ui;
namespace {
ExportRequest request(const std::filesystem::path &root, const Session &s,
                      const std::filesystem::path &dest, std::uint64_t revision = 4) {
    ExportSpec spec(s.tracks.front().id);
    spec.startFrame = s.exportStartFrame;
    spec.endFrame = s.exportEndFrame;
    ExportRequest r(spec);
    r.root = root;
    r.destination = dest;
    r.session = std::make_shared<const Session>(s);
    r.modelRevision = revision;
    return r;
}
void done(ExportController &c) {
    await([&] { return !c.snapshot()->busy; });
}
void workflows(const std::filesystem::path &dir) {
    auto root = dir / "project";
    auto s = project(root);
    const auto projectBefore = bytes(root / "project.json");
    const auto rawBefore = hashMediaFile(root / utf8Path(s.assets.front().relativePath));
    Gate gate;
    bool wrongThread = false;
    auto *gui = QThread::currentThread();
    ExportControllerOptions o;
    o.beforeRender = [&](const ExportRequest &) {
        wrongThread = QThread::currentThread() == gui;
        gate.wait();
    };
    ExportController c(o);
    Release release{gate};
    auto dest = dir / "mix-Δ.wav";
    check(c.submit(request(root, s, dest)) == Admission::Accepted, "Export not admitted");
    await([&] { return gate.entered.load(); });
    check(c.submit(request(root, s, dir / "lost.wav")) == Admission::Full,
          "Single-flight export overwritten");
    check(!c.confirm(c.snapshot()->job, true), "Confirmation outside inspection accepted");
    const auto fixed = *c.snapshot();
    s.tracks.front().eq.bands.front().gainDb = 12;
    gate.released = true;
    done(c);
    auto view = c.snapshot();
    check(view->phase == ExportPhase::Complete && view->result &&
              view->modelRevision == fixed.modelRevision && !wrongThread,
          "Export completion/thread ownership incorrect");
    auto original = ProjectStore(root).load();
    auto expectedSpec = request(root, original, dir / "expected.wav").spec;
    auto expected = exportTrackWav(root, original, dir / "expected.wav", expectedSpec);
    check(view->result->fileSha256 == expected.fileSha256 && view->result->overFullScaleSamples > 0,
          "Immutable export or float headroom lost");
    check(!std::filesystem::exists(dir / "lost.wav") && noTemporary(dir),
          "Lost job or temporary file published");
    // Consent is bound to the inspected job/content, not supplied by the GUI.
    const auto prior = bytes(dest);
    auto bad = request(root, s, dest);
    bad.spec.replaceSha256 = hashMediaFile(dest);
    check(c.submit(std::move(bad)) == Admission::Accepted, "Invalid consent test not admitted");
    done(c);
    check(c.snapshot()->phase == ExportPhase::Fault && bytes(dest) == prior,
          "Caller-supplied replacement bypassed consent");
    check(c.submit(request(root, s, dest)) == Admission::Accepted,
          "Overwrite inspection not admitted");
    await([&] { return c.snapshot()->phase == ExportPhase::AwaitingConfirmation; });
    const auto inspection = c.snapshot();
    check(inspection->replacement && inspection->replacement->bytes == prior.size() &&
              inspection->replacement->sha256 == hashMediaFile(dest),
          "Destination inspected incorrectly");
    check(!c.confirm(inspection->job + 1, true) && c.confirm(inspection->job, false) &&
              !c.confirm(inspection->job, true),
          "Stale or duplicate consent accepted");
    done(c);
    check(c.snapshot()->phase == ExportPhase::Canceled && bytes(dest) == prior,
          "No did not preserve existing export");
    check(c.submit(request(root, s, dest)) == Admission::Accepted,
          "Changed destination not admitted");
    await([&] { return c.snapshot()->phase == ExportPhase::AwaitingConfirmation; });
    write(dest, "independent edit after preview");
    check(c.confirm(c.snapshot()->job, true), "Yes not accepted");
    done(c);
    check(c.snapshot()->phase == ExportPhase::Fault &&
              bytes(dest) == "independent edit after preview" && noTemporary(dir),
          "Changed target overwritten");
    check(c.submit(request(root, s, dest)) == Admission::Accepted,
          "Confirmed replacement not admitted");
    await([&] { return c.snapshot()->phase == ExportPhase::AwaitingConfirmation; });
    check(c.confirm(c.snapshot()->job, true), "Replacement confirmation rejected");
    done(c);
    check(c.snapshot()->phase == ExportPhase::Complete && c.snapshot()->result->replaced &&
              bytes(dest) != "independent edit after preview",
          "Confirmed file not replaced");
    check(projectBefore == bytes(root / "project.json") &&
              rawBefore == hashMediaFile(root / utf8Path(s.assets.front().relativePath)),
          "Export changed project/raw media");
    c.requestShutdown();
    await([&] { return c.snapshot()->closed; });
    check(c.submit(request(root, s, dest)) == Admission::Closing, "Closed export accepted a job");
}
void cancellation(const std::filesystem::path &dir) {
    const auto root = dir / "cancel-project";
    auto s = project(root);
    Gate gate;
    ExportControllerOptions o;
    o.beforeRender = [&](const ExportRequest &) { gate.wait(); };
    ExportController c(o);
    Release release{gate};
    const auto dest = dir / "cancel.wav";
    check(c.submit(request(root, s, dest)) == Admission::Accepted, "Blocked export rejected");
    await([&] { return gate.entered.load(); });
    c.requestCancel();
    check(c.snapshot()->busy && c.snapshot()->cancelRequested && !c.snapshot()->closed,
          "Cancel falsely acknowledged worker completion");
    c.requestShutdown();
    check(c.submit(request(root, s, dest)) == Admission::Closing, "Shutdown admitted new export");
    check(!c.snapshot()->closed, "Blocked export closed prematurely");
    gate.released = true;
    await([&] { return c.snapshot()->closed; });
    check(!std::filesystem::exists(dest) && noTemporary(dir), "Shutdown left export/temporary");
    // Actual partial writer canceled before publication; publication success wins late cancel.
    bool cancel = false;
    ExportControllerOptions partial;
    partial.render.canceled = [&] { return cancel; };
    partial.render.boundary = [&](ExportBoundary b, Frame) {
        if (b == ExportBoundary::BeforePublish)
            cancel = true;
    };
    ExportController p(partial);
    check(p.submit(request(root, s, dest)) == Admission::Accepted, "Partial job rejected");
    done(p);
    check(p.snapshot()->phase == ExportPhase::Canceled && !std::filesystem::exists(dest) &&
              noTemporary(dir),
          "Canceled WAV published or leaked partial");
    ExportController *owner = nullptr;
    ExportControllerOptions published;
    published.render.boundary = [&](ExportBoundary b, Frame) {
        if (b == ExportBoundary::DirectoryFlush) {
            owner->requestCancel();
            throw std::runtime_error("Injected directory flush warning");
        }
    };
    ExportController success(published);
    owner = &success;
    check(success.submit(request(root, s, dest)) == Admission::Accepted,
          "Publication job rejected");
    done(success);
    check(success.snapshot()->phase == ExportPhase::Complete && success.snapshot()->result &&
              !success.snapshot()->result->publicationWarning.empty() &&
              std::filesystem::exists(dest),
          "Published complete file misreported as canceled");
}
void inspection(const std::filesystem::path &dir) {
    const auto root = dir / "inspect-project";
    auto s = project(root);
    auto invalid = [&](auto f) {
        try {
            f();
        } catch (const ProjectError &) {
            ++checks;
            return;
        }
        throw std::runtime_error("Unsafe destination inspected");
    };
    invalid([&] { inspectExportDestination(root, root / "project.json"); });
    invalid([&] {
        inspectExportDestination(root, std::filesystem::path(std::string("bad\0name", 8)));
    });
    invalid([&] { inspectExportDestination(root, dir / "missing" / "mix.wav"); });
    const auto p = dir / "inspected.wav";
    write(p, std::string(180000, 'x'));
    int reads = 0;
    invalid([&] {
        inspectExportDestination(root, p, [&] {
            if (++reads == 3)
                throw ProjectError(ErrorCode::Canceled, "Canceled hash");
        });
    });
    check(reads == 3 && bytes(p).size() == 180000, "Destination hash cancellation failed");
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    try {
        check(temp.isValid(), "Temporary directory unavailable");
        auto dir = utf8Path(temp.path().toUtf8().toStdString());
        workflows(dir);
        cancellation(dir);
        inspection(dir);
        std::cout << checks << " export controller/inspection checks passed.\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
