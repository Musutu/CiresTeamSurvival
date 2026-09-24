"""Focused schema, merge and fail-before-write checks; no Unreal launch."""
import copy
import json
from pathlib import Path
import tempfile
import unittest
from ImportAstraAbilities import install, validate


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.bundle = json.loads((Path(__file__).resolve().parent.parent / "Content/Data/AstraAbilities.json").read_text(encoding="utf-8"))

    def test_all_native_shapes_and_cast_fields(self):
        validate(self.bundle)
        self.assertEqual({a["area"]["shape"] for a in self.bundle["abilities"]}, {"circle", "cone", "line", "square", "custom"})

    def test_invalid_geometry_rejected(self):
        for key, bad in (("tickInterval", 0), ("verticalTolerance", 1000), ("damagePerSecond", float("nan"))):
            bundle = copy.deepcopy(self.bundle)
            bundle["abilities"][0]["area"][key] = bad
            with self.assertRaises(ValueError):
                validate(bundle)
        area = self.bundle["abilities"][0]["area"]
        area["shape"] = "custom"
        area["customPolygon"] = [[0, 0], [100, 100], [0, 100], [100, 0]]
        with self.assertRaises(ValueError):
            validate(self.bundle)

    def test_merge_preserves_unrelated_and_backs_up_previous(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, destination = Path(tmp)/"input.json", Path(tmp)/"runtime.json"
            prior = copy.deepcopy(self.bundle)
            prior["abilities"] = [prior["abilities"][0]]
            prior["abilities"][0]["id"] = "other_authored_ability"
            destination.write_text(json.dumps(prior), encoding="utf-8")
            source.write_text(json.dumps(self.bundle), encoding="utf-8")
            self.assertEqual(install(source, destination, check=True)["status"], "validated")
            self.assertEqual(json.loads(destination.read_text()), prior)
            self.assertEqual(install(source, destination)["abilities"], 6)
            self.assertEqual(json.loads(destination.read_text())["abilities"][0], prior["abilities"][0])
            self.assertEqual(len(list(Path(tmp).glob("*.backup-*.json"))), 1)

    def test_invalid_input_leaves_destination_unchanged(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, destination = Path(tmp)/"input.json", Path(tmp)/"runtime.json"
            destination.write_text(json.dumps(self.bundle), encoding="utf-8")
            before = destination.read_bytes()
            self.bundle["abilities"][0]["manaCost"] = -1
            source.write_text(json.dumps(self.bundle), encoding="utf-8")
            with self.assertRaises(ValueError):
                install(source, destination)
            self.assertEqual(destination.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
