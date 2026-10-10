# SPDX-License-Identifier: GPL-3.0-only
"""Bounded observation of a disposable owned worker in acceptance tests.

Read both pipes concurrently: waiting for stdout alone can hide a refusal or
block forever. This is test infrastructure, outside the real-time engine.
"""
import json
import queue
import subprocess
import threading
import time


def observe(command, request, on_stdout_line=None, *, timeout=15,
            maximum_output_bytes=4 * 1024 * 1024):
    if timeout <= 0 or maximum_output_bytes <= 0:
        raise ValueError("Worker observation limits must be positive")
    started = time.monotonic()
    child = subprocess.Popen(command, stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    events = queue.Queue(maxsize=32)
    stop = threading.Event()
    chunks = {"stdout": [], "stderr": []}

    def publish(event):
        while not stop.is_set():
            try:
                events.put(event, timeout=.05)
                return
            except queue.Full:
                pass

    def reader(name, stream):
        try:
            while not stop.is_set():
                block = stream.read1(4096)
                if not block:
                    break
                publish((name, block))
        finally:
            publish((name, None))

    threads = [threading.Thread(target=reader, args=(name, getattr(child, name)),
                                daemon=True) for name in chunks]
    for thread in threads:
        thread.start()
    input_error = None
    def writer():
        nonlocal input_error
        try:
            child.stdin.write(request.encode("utf-8"))
            child.stdin.close()
        except (BrokenPipeError, OSError) as error:
            input_error = str(error)

    input_thread = threading.Thread(target=writer, daemon=True)
    input_thread.start()
    timed_out = output_limited = False
    retirement_pending = False
    closed = set()
    pending = b""
    total = 0
    try:
        # The write is observed under the same deadline. A worker that never
        # reads stdin must not hang its parent on a full request pipe.
        while len(closed) != 2 or child.poll() is None:
            remaining = timeout - (time.monotonic() - started)
            if remaining <= 0:
                timed_out = True
                break
            try:
                name, block = events.get(timeout=min(.05, remaining))
            except queue.Empty:
                continue
            if block is None:
                closed.add(name)
                continue
            available = maximum_output_bytes - total
            chunks[name].append(block[:available])
            total += min(len(block), available)
            if len(block) > available:
                output_limited = True
                break
            if name == "stdout" and on_stdout_line:
                pending += block
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    on_stdout_line(line.decode("utf-8"))
        if pending and on_stdout_line and not (timed_out or output_limited):
            on_stdout_line(pending.decode("utf-8"))
    finally:
        if child.poll() is None:
            child.kill()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            retirement_pending = True
        stop.set()
        input_thread.join(timeout=1)
        for thread in threads:
            thread.join(timeout=1)
        # Closing a BufferedReader from another thread takes its read lock.
        # A descendant holding an inherited pipe can keep that lock forever,
        # even after the direct worker exited. Leave such daemon readers to
        # finish at EOF rather than defeating our observation deadline.
        if not input_thread.is_alive():
            try: child.stdin.close()
            except OSError: pass
        for thread, name in zip(threads, chunks):
            if not thread.is_alive():
                getattr(child, name).close()
    result = subprocess.CompletedProcess(command, child.returncode,
        b"".join(chunks["stdout"]).decode("utf-8", "replace"),
        b"".join(chunks["stderr"]).decode("utf-8", "replace"))
    result.pid = child.pid
    result.elapsed_seconds = time.monotonic() - started
    result.observation_timed_out = timed_out
    result.output_limited = output_limited
    result.input_error = input_error
    result.retirement_pending = retirement_pending
    return result


def diagnostic(result):
    """Retain real exit and protocol packets even when stderr is empty."""
    return json.dumps({"pid": result.pid, "exitCode": result.returncode,
        "exitCodeHex": (f"{result.returncode & 0xffffffff:08x}"
                        if result.returncode is not None else None),
        "seconds": result.elapsed_seconds,
        "observationTimedOut": result.observation_timed_out,
        "outputLimited": result.output_limited,
        "inputError": result.input_error,
        "retirementPending": result.retirement_pending,
        "stdout": result.stdout, "stderr": result.stderr}, ensure_ascii=True)
