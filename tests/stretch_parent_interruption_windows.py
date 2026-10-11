#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Windows production-controller interruption; owned handles, no GUI/audio claim."""
import argparse
import ctypes
from ctypes import wintypes as W
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

K = ctypes.WinDLL("kernel32", use_last_error=True)
HANDLE = W.HANDLE
DWORD = W.DWORD
INVALID = ctypes.c_void_p(-1).value
PARENT_EXIT, HELPER_EXIT = 0x53430001, 0x53430002


class ThreadEntry(ctypes.Structure):
    _fields_ = [("dwSize", DWORD), ("cntUsage", DWORD), ("tid", DWORD), ("pid", DWORD),
                ("base", W.LONG), ("delta", W.LONG), ("flags", DWORD)]


class ProcessEntry(ctypes.Structure):
    _fields_ = [("dwSize", DWORD), ("cntUsage", DWORD), ("pid", DWORD), ("heap", ctypes.c_size_t),
                ("module", DWORD), ("threads", DWORD), ("parent", DWORD), ("base", W.LONG),
                ("flags", DWORD), ("exe", W.WCHAR * 260)]


def api(name, result, *args):
    f = getattr(K, name)
    f.restype, f.argtypes = result, args
    return f


OpenProcess = api("OpenProcess", HANDLE, DWORD, W.BOOL, DWORD)
CloseHandle = api("CloseHandle", W.BOOL, HANDLE)
QueryImage = api("QueryFullProcessImageNameW", W.BOOL, HANDLE, DWORD, W.LPWSTR, ctypes.POINTER(DWORD))
GetTimes = api("GetProcessTimes", W.BOOL, HANDLE, *([ctypes.POINTER(W.FILETIME)] * 4))
Wait = api("WaitForSingleObject", DWORD, HANDLE, DWORD)
GetExit = api("GetExitCodeProcess", W.BOOL, HANDLE, ctypes.POINTER(DWORD))
Terminate = api("TerminateProcess", W.BOOL, HANDLE, W.UINT)
Snapshot = api("CreateToolhelp32Snapshot", HANDLE, DWORD, DWORD)
ThreadFirst = api("Thread32First", W.BOOL, HANDLE, ctypes.POINTER(ThreadEntry))
ThreadNext = api("Thread32Next", W.BOOL, HANDLE, ctypes.POINTER(ThreadEntry))
ProcessFirst = api("Process32FirstW", W.BOOL, HANDLE, ctypes.POINTER(ProcessEntry))
ProcessNext = api("Process32NextW", W.BOOL, HANDLE, ctypes.POINTER(ProcessEntry))
OpenThread = api("OpenThread", HANDLE, DWORD, W.BOOL, DWORD)
ThreadOwner = api("GetProcessIdOfThread", DWORD, HANDLE)
Suspend = api("SuspendThread", DWORD, HANDLE)
Resume = api("ResumeThread", DWORD, HANDLE)
SetPriority = api("SetPriorityClass", W.BOOL, HANDLE, DWORD)
CurrentProcess = api("GetCurrentProcess", HANDLE)


def checked(value):
    if not value:
        raise ctypes.WinError(ctypes.get_last_error())
    return value


def wait_for(predicate, seconds=12):
    deadline = time.monotonic() + seconds
    while True:
        result = predicate()
        if result:
            return result
        if time.monotonic() >= deadline:
            raise RuntimeError("Observation deadline expired; handle still needs inspection/cleanup")
        time.sleep(.001)


def inventory(root, active=False):
    # Windows writer.lock is intentionally opened with no sharing. Its complete
    # file hash is captured after actual termination, never guessed while active.
    # A suspended user thread is not a kernel I/O completion barrier. The owned
    # writer's audio.partial can still change until its process handle signals.
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob("*") if p.is_file() and not (active and p.name in ("writer.lock", "audio.partial"))}


def parent_pid(pid):
    snapshot = Snapshot(2, 0)
    assert snapshot not in (None, INVALID)
    entry = ProcessEntry()
    entry.dwSize = ctypes.sizeof(entry)
    try:
        good = ProcessFirst(snapshot, ctypes.byref(entry))
        while good:
            if entry.pid == pid:
                return entry.parent
            entry.dwSize = ctypes.sizeof(entry)
            good = ProcessNext(snapshot, ctypes.byref(entry))
        raise RuntimeError("Owned helper missing from process snapshot")
    finally:
        checked(CloseHandle(snapshot))


class Process:
    def __init__(self, pid, expected):
        self.pid = pid
        self.handle = checked(OpenProcess(0x100000 | 0x1000 | 1, False, pid))
        try:
            buffer = ctypes.create_unicode_buffer(32768)
            length = DWORD(len(buffer))
            checked(QueryImage(self.handle, 0, buffer, ctypes.byref(length)))
            times = [W.FILETIME() for _ in range(4)]
            checked(GetTimes(self.handle, *(ctypes.byref(t) for t in times)))
            self.identity = {"pid": pid, "exe": buffer.value,
                             "creationFileTime": (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime,
                             "sha256": hashlib.sha256(expected.read_bytes()).hexdigest()}
            assert Path(buffer.value).resolve() == expected.resolve(), "Wrong process image"
        except Exception:
            checked(CloseHandle(self.handle))
            self.handle = None
            raise

    def running(self):
        result = Wait(self.handle, 0)
        assert result in (0, 258), result
        return result == 258

    def terminal(self):
        if self.running():
            return None
        code = DWORD()
        checked(GetExit(self.handle, ctypes.byref(code)))
        return {"pid": self.pid, "creationFileTime": self.identity["creationFileTime"], "exitCode": code.value}

    def kill(self, code):
        checked(Terminate(self.handle, code))
        result = wait_for(self.terminal, 5)
        assert result["exitCode"] == code
        return result

    def close(self):
        if self.handle:
            checked(CloseHandle(self.handle))
            self.handle = None


class FrozenThreads:
    """Debugger-style pause of this test helper only; never a product lock primitive."""
    def __init__(self, process):
        self.process, self.handles = process, []
        snapshot = Snapshot(4, 0)
        assert snapshot not in (None, INVALID)
        entry = ThreadEntry()
        entry.dwSize = ctypes.sizeof(entry)
        try:
            good = ThreadFirst(snapshot, ctypes.byref(entry))
            while good:
                if entry.pid == process.pid:
                    handle = checked(OpenThread(2 | 0x40, False, entry.tid))
                    try:
                        assert ThreadOwner(handle) == process.pid
                        previous = Suspend(handle)
                        assert previous != 0xffffffff, ctypes.get_last_error()
                        self.handles.append((handle, previous, entry.tid))
                    except Exception:
                        checked(CloseHandle(handle))
                        raise
                entry.dwSize = ctypes.sizeof(entry)
                good = ThreadNext(snapshot, ctypes.byref(entry))
            assert self.handles and process.running()
        except Exception:
            self.resume()
            raise
        finally:
            checked(CloseHandle(snapshot))

    def resume(self):
        while self.handles:
            handle, previous, tid = self.handles.pop()
            try:
                if self.process.running():
                    assert Resume(handle) == previous + 1, ("Resume count changed", tid)
            finally:
                checked(CloseHandle(handle))


def run_case(probe, helper, directory, stage):
    directory.mkdir()
    root = (directory / "project-été-Κиїв").resolve()
    subprocess.run([str(probe), "--init", str(root)], check=True, timeout=10, creationflags=0x08004000)
    baseline = inventory(root)
    ticket = (directory / "parent-ready.json").resolve()
    release = Path(str(ticket) + ".release")
    parent_log = (directory / "parent.log").open("wb")
    launcher = subprocess.Popen([str(probe), "--render", str(root), stage, str(ticket), str(helper)],
                                stdout=parent_log, stderr=parent_log, creationflags=0x08004000)
    parent, child, frozen = None, None, None
    report = {"stage": stage, "fixtureBefore": baseline}

    def scan(expected):
        before = inventory(root, active=expected == "Active")
        run = subprocess.run([str(probe), "--scan", str(root)], capture_output=True, text=True,
                             encoding="utf-8", timeout=10, check=True, creationflags=0x08004000)
        response = json.loads(run.stdout)
        assert response["status"] == expected, response
        assert response["reviewed"] == (expected == "Ready") and not response["attached"]
        assert response["projectSha256"] == baseline["project.json"] and inventory(root, active=expected == "Active") == before
        if expected == "Active":
            response["exclusiveLockHashDeferredUntilTerminal"] = True
            response["writerPartialHashDeferredUntilTerminal"] = True
        return response

    try:
        parent = Process(launcher.pid, probe)
        report["parentIdentity"] = parent.identity
        report["boundaryReadRefusals"] = 0
        def boundary():
            if ticket.exists():
                try:
                    return json.loads(ticket.read_bytes())
                except PermissionError:
                    # A visible Windows publication may still refuse an open.
                    # Keep the same owned instance and original bounded deadline.
                    report["boundaryReadRefusals"] += 1
            if not parent.running():
                raise RuntimeError(f"Parent exited before boundary: {parent.terminal()}")
            return None
        observed = wait_for(boundary)
        assert observed["pid"] == parent.pid and not observed["attached"]
        report["operation"] = observed["operation"]
        job = root / "media" / "derived" / observed["operation"]
        if stage == "after-complete":
            assert observed["point"] == "after-child-before-verification"
            assert (job / "complete.json").exists() and (job / "audio.wav").exists()
            report["helperPidReportedByReapingParent"] = observed["helperPid"]
            report["parentTerminal"] = parent.kill(PARENT_EXIT)
            report["terminalRecovery"] = scan("Ready")
        else:
            assert observed["point"] == "before-start-acknowledgement"
            child = Process(observed["helperPid"], helper)
            report["helperIdentity"] = {**child.identity, "parentPid": parent_pid(child.pid)}
            assert report["helperIdentity"]["parentPid"] == parent.pid
            if stage == "before-ack":
                assert not (job / "start.request").exists() and not (job / "complete.json").exists()
                report["parentTerminal"] = parent.kill(PARENT_EXIT)
                report["liveRecovery"] = scan("Active")
                report["helperTerminal"] = wait_for(child.terminal)
                assert report["helperTerminal"]["exitCode"] == 2
                report["terminalRecovery"] = scan("Incomplete")
                assert not (job / "audio.wav").exists() and not (job / "complete.json").exists()
            else:
                frozen = FrozenThreads(child)
                report["ackFrozenThreadIds"] = [tid for _, _, tid in frozen.handles]
                release.write_text("release\n")
                wait_for(lambda: (job / "start.request").exists())
                frozen.resume()
                frozen = None
                wait_for(lambda: (job / "audio.partial").exists() and (job / "audio.partial").stat().st_size > 4096)
                frozen = FrozenThreads(child)
                report["partialFrozenThreadIds"] = [tid for _, _, tid in frozen.handles]
                assert not (job / "complete.json").exists(), "Missed partial boundary"
                report["partialBytesAtParentDeath"] = (job / "audio.partial").stat().st_size
                report["parentTerminal"] = parent.kill(PARENT_EXIT)
                report["liveRecovery"] = scan("Active")
                if stage == "during-processing-helper-death":
                    before_terminal = (job / "audio.partial").read_bytes()
                    report["partialSnapshotBeforeTermination"] = {"bytes": len(before_terminal),
                        "sha256": hashlib.sha256(before_terminal).hexdigest(), "stableWriterBaseline": False}
                    report["helperTerminal"] = child.kill(HELPER_EXIT)
                    frozen.resume()
                    frozen = None
                    # Process.kill waits for the exact owned process handle.
                    # Pending writes may finish/cancel before that terminal
                    # barrier, so establish the read-only recovery baseline now.
                    terminal_partial = (job / "audio.partial").read_bytes()
                    digest = hashlib.sha256(terminal_partial).hexdigest()
                    report["terminalPartialBaseline"] = {"bytes": len(terminal_partial), "sha256": digest}
                    report["partialChangedBeforeTerminalBarrier"] = digest != report["partialSnapshotBeforeTermination"]["sha256"]
                    report["terminalRecovery"] = scan("Incomplete")
                    assert hashlib.sha256((job / "audio.partial").read_bytes()).hexdigest() == digest
                    report["partialPreservedSha256"] = digest
                    assert not (job / "complete.json").exists() and not (job / "audio.wav").exists()
                else:
                    frozen.resume()
                    frozen = None
                    report["helperTerminal"] = wait_for(child.terminal)
                    report["terminalRecovery"] = scan("Ready")
        assert launcher.wait(timeout=5) == PARENT_EXIT
        report["fixtureAfter"] = inventory(root)
        assert all(report["fixtureAfter"].get(n) == h for n, h in baseline.items())
        report.update({"savedProjectAndRawUnchanged": True, "installedGuiQualified": False,
                       "audioEndpointActivated": False, "powerLossQualified": False})
        return report
    finally:
        if frozen:
            frozen.resume()
        cleanup = []
        for process, code in [(parent, PARENT_EXIT), (child, HELPER_EXIT)]:
            if process:
                if process.running():
                    process.kill(code)
                actual_terminal = process.terminal()
                assert actual_terminal
                cleanup.append(actual_terminal)
                process.close()
        if launcher.poll() is None:
            launcher.kill()  # Popen owns the exact instance even if identity query failed.
        launcher.wait(timeout=5)
        (directory / "cleanup-terminal.json").write_text(json.dumps(cleanup, indent=2) + "\n", encoding="utf-8")
        parent_log.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("probe", type=Path)
    parser.add_argument("helper", type=Path)
    parser.add_argument("--retain", type=Path)
    args = parser.parse_args()
    checked(SetPriority(CurrentProcess(), 0x4000))
    root = args.retain or Path(tempfile.mkdtemp(prefix="sc-parent-interruption-"))
    if args.retain:
        root.mkdir(parents=True, exist_ok=False)
    try:
        report = {"utc": datetime.now(timezone.utc).isoformat(), "platform": "Windows",
                  "qualification": "production-controller-subprocess",
                  "probeSha256": hashlib.sha256(args.probe.read_bytes()).hexdigest(),
                  "helperSha256": hashlib.sha256(args.helper.read_bytes()).hexdigest(), "cases": []}
        for stage in ("before-ack", "during-processing", "during-processing-helper-death", "after-complete"):
            case = run_case(args.probe.resolve(), args.helper.resolve(), root / stage, stage)
            (root / stage / "case-report.json").write_text(json.dumps(case, indent=2) + "\n", encoding="utf-8")
            report["cases"].append(case)
        (root / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
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
