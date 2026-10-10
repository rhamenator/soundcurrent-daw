"""Bounded inspection of evidence members without extracting them."""

import io
from pathlib import PurePosixPath
import zipfile


def check_member(name, data, depth=0, budget=None):
    # Preserve original bytes/names; normalize only for refusal decisions.
    normalized = name.replace("\\", "/").casefold()
    parts = PurePosixPath(normalized).parts
    assert parts and not normalized.startswith("/")
    assert all(p not in ("..", ".") and ":" not in p for p in parts), name
    windows_name = "/".join(p.rstrip(" .") for p in parts)
    assert not windows_name.endswith((".exe", ".dll", ".iso")), name
    assert "private-transport" not in windows_name and "credentials" not in windows_name, name
    assert not data.startswith(b"MZ"), name
    assert depth <= 3 and len(data) <= 32 * 1024 * 1024, name
    if budget is None:
        budget = [128 * 1024 * 1024]
    budget[0] -= len(data)
    assert budget[0] >= 0, "Evidence byte budget exceeded"
    # A renamed archive must not conceal forbidden members either.
    if windows_name.endswith(".zip") or zipfile.is_zipfile(io.BytesIO(data)):
        with zipfile.ZipFile(io.BytesIO(data)) as archive:
            members = archive.infolist()
            assert len(members) <= 512
            assert len({m.filename for m in members}) == len(members)
            for member in members:
                # Refuse declared oversized members before decompression.
                assert member.file_size <= 32 * 1024 * 1024
                assert member.file_size <= budget[0]
                check_member(member.filename, archive.read(member), depth + 1, budget)
