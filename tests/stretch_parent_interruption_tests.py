#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Linux production-controller parent death; no GUI, installed or audio claim."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time


def wait_for(predicate, seconds=12):
    deadline = time.monotonic() + seconds
    while True:
        value = predicate()
        if value:
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError("Observation deadline expired; process state needs cleanup")
        time.sleep(.001)


def identity(pid):
    stat = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return {"pid": pid, "parentPid": int(stat[1]), "startTicks": int(stat[19]),
            "exe": os.readlink(f"/proc/{pid}/exe"), "state": stat[0]}


def inventory(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file()}


def terminal(pid):
    found, status = os.waitpid(pid, os.WNOHANG)
    return {"pid": found, "waitStatus": status, "exitCode": os.waitstatus_to_exitcode(status)} if found else None


def run_case(probe, helper, directory, stage):
    directory.mkdir()
    root = (directory / "project-été-Κиїв").resolve()
    subprocess.run([str(probe), "--init", str(root)], check=True, timeout=10)
    baseline = inventory(root)
    ticket = directory / "parent-ready.json"
    release = Path(str(ticket) + ".release")
    log = (directory / "parent.log").open("wb")
    parent = subprocess.Popen([str(probe), "--render", str(root), stage, str(ticket), str(helper)],
                              stdout=log, stderr=log)
    parent_fd = os.pidfd_open(parent.pid)
    helper_fd = None
    child = None
    child_terminal = None
    report = {"stage": stage, "parentIdentity": identity(parent.pid), "fixtureBefore": baseline}

    def scan(expected):
        before = inventory(root)
        result = subprocess.run([str(probe), "--scan", str(root)], capture_output=True,
                                text=True, timeout=10, check=True)
        response = json.loads(result.stdout)
        assert response["status"] == expected, response
        assert response["reviewed"] == (expected == "Ready") and not response["attached"]
        assert inventory(root) == before, "Production scan/review changed files"
        assert response["projectSha256"] == baseline["project.json"]
        return response

    def kill_parent():
        assert parent.poll() is None
        signal.pidfd_send_signal(parent_fd, signal.SIGKILL)
        assert parent.wait(timeout=5) == -signal.SIGKILL
        report["parentExitCode"] = parent.returncode
        assert not Path(f"/proc/{parent.pid}").exists()

    try:
        def boundary():
            if ticket.exists():
                return json.loads(ticket.read_bytes())
            if parent.poll() is not None:
                raise RuntimeError(f"Parent exited before boundary: {parent.returncode}")
            return None
        observed = wait_for(boundary)
        assert observed["pid"] == parent.pid and not observed["attached"]
        child = observed["helperPid"]
        if stage != "after-complete":
            helper_fd = os.pidfd_open(child)
            report["helperIdentity"] = identity(child)
            assert report["helperIdentity"]["exe"] == str(helper)
        else:
            report["helperPidReportedByReapingParent"] = child
            assert not Path(f"/proc/{child}").exists(), "afterChild helper was not reaped"
        job = root / "media" / "derived" / observed["operation"]
        report["operation"] = observed["operation"]
        if stage == "after-complete":
            # afterChild runs after QProcess reaps the helper. This PID may already
            # be absent; use the actual completed receipt, not a fabricated exit.
            assert observed["point"] == "after-child-before-verification"
            assert (job / "complete.json").exists() and (job / "audio.wav").exists()
            kill_parent()
            report["terminalRecovery"] = scan("Ready")
        elif stage == "before-ack":
            assert observed["point"] == "before-start-acknowledgement"
            assert not (job / "start.request").exists() and not (job / "complete.json").exists()
            assert report["helperIdentity"]["parentPid"] == parent.pid
            kill_parent()
            report["liveRecovery"] = scan("Active")
            child_terminal = wait_for(lambda: terminal(child))
            assert child_terminal["exitCode"] == 2, child_terminal  # actual hard watchdog
            report["terminalRecovery"] = scan("Incomplete")
            assert not (job / "audio.wav").exists() and not (job / "complete.json").exists()
        else:
            assert observed["point"] == "before-start-acknowledgement"
            assert report["helperIdentity"]["parentPid"] == parent.pid
            # Freeze only this owned helper to observe the ack boundary, then
            # resume and freeze once actual partial audio is being written.
            signal.pidfd_send_signal(helper_fd, signal.SIGSTOP)
            wait_for(lambda: identity(child)["state"] == "T")
            release.write_text("release\n")
            wait_for(lambda: (job / "start.request").exists())
            signal.pidfd_send_signal(helper_fd, signal.SIGCONT)
            wait_for(lambda: (job / "audio.partial").exists() and (job / "audio.partial").stat().st_size > 4096)
            signal.pidfd_send_signal(helper_fd, signal.SIGSTOP)
            wait_for(lambda: identity(child)["state"] == "T")
            assert not (job / "complete.json").exists(), "Missed during-processing boundary"
            report["partialBytesAtParentDeath"] = (job / "audio.partial").stat().st_size
            kill_parent()
            report["liveRecovery"] = scan("Active")
            # Parent death alone does not contain this Linux helper. Allow its
            # bounded work to finish, retain its actual terminal status and review
            # the committed result irrespective of broken stdout after parent loss.
            if stage == "during-processing-helper-death":
                partial_hash = hashlib.sha256((job / "audio.partial").read_bytes()).hexdigest()
                signal.pidfd_send_signal(helper_fd, signal.SIGKILL)
                child_terminal = wait_for(lambda: terminal(child))
                assert child_terminal["exitCode"] == -signal.SIGKILL
                report["terminalRecovery"] = scan("Incomplete")
                assert hashlib.sha256((job / "audio.partial").read_bytes()).hexdigest() == partial_hash
                assert not (job / "complete.json").exists() and not (job / "audio.wav").exists()
                report["partialPreservedSha256"] = partial_hash
            else:
                signal.pidfd_send_signal(helper_fd, signal.SIGCONT)
                child_terminal = wait_for(lambda: terminal(child))
                report["terminalRecovery"] = scan("Ready")
        if child_terminal:
            report["helperTerminal"] = child_terminal
        report["fixtureAfter"] = inventory(root)
        assert all(report["fixtureAfter"].get(n) == digest for n, digest in baseline.items())
        report.update({"savedProjectAndRawUnchanged": True, "installedGuiQualified": False,
                       "windowsQualified": False, "powerLossQualified": False})
        return report
    finally:
        # All signals target pidfds acquired for these owned process instances.
        # Deadlines never mean a process has exited; reap and close explicitly.
        if parent.poll() is None:
            signal.pidfd_send_signal(parent_fd, signal.SIGKILL)
            parent.wait(timeout=5)
        if helper_fd is not None:
            if child_terminal is None:
                try:
                    signal.pidfd_send_signal(helper_fd, signal.SIGCONT)
                    signal.pidfd_send_signal(helper_fd, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                try:
                    wait_for(lambda: terminal(child), 5)
                except ChildProcessError:  # already reaped by the live parent
                    pass
            os.close(helper_fd)
        os.close(parent_fd)
        log.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path)
    parser.add_argument("helper", type=Path)
    parser.add_argument("--retain", type=Path)
    args = parser.parse_args()
    libc = ctypes.CDLL(None, use_errno=True)
    assert libc.prctl(36, 1, 0, 0, 0) == 0, "Cannot adopt/reap owned orphan helper"
    root = args.retain or Path(tempfile.mkdtemp(prefix="sc-parent-interruption-"))
    if args.retain:
        root.mkdir(parents=True, exist_ok=False)
    try:
        report = {"platform": "Linux", "qualification": "production-controller-subprocess",
                  "probeSha256": hashlib.sha256(args.probe.read_bytes()).hexdigest(),
                  "helperSha256": hashlib.sha256(args.helper.read_bytes()).hexdigest(), "cases": []}
        for stage in ("before-ack", "during-processing", "during-processing-helper-death", "after-complete"):
            report["cases"].append(run_case(args.probe.resolve(), args.helper.resolve(), root / stage, stage))
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps({"passed": True, "cases": [{"stage": c["stage"], "status": c["terminalRecovery"]["status"],
                         "helperExit": c.get("helperTerminal", {}).get("exitCode")} for c in report["cases"]]}))
    except Exception:
        print(f"Failed fixtures retained at {root}")
        raise
    else:
        if not args.retain:
            shutil.rmtree(root)


if __name__ == "__main__":
    main()
