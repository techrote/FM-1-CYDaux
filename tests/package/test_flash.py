"""Guard the boundary between a downloaded package and a serial flash operation."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("cydaux_flash", ROOT / "scripts" / "flash.py")
flash = importlib.util.module_from_spec(spec)
spec.loader.exec_module(flash)


class PackageIntegrity(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.manifest = {"schema": 1, "board": "ESP32-2432S028R", "chip": "esp32", "files": {}}
        for name, offset in flash.PARTS:
            data = bytes(range(64))
            (self.directory / name).write_bytes(data)
            self.manifest["files"][name] = {"offset": offset, "size": len(data),
                "sha256": hashlib.sha256(data).hexdigest()}
        self.save()

    def save(self):
        (self.directory / "manifest.json").write_text(json.dumps(self.manifest))

    def tearDown(self):
        self.temp.cleanup()

    def test_qualified_parts(self):
        self.assertEqual(flash.verify_package(self.directory)["chip"], "esp32")

    def test_corrupt_image_is_rejected(self):
        (self.directory / "firmware.bin").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "validation failed"):
            flash.verify_package(self.directory)

    def test_other_chip_is_rejected(self):
        self.manifest["chip"] = "esp32s3"
        self.save()
        with self.assertRaisesRegex(ValueError, "original ESP32"):
            flash.verify_package(self.directory)

    def test_changed_offset_is_rejected(self):
        self.manifest["files"]["firmware.bin"]["offset"] = "0x9000"
        self.save()
        with self.assertRaisesRegex(ValueError, "validation failed"):
            flash.verify_package(self.directory)

    def test_overlap_is_rejected_even_with_valid_hash(self):
        data = bytes(0x7001)
        (self.directory / "bootloader.bin").write_bytes(data)
        self.manifest["files"]["bootloader.bin"].update(size=len(data), sha256=hashlib.sha256(data).hexdigest())
        self.save()
        with self.assertRaisesRegex(ValueError, "overlaps"):
            flash.verify_package(self.directory)

    def test_missing_binary_is_rejected(self):
        (self.directory / "partitions.bin").unlink()
        with self.assertRaises(OSError):
            flash.verify_package(self.directory)


if __name__ == "__main__":
    unittest.main()
