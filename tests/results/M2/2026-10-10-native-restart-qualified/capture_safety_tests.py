"""Adversarial evidence exclusions; no real credentials or product binaries."""

import io
import unittest
import zipfile

from capture_safety import check_member


def nested(name, data):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(name, data)
    return buffer.getvalue()


class CaptureSafetyTests(unittest.TestCase):
    def test_windows_case_variants_are_refused(self):
        for name in ("Credentials.json", "records\\private-Transport.json",
                     "soundcurrent-daw.EXE", "QT6CORE.DlL", "test.ISO",
                     "records\\Credentials.JSON", "app.EXE. "):
            with self.subTest(name=name), self.assertRaises(AssertionError):
                check_member("owned.zip", nested(name, b"synthetic refusal fixture"))

    def test_renamed_binary_signature_is_refused_in_nested_capture(self):
        with self.assertRaises(AssertionError):
            check_member("owned.zip", nested("records/report.json", b"MZsynthetic fixture"))

    def test_second_level_archive_cannot_hide_forbidden_material(self):
        with self.assertRaises(AssertionError):
            check_member("owned.zip", nested("records/INNER.ZIP", nested("private-Transport.json", b"fixture")))

    def test_windows_path_escape_is_refused(self):
        for name in ("..\\outside.json", "C:\\outside.json", "\\outside.json"):
            with self.subTest(name=name), self.assertRaises(AssertionError):
                check_member("owned.zip", nested(name, b"fixture"))

    def test_declared_oversize_refused_without_reading_member(self):
        with self.assertRaises(AssertionError):
            check_member("owned.zip", nested("report.json", b"x" * (33 * 1024 * 1024)))

    def test_unicode_owned_records_pass_without_extraction(self):
        check_member("owned.zip", nested("project-été-Κиїв\\records\\report.json", b'{"owned":true}'))


if __name__ == "__main__":
    unittest.main()
