"""Offline contract checks; does not import Unreal, launch an editor, or edit assets."""
import argparse
import copy
import json
from pathlib import Path
import tempfile
import unittest

import IntegrateTripoBatch as pipeline
import RunBatchArtGallery as gallery

LANCER = "2ef5bf56-708e-436b-ac9a-1086e61faa41"
SUMMONER = "39a83098-f50e-4c5f-a955-a5decdd99d4a"
PALADIN = "eed717ba-2996-4179-85a3-2069f87a7ef3"


class Contracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "Art").mkdir()
        self.write("Art/TripoHumanoidBatch.json", {"assets": [{"role": "Lancer", "id": LANCER}, {"role": "Paladin (Holy / Righteous shared body)", "id": PALADIN}]})
        self.write("Art/TripoCreatureBatch.json", {"assets": []})
        self.write("Art/TripoManifest.json", {"assets": [{"name": "Rift Summoner", "asset_id": SUMMONER}]})
        self.binding = {"schemaVersion": 1, "batch": "Batch01", "bindings": [
            {"uuid": LANCER, "importFolder": "/Game/TripoModels/verified_lancer", "mesh": "/Game/TripoModels/verified_lancer/body", "profileIds": ["lancer"], "attackPose": "Lancer", "heightCm": 182},
            {"uuid": SUMMONER, "importFolder": "/Game/TripoModels/verified_summoner", "mesh": "/Game/TripoModels/verified_summoner/body", "profileIds": ["summoner"], "attackPose": "Scholar", "heightCm": 176}]}
        self.write("Art/TripoImportBindings.json", self.binding)
        for name in ("verified_lancer", "verified_summoner"):
            path = self.root / f"Content/TripoModels/{name}/body.uasset"
            path.parent.mkdir(parents=True); path.write_bytes(b"fixture package presence only")

    def write(self, relative, data):
        (self.root / relative).write_text(json.dumps(data), encoding="utf-8")

    def args(self, **changes):
        values = dict(project=self.root, bindings=None, batch=None, profile=[], all_humanoids=False, build=False, verify=False)
        values.update(changes)
        return argparse.Namespace(**values)

    def snapshots(self):
        return {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob("*") if p.is_file()}

    def test_preflight_is_read_only_and_never_claims_engine_validation(self):
        before = self.snapshots(); result = pipeline.run(self.args())
        self.assertEqual(result["status"], "files_present_engine_validation_pending")
        self.assertEqual([r["key"] for r in result["selected"]], ["lancer", "summoner"])
        self.assertFalse(result["assetMutation"])
        self.assertEqual(before, self.snapshots())

    def test_missing_imports_are_aggregated(self):
        for path in (self.root / "Content").rglob("*.uasset"):
            path.unlink()
        result = pipeline.run(self.args())
        self.assertEqual(result["status"], "failed")
        self.assertTrue(any("lancer:" in e for e in result["errors"]))
        self.assertTrue(any("summoner:" in e for e in result["errors"]))

    def test_observed_hyphenated_bridge_package_is_preserved(self):
        folder = "/Game/TripoModels/dragon-armored_knight_3d_model"
        mesh = folder + "/dragon-armored_knight_3d_model"
        self.binding["bindings"][0].update(importFolder=folder, mesh=mesh)
        self.write("Art/TripoImportBindings.json", self.binding)
        path = pipeline.file_for(self.root / "Content", mesh)
        path.parent.mkdir(parents=True); path.write_bytes(b"hyphenated fixture package")
        before = self.snapshots(); result = pipeline.run(self.args())
        self.assertEqual(result["status"], "files_present_engine_validation_pending")
        self.assertEqual(result["selected"][0]["mesh"], mesh)
        self.assertEqual(gallery.canonical_asset(mesh), mesh + ".dragon-armored_knight_3d_model")
        self.assertEqual(before, self.snapshots())

    def test_missing_binding_does_not_guess_an_import_folder(self):
        self.binding["bindings"] = self.binding["bindings"][:1]
        self.write("Art/TripoImportBindings.json", self.binding)
        result = pipeline.run(self.args())
        self.assertTrue(any("Missing explicit" in e and "summoner" in e for e in result["errors"]))

    def test_long_paths_remain_hashed_and_protected(self):
        content = self.root / "Content"
        path = content / ("long_model_" * 9) / ("texture_" * 20 + ".uasset")
        self.assertGreater(len(str(path)), 260)
        pipeline.io_path(path.parent).mkdir(parents=True)
        pipeline.io_path(path).write_bytes(b"protected long package")
        self.addCleanup(pipeline.io_path(path).unlink)
        hashes = pipeline.original_hashes(content, "Art/Characters/TripoBatch/Batch01")
        self.assertIn(str(path.relative_to(content)), hashes)
        pipeline.validate_hashes(content, hashes)
        pipeline.io_path(path).write_bytes(b"changed")
        with self.assertRaisesRegex(RuntimeError, "Protected preexisting asset changed"):
            pipeline.validate_hashes(content, hashes)

    def test_rejects_wrong_profile_uuid_association(self):
        self.binding["bindings"][0]["profileIds"] = ["summoner"]
        self.write("Art/TripoImportBindings.json", self.binding)
        self.assertEqual(pipeline.run(self.args())["status"], "failed")

    def test_rejects_duplicate_uuid_folder_and_unsafe_paths(self):
        mutations = [dict(uuid=LANCER), dict(importFolder="/Game/TripoModels/VERIFIED_LANCER"),
                     dict(importFolder="/Game/TripoModels/../Outside"), dict(mesh="/Game/Other/body")]
        for change in mutations:
            binding = copy.deepcopy(self.binding); binding["bindings"][1].update(change)
            self.write("Art/TripoImportBindings.json", binding)
            with self.subTest(change=change):
                self.assertEqual(pipeline.run(self.args())["status"], "failed")

    def test_protected_original_import_cannot_be_rebound(self):
        self.binding["bindings"][0].update(importFolder="/Game/TripoModels/Medieval_Knight_Armor_3D_Model", mesh=None)
        self.write("Art/TripoImportBindings.json", self.binding)
        self.assertEqual(pipeline.run(self.args())["status"], "failed")

    def test_all_humanoids_reports_missing_shared_profiles(self):
        result = pipeline.run(self.args(all_humanoids=True))
        self.assertTrue(any("paladin_holy" in e for e in result["errors"]))
        self.assertTrue(any("paladin_righteous" in e for e in result["errors"]))

    def test_existing_output_is_never_overwritten(self):
        output = self.root / "Content/Art/Characters/TripoBatch/Batch01"
        output.mkdir(parents=True); (output / "Existing.uasset").write_bytes(b"existing")
        before = self.snapshots(); result = pipeline.run(self.args(build=True))
        self.assertEqual(result["status"], "failed")
        self.assertEqual(before, self.snapshots())

    def test_build_without_unreal_never_writes_ready_bindings(self):
        before = self.snapshots(); result = pipeline.run(self.args(build=True))
        self.assertEqual(result["status"], "failed")
        self.assertEqual(before, self.snapshots())
        self.assertFalse((self.root / "Content/Data/ChampionArtBindings.json").exists())

    def test_ready_publication_retains_other_profiles_and_is_atomic(self):
        (self.root / "Content/Data").mkdir()
        self.write("Content/Data/ChampionArtBindings.json", {"schemaVersion": 1, "bindings": [{"profileId": "knight", "mesh": "original", "status": "ready"}]})
        rows = [{"profileId": "lancer", "mesh": "new", "status": "ready"}]
        pipeline.publish(self.root, rows, {"batch": "Batch01"})
        result = pipeline.read_json(self.root / "Content/Data/ChampionArtBindings.json")
        self.assertEqual(result["bindings"][0]["mesh"], "original")
        self.assertEqual(result["bindings"][1], rows[0])
        self.assertTrue((self.root / "Saved/TripoBatchIntegration/Batch01_bindings_before.json").is_file())

    def test_publication_retries_preserve_the_first_backup(self):
        (self.root / "Content/Data").mkdir()
        self.write("Content/Data/ChampionArtBindings.json", {"schemaVersion": 1, "bindings": []})
        rows = [{"profileId": "summoner", "mesh": "new", "status": "ready"}]
        pipeline.publish(self.root, rows, {"batch": "Batch01"})
        before = self.snapshots()
        pipeline.publish(self.root, rows, {"batch": "Batch01"})
        self.assertEqual(before, self.snapshots())


if __name__ == "__main__":
    unittest.main()
