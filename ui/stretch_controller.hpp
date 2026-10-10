// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "project_controller.hpp"
#include <soundcurrent/stretch_render_protocol.hpp>
#include <QString>
namespace soundcurrent::daw::ui {
enum class StretchPhase {Idle,Queued,Preparing,Running,Verifying,Complete,Canceled,Fault,RecoveryRequired};
struct StretchSelection {
    ResourceLease lease;
    std::shared_ptr<const ControllerSnapshot> project;
    Id track,clip,operation=Id::generate();
    StretchSettings settings;
    std::optional<StretchContext> context;
    std::optional<WarpSettings> warp;
    std::optional<ClipStretchPlan> plan;
    StretchSelection(ResourceLease credit,std::shared_ptr<const ControllerSnapshot> source,Id t,Id c,StretchSettings value,std::optional<StretchContext> region={},const std::optional<WarpSettings> &markers={})
        :lease(std::move(credit)),project(std::move(source)),track(std::move(t)),clip(std::move(c)),settings(value),context(region),warp(markers) {}
};
struct StretchSnapshot {
    StretchPhase phase=StretchPhase::Idle;
    std::shared_ptr<const StretchSelection> selection;
    std::shared_ptr<const VerifiedClipStretch> result;
    std::size_t childPid=0;
    std::optional<int> childExit;
    std::optional<ErrorCode> error;
    bool busy=false,closed=false,canceled=false,timedOut=false,abnormalExit=false;
};
struct StretchOptions {
    ResourceLedger memory{512*1024*1024,"Background stretch"};
    QString program;
    StretchRenderPolicy policy;
    // Qualification hooks run on the control worker, never GUI/audio.
    std::function<void()> beforeSpawn,afterReady,afterChild;
};
// One admitted job, off-GUI process/IO/codec work. Completion does not mutate
// Session: the caller binds project epoch/root and submits the verified edit.
// Canceled/abnormal outcomes retain inspectable completed or incomplete jobs.
class StretchController {
 public:
    explicit StretchController(StretchOptions={});
    ~StretchController();
    Admission render(std::shared_ptr<const ControllerSnapshot>,Id track,Id clip,StretchSettings,std::optional<StretchContext> = {},const std::optional<WarpSettings> & = {});
    void requestCancel() noexcept;
    void requestShutdown() noexcept;
    bool clearResult();
    std::shared_ptr<const StretchSnapshot> snapshot() const;
 private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace soundcurrent::daw::ui
