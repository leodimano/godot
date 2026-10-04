"""Run with: python -m unittest discover -s modules/gdscript/tests -p test_gdscript_bytecode_builders.py"""

import unittest

from modules.gdscript.gdscript_bytecode_builders import source_fingerprint


class TestBytecodeFingerprint(unittest.TestCase):
    def test_file_order_is_canonical(self):
        records = [("vm.cpp", b"instructions\n"), ("variant.h", b"types\n")]
        self.assertEqual(source_fingerprint(records), source_fingerprint(list(reversed(records))))

    def test_line_endings_are_canonical(self):
        self.assertEqual(source_fingerprint([("vm.cpp", b"a\r\nb\r\n")]), source_fingerprint([("vm.cpp", b"a\nb\n")]))

    def test_content_and_paths_affect_identity(self):
        original = source_fingerprint([("vm.cpp", b"a\n")])
        self.assertNotEqual(original, source_fingerprint([("vm.cpp", b"b\n")]))
        self.assertNotEqual(original, source_fingerprint([("variant.cpp", b"a\n")]))

    def test_record_boundaries_are_unambiguous(self):
        self.assertNotEqual(source_fingerprint([("ab", b"c")]), source_fingerprint([("a", b"bc")]))
        self.assertNotEqual(source_fingerprint([]), source_fingerprint([("", b"")]))


if __name__ == "__main__":
    unittest.main()
