#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Author the corpus's deterministic, original PCM; never opens audio devices."""
import pathlib
import struct
import sys
import wave


def generate(root):
    # Refuse an existing media directory; regeneration belongs in a fresh lab.
    root.mkdir(parents=True, exist_ok=False)
    for channels, name in ((1, "mono.wav"), (2, "stereo.wav")):
        with wave.open(str(root / name), "wb") as writer:
            writer.setparams((channels, 2, 48000, 96000, "NONE", "not compressed"))
            for first in range(0, 96000, 1024):
                block = bytearray()
                for frame in range(first, min(first + 1024, 96000)):
                    for channel in range(channels):
                        sample = ((frame * 73 + channel * 193) % 4093) - 2046
                        block.extend(struct.pack("<h", sample))
                writer.writeframesraw(block)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: generate_media.py NEW_MEDIA_DIRECTORY")
    generate(pathlib.Path(sys.argv[1]))
