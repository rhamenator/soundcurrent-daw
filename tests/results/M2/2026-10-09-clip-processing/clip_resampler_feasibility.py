# SPDX-License-Identifier: GPL-3.0-only
# Original bounded offline feasibility harness. No production dependency chosen.
# ABI reviewed against https://raw.githubusercontent.com/libsndfile/libsamplerate/0.2.2/include/samplerate.h
# API: https://libsndfile.github.io/libsamplerate/api_full.html
import ctypes as c
import math
import hashlib
import json
import time
from pathlib import Path

library = Path('/usr/lib/x86_64-linux-gnu/libsamplerate.so.0').resolve()
lib = c.CDLL(str(library))
class Data(c.Structure):
    _fields_ = [('input', c.POINTER(c.c_float)), ('output', c.POINTER(c.c_float)),
                ('input_frames', c.c_long), ('output_frames', c.c_long),
                ('consumed', c.c_long), ('produced', c.c_long),
                ('final', c.c_int), ('ratio', c.c_double)]
lib.src_get_version.restype = c.c_char_p
lib.src_simple.argtypes = [c.POINTER(Data), c.c_int, c.c_int]
lib.src_new.argtypes = [c.c_int, c.c_int, c.POINTER(c.c_int)]
lib.src_new.restype = c.c_void_p
lib.src_process.argtypes = [c.c_void_p, c.POINTER(Data)]
lib.src_delete.argtypes = [c.c_void_p]
lib.src_delete.restype = c.c_void_p

ratio = 44100 / 48000
n = 48000
start = time.monotonic()
checks = 0

def check(ok, why):
    global checks
    checks += 1
    if not ok: raise RuntimeError(why)

def pointer(buf, frame=0):
    return c.cast(c.byref(buf, frame * c.sizeof(c.c_float)), c.POINTER(c.c_float))

def convert(values, chunk=None, output_block=256):
    src = (c.c_float * len(values))(*values)
    if chunk is None:
        dest = (c.c_float * (math.ceil(len(values) * ratio) + 4096))()
        d = Data(pointer(src), pointer(dest), len(values), len(dest), 0, 0, 1, ratio)
        check(lib.src_simple(c.byref(d), 0, 1) == 0, 'Whole-buffer conversion failed')
        check(d.consumed == len(values), 'Whole-buffer input not fully consumed')
        return list(dest[:d.produced]), 1
    error = c.c_int()
    state = lib.src_new(0, 1, c.byref(error))
    check(bool(state) and error.value == 0, 'Streaming preparation failed')
    out = []
    position = 0
    calls = 0
    dest = (c.c_float * output_block)()
    try:
        for _ in range(20000):
            count = min(chunk, len(values) - position)
            d = Data(pointer(src, position), pointer(dest), count, output_block,
                     0, 0, int(position + count == len(values)), ratio)
            check(lib.src_process(state, c.byref(d)) == 0, 'Streaming process failed')
            check(0 <= d.consumed <= count and 0 <= d.produced <= output_block,
                  'Streaming counts exceed admitted spans')
            position += d.consumed
            out.extend(dest[:d.produced])
            calls += 1
            if position == len(values) and d.produced == 0:
                break
            check(d.consumed or d.produced or position == len(values), 'Streaming made no progress')
        else:
            raise RuntimeError('Streaming bounded call envelope exceeded')
        check(position == len(values), 'Streaming did not consume source extent')
    finally:
        lib.src_delete(state)
    return out, calls

def amplitude(signal, frequency):
    # 0.8 seconds / integer tone cycles; exclude finite-source filter edges.
    section = signal[4410:39690]
    check(len(section) == 35280, 'Interior analysis extent missing')
    a = math.fsum(x * math.sin(2 * math.pi * frequency * i / 44100)
                  for i, x in enumerate(section))
    b = math.fsum(x * math.cos(2 * math.pi * frequency * i / 44100)
                  for i, x in enumerate(section))
    return 2 * math.hypot(a, b) / len(section)

results = []
for frequency in (1000, 10000, 19000, 23000):
    values = [0.5 * math.sin(2 * math.pi * frequency * i / 48000) for i in range(n)]
    output, _ = convert(values)
    check(len(output) == 44100, 'Integer-rate duration differs from requested one second')
    check(all(math.isfinite(x) for x in output), 'Nonfinite resampled output')
    analysis_frequency = frequency if frequency < 22050 else 44100 - frequency
    gain = amplitude(output, analysis_frequency) / 0.5
    db = 20 * math.log10(max(gain, 1e-300))
    row = {'inputToneHz': frequency, 'analysisToneHz': analysis_frequency,
           'generatedFrames': len(output), 'interiorGainDb': db}
    if frequency == 1000:
        comparisons = []
        for chunk, block in ((37, 64), (211, 256), (4096, 511)):
            streamed, calls = convert(values, chunk, block)
            check(len(streamed) == len(output), 'Streaming duration differs from whole input')
            maximum = max(abs(a - b) for a, b in zip(output, streamed))
            comparisons.append({'inputChunk': chunk, 'outputBlock': block,
                                'calls': calls, 'maximumFloatDifference': maximum})
        row['partitionComparisons'] = comparisons
    results.append(row)
headroom, _ = convert([2.5] * n)
interior = headroom[4410:39690]
check(min(interior) > 2.49 and max(interior) < 2.51, 'Floating-point headroom was clamped')
report = {'experiment': 'candidate-libsamplerate-offline-feasibility-only',
          'libraryPath': str(library), 'librarySha256': hashlib.sha256(library.read_bytes()).hexdigest(),
          'runtimeVersion': lib.src_get_version().decode(), 'packageVersion': '0.2.2-4build2',
          'scriptSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
          'mode': 'SRC_SINC_BEST_QUALITY (0)', 'inputRate': 48000, 'outputRate': 44100,
          'checks': checks, 'elapsedSeconds': time.monotonic() - start, 'tones': results,
          'headroomInteriorMin': min(interior), 'headroomInteriorMax': max(interior),
          'scope': {'productionDependencyAdopted': False, 'qualityParity': False,
                    'fullQResampleGate': False, 'realTimeQualification': False,
                    'windowsQualification': False, 'audioDevice': False, 'vm': False,
                    'systemInstall': False},
          'remaining': ['exact source/build/transitive pin and notices', 'all declared ratios/rates/channel layouts',
                        'delay/tail alignment and seeks', 'alias/ripple sweep beyond four tones',
                        'allocation/deadline characterization', 'native Windows same-mode execution',
                        'shared clip timing/reader integration', 'independent pitch/stretch processor evaluation']}
Path('.cache/clip-resampler-feasibility.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
