"""Test first-floor selection, report compatibility, and dependency isolation."""

import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from types import SimpleNamespace
from unittest import mock

sys.modules.setdefault("unreal", types.ModuleType("unreal"))
from create_first_floor_level import (
    FLOOR_LAYER, clear_import_templates, read_build_report, select_actors, write_report,
)
from verify_first_floor_level import MAP, SOURCE, find_dependencies


def actor(label, layer="", kind="StaticMeshActor", low=0, high=100, editor_only=False):
    """Return a fake actor descriptor with the supplied layer, type, and height bounds."""
    path = SimpleNamespace(export_text=lambda: layer)
    return SimpleNamespace(
        label=label,
        native_class=SimpleNamespace(get_name=lambda: kind),
        data_layer_assets=[path] if layer else [],
        actor_is_editor_only=editor_only,
        bounds=SimpleNamespace(min=SimpleNamespace(z=low), max=SimpleNamespace(z=high)),
    )


class FirstFloorSelectionTests(unittest.TestCase):
    """Check which source actors are safe to include in the first-floor map."""
    def setUp(self):
        """Create a first-floor room descriptor shared by the selection tests."""
        self.room = actor("Room1100", FLOOR_LAYER, "PFTAreaTriggers", 0, 400)

    def test_upper_floors_roof_and_mep_are_excluded(self):
        """Reject actors in each upper-floor, roof, or mechanical-services layer."""
        excluded = [
            actor(name, f"/Game/DataLayers/{name}.{name}")
            for name in ("Floor2", "Floor3", "Floor4", "Roof", "MEP")
        ]
        selected, _ = select_actors([self.room] + excluded)
        self.assertEqual([self.room], selected)

    def test_unfiled_and_unlayered_geometry_use_first_floor_height(self):
        """Keep unfiled ground-floor geometry and reject unlayered upper-floor geometry."""
        ground = actor("GroundWall", "/Game/DataLayers/ToBeFiled.ToBeFiled", low=0, high=350)
        upper = actor("UpperWall", low=900, high=1200)
        selected, _ = select_actors([self.room, ground, upper])
        self.assertEqual([self.room, ground], selected)

    def test_shared_lighting_and_room_volumes_are_kept(self):
        """Retain shared lighting and first-floor rooms without copying upper-floor rooms."""
        light = actor("SkyLight", "/Game/DataLayers/LSUCampusWide.LSUCampusWide", "SkyLight", 5000, 5000)
        room = actor("HallwayC1010", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers")
        upper_room = actor(
            "Room3325", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers", 1000, 1400,
        )
        selected, _ = select_actors([self.room, light, room, upper_room])
        self.assertEqual([self.room, light, room], selected)

    def test_editor_only_actors_are_excluded(self):
        """Reject editor helpers even when they belong to the first-floor layer."""
        selected, _ = select_actors([self.room, actor("EditorHelper", FLOOR_LAYER, editor_only=True)])
        self.assertEqual([self.room], selected)

    def test_missing_room_volumes_fail(self):
        """Reject a map without room volumes instead of inventing a floor height."""
        with self.assertRaises(RuntimeError):
            select_actors([actor("Floor", FLOOR_LAYER)])

    def test_missing_first_floor_layer_fails(self):
        """Reject room volumes that have no accompanying first-floor layer content."""
        with self.assertRaises(RuntimeError):
            select_actors([actor("Room1100", kind="PFTAreaTriggers")])


class MetadataData:
    """Model Datasmith metadata and source import templates without loading Unreal."""

    def __init__(self):
        """Create empty metadata and template dictionaries for a user-data object."""
        self.metadata = {}
        self.templates = {}

    def get_editor_property(self, name):
        """Return the named stored property, matching Unreal's read interface."""
        return getattr(self, name)


class FirstFloorCompatibilityTests(unittest.TestCase):
    """Check report migration and references retained by the refactored tools."""

    def test_legacy_report_preserves_room_ids(self):
        """Read old field names without changing nested room IDs or completion state."""
        original = {"output": MAP, "source_room_ids": {"Room1120": "room-guid"}, "complete": True}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "build.json"
            path.write_text(json.dumps(original), encoding="utf-8")
            report = read_build_report(path)
        self.assertEqual(report["Output"], MAP)
        self.assertEqual(report["SourceRoomIds"], original["source_room_ids"])
        self.assertTrue(report["Complete"])
        self.assertEqual(report["Name"], "FirstFloorBuild")

    def test_pascal_case_report_round_trip(self):
        """Save and reload current report fields without renaming or losing data."""
        original = {"Name": "FirstFloorBuild", "Output": MAP, "SourceRoomIds": {"Room1120": "id"}}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "build.json"
            write_report(path, original)
            self.assertEqual(read_build_report(path), original)

    def test_metadata_cleanup_preserves_values_and_other_user_data(self):
        """Remove import links from copies while retaining source metadata and unrelated data."""
        source = MetadataData()
        source.metadata["RoomNumber"] = "1120"
        source.templates["AttachParent"] = SOURCE
        other_data = object()
        values = {"asset_user_data": [source, other_data], "asset_user_data_editor_only": []}
        component = SimpleNamespace(
            get_editor_property=values.__getitem__, set_editor_property=values.__setitem__,
        )
        source_actor = SimpleNamespace(get_components_by_class=lambda component_type: [component])
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, DatasmithUserData=MetadataData,
            ActorComponent=object, new_object=lambda object_class, outer: object_class(),
        ):
            self.assertEqual(clear_import_templates(source_actor), 1)
        cleaned = values["asset_user_data"][0]
        self.assertIsNot(cleaned, source)
        self.assertEqual(cleaned.metadata, source.metadata)
        self.assertEqual(cleaned.templates, {})
        self.assertEqual(source.templates, {"AttachParent": SOURCE})
        self.assertIs(values["asset_user_data"][1], other_data)

    def test_dependency_cycle_still_reveals_full_twin(self):
        """Terminate a cyclic graph walk and retain the path that reaches the full map."""
        shared = "/Game/Shared"
        graph = {MAP: [shared], shared: [MAP, SOURCE], SOURCE: []}
        registry = SimpleNamespace(get_dependencies=lambda package, options: graph[package])
        with mock.patch.object(
            sys.modules["unreal"], "AssetRegistryDependencyOptions", create=True,
            return_value=object(),
        ):
            visited, paths = find_dependencies(registry)
        self.assertEqual(visited, {MAP, shared, SOURCE})
        self.assertEqual(paths[SOURCE], [MAP, shared, SOURCE])


if __name__ == "__main__":
    unittest.main()
