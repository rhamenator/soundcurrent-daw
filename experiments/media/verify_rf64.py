#!/usr/bin/env python3
"""Independent reader for this probe's exact mono RF64 float fixture, not a general decoder."""
from pathlib import Path
import json
import struct
import sys


def verify(path: Path):
    data = path.read_bytes()
    assert data[:4] == b"RF64" and data[8:12] == b"WAVE"
    offset, header, audio = 12, {}, None
    while offset + 8 <= len(data):
        tag = data[offset : offset + 4]
        size = struct.unpack_from("<I", data, offset + 4)[0]
        offset += 8
        if tag == b"ds64":
            riff_bytes, audio_bytes, frames = struct.unpack_from("<QQQ", data, offset)
            header.update(riff_bytes=riff_bytes, audio_bytes=audio_bytes, frames=frames)
        if tag == b"fmt ":
            fmt = data[offset : offset + size]
            kind, channels, rate, byte_rate, alignment, bits = struct.unpack_from("<HHIIHH", fmt)
            assert kind == 0xFFFE and len(fmt) >= 40
            assert fmt[24:40] == bytes.fromhex("0300000000001000800000aa00389b71")
            assert alignment == 4 and byte_rate == 48000 * 4
            header.update(format_tag=kind, subtype="IEEE_FLOAT", channels=channels, rate=rate, bits=bits)
        if tag == b"data":
            assert size == 0xFFFFFFFF
            audio = data[offset : offset + header["audio_bytes"]]
            break
        offset += size + size % 2
    assert header["riff_bytes"] == len(data) - 8
    assert header["frames"] == 1536 and header["audio_bytes"] == 1536 * 4
    assert header["channels"] == 1 and header["rate"] == 48000 and header["bits"] == 32
    values = struct.unpack("<1536f", audio)
    expected = tuple(struct.unpack("<f", struct.pack("<f", ((i % 101) - 50) * .04))[0] for i in range(1536))
    assert values == expected and max(abs(n) for n in values) > 1
    return header


if __name__ == "__main__":
    print(json.dumps(verify(Path(sys.argv[1])), indent=2))
