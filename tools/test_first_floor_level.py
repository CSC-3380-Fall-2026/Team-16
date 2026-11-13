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
    CAMPUS_ROAD_OVERLAY, FLOOR_LAYER, MAIN_HALLWAY_FLOOR, REQUIRED_MATERIAL_ASSETS, clear_import_templates,
    disable_fixture_shadows, limit_fixture_draw_distance, load_selected_actors, missing_material_assets,
    map_report_path, read_build_report, read_map_build_report, select_actors, valid_output_map, write_report,
)
from first_floor_materials import MESHES, SHARED, WORLD_GRID, restore_surface_materials
from verify_first_floor_level import MAP, SOURCE, find_dependencies


def actor(label, layer="", kind="StaticMeshActor", low=0, high=100, editor_only=False, x=0):
    """Return a fake actor descriptor with the supplied layer, type, and height bounds."""
    path = SimpleNamespace(export_text=lambda: layer)
    return SimpleNamespace(
        label=label,
        native_class=SimpleNamespace(get_name=lambda: kind),
        data_layer_assets=[path] if layer else [],
        actor_is_editor_only=editor_only,
        bounds=SimpleNamespace(
            min=SimpleNamespace(x=x, y=0, z=low), max=SimpleNamespace(x=x + 100, y=100, z=high),
        ),
    )


class FirstFloorSelectionTests(unittest.TestCase):
    """Check which source actors are safe to include in the first-floor map."""
    def setUp(self):
        """Create a first-floor room descriptor shared by the selection tests."""
        self.room = actor("Room1100", FLOOR_LAYER, "PFTAreaTriggers", 0, 400)

    def test_building_structure_is_retained_across_floors(self):
        """Keep upper architecture and roof meshes while excluding hidden services and upper desks."""
        upper_wall = actor("Walls_CurtainWall", "/Game/DataLayers/Floor2.Floor2", low=500, high=800)
        roof = actor("Roof", "/Game/DataLayers/Roof.Roof", low=1500, high=1600)
        desk = actor("Furniture_Desk", "/Game/DataLayers/Floor2.Floor2", low=500, high=600)
        duct = actor("Duct", "/Game/DataLayers/MEP.MEP")
        selected, _ = select_actors([self.room, upper_wall, roof, desk, duct])
        self.assertEqual([self.room, upper_wall, roof], selected)

    def test_unfiled_structure_and_nearby_scenery_are_retained(self):
        """Keep shared geometry and entrance scenery without copying distant campus meshes."""
        tall_wall = actor("TallWall", "/Game/DataLayers/ToBeFiled.ToBeFiled", low=0, high=1200)
        entrance = actor("EntranceSlab", low=-150, high=-60)
        tree = actor("Tree", "/Game/DataLayers/Landscapes.Landscapes", x=500)
        distant_building = actor("CampusBuilding", "/Game/DataLayers/OtherLSUBuildings.OtherLSUBuildings", x=5000)
        selected, _ = select_actors([self.room, tall_wall, entrance, tree, distant_building])
        self.assertEqual([self.room, tall_wall, entrance, tree], selected)

    def test_shared_lighting_and_room_volumes_are_kept(self):
        """Retain shared lighting and first-floor rooms without copying upper-floor rooms."""
        light = actor("SkyLight", "/Game/DataLayers/LSUCampusWide.LSUCampusWide", "SkyLight", 5000, 5000)
        room = actor("HallwayC1010", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers")
        upper_room = actor(
            "Room3325", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers", 1000, 1400,
        )
        selected, _ = select_actors([self.room, light, room, upper_room])
        self.assertEqual([self.room, light, room], selected)

    def test_main_hallway_floor_group_is_kept(self):
        """Keep the original hallway floor even though its meshes are grouped on an Actor."""
        floor = actor(MAIN_HALLWAY_FLOOR, kind="Actor", low=10, high=10)
        selected, _ = select_actors([self.room, floor])
        self.assertEqual([self.room, floor], selected)

    def test_campus_road_overlay_is_excluded(self):
        """Keep the entrance sidewalk while dropping the elevated campus-wide road mesh."""
        road = actor(CAMPUS_ROAD_OVERLAY, "/Game/DataLayers/LSUCampusWide.LSUCampusWide", low=1177, high=1202)
        sidewalk = actor("SM_Sidewalk_03", "/Game/DataLayers/Landscapes.Landscapes", low=-15, high=0)
        selected, _ = select_actors([self.room, road, sidewalk])
        self.assertEqual([self.room, sidewalk], selected)

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

    def test_actor_loader_enables_selected_layers(self):
        """Enable the selected actors' layers before asking World Partition to load them."""
        self.room.guid = "room-guid"
        self.room.actor_path = "/Game/Source.Source:PersistentLevel.Room1100"
        floor = mock.Mock()
        floor.get_asset.return_value.get_path_name.return_value = FLOOR_LAYER
        other = mock.Mock()
        other.get_asset.return_value.get_path_name.return_value = "/Game/DataLayers/MEP.MEP"
        layer_editor = mock.Mock()
        layer_editor.get_all_data_layers.return_value = [floor, other]
        partition = mock.Mock()
        calls = mock.Mock()
        calls.attach_mock(layer_editor.set_data_layers_is_loaded_in_editor, "enable_layers")
        calls.attach_mock(partition.load_actors, "load_actors")
        loaded_room = object()
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, DataLayerEditorSubsystem=object(),
            get_editor_subsystem=lambda subsystem_class: layer_editor,
            WorldPartitionBlueprintLibrary=partition, find_object=lambda outer, path: loaded_room,
        ):
            self.assertEqual(load_selected_actors([self.room]), [loaded_room])
        self.assertEqual(calls.mock_calls, [
            mock.call.enable_layers([floor], True, False), mock.call.load_actors(["room-guid"]),
        ])


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

    def test_material_check_reports_only_missing_assets(self):
        """Name the absent texture package so it can be synced before generation."""
        missing_texture = REQUIRED_MATERIAL_ASSETS[1]
        asset_editor = SimpleNamespace(does_asset_exist=lambda path: path != missing_texture)
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, EditorAssetSubsystem=object,
            get_editor_subsystem=lambda subsystem_class: asset_editor,
        ):
            self.assertEqual(missing_material_assets(), [missing_texture])

    def test_fixture_shadows_leave_directional_light_alone(self):
        """Remove shadows from local fixtures and keep the directional light unchanged."""
        light = mock.Mock()
        light.get_editor_property.return_value = True
        fixture_type = type("Fixture", (), {"get_component_by_class": lambda self, kind: light})
        sun = mock.Mock()
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, PointLight=fixture_type,
            SpotLight=fixture_type, RectLight=fixture_type, LightComponent=object,
        ):
            self.assertEqual(disable_fixture_shadows([fixture_type(), sun]), 1)
        light.set_editor_property.assert_called_once_with("cast_shadows", False)
        sun.get_component_by_class.assert_not_called()

    def test_fixture_draw_limits_respect_coverage_and_shorter_authored_distances(self):
        """Cap typical fixtures, keep a wider light's reach, and preserve a shorter limit."""
        normal, wide, authored = [mock.Mock() for _ in range(3)]
        for light, radius, distance in ((normal, 1000, 0), (wide, 5000, 0), (authored, 1000, 2000)):
            properties = {"attenuation_radius": radius, "max_draw_distance": distance}
            light.get_editor_property.side_effect = properties.__getitem__
        fixture_type = type("Fixture", (), {"get_component_by_class": lambda self, kind: self.light})
        actors = [fixture_type() for _ in range(3)]
        for actor, light in zip(actors, (normal, wide, authored)):
            actor.light = light
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, PointLight=fixture_type,
            SpotLight=fixture_type, RectLight=fixture_type, LightComponent=object,
        ):
            self.assertEqual(limit_fixture_draw_distance(actors), 2)
        normal.set_editor_property.assert_has_calls([
            mock.call("max_draw_distance", 6000), mock.call("max_distance_fade_range", 1200),
        ])
        wide.set_editor_property.assert_any_call("max_draw_distance", 21200)
        authored.set_editor_property.assert_not_called()

    def test_surface_repairs_only_replace_mapped_fallback_slots(self):
        class MeshActor:
            def __init__(self, mesh_path, material_path):
                component = mock.Mock()
                component.get_editor_property.return_value = SimpleNamespace(get_path_name=lambda: mesh_path)
                component.get_num_materials.return_value = 2
                component.get_material.return_value = (
                    SimpleNamespace(get_path_name=lambda: material_path) if material_path else None
                )
                self.static_mesh_component = component

        table = f"{MESHES}/Shared/Furniture/SM_SecondFloor_CommonArea_Table"
        checkerboard = MeshActor(table, WORLD_GRID)
        unassigned = MeshActor(f"{MESHES}/Shared/Furniture/SM_Cushion", None)
        authored = MeshActor(table, f"{SHARED}/Metals/MI_Gray_01Metal")
        unrelated = MeshActor(f"{MESHES}/Shared/Furniture/SM_UnlistedTable", WORLD_GRID)
        replacement = SimpleNamespace()
        load_asset = mock.Mock(return_value=replacement)
        with mock.patch.multiple(
            sys.modules["unreal"], create=True, StaticMeshActor=MeshActor,
            MaterialInterface=SimpleNamespace, load_asset=load_asset,
        ):
            self.assertEqual(restore_surface_materials([checkerboard, unassigned, authored, unrelated]), 2)

        self.assertEqual(load_asset.call_count, 2)
        load_asset.assert_any_call(f"{SHARED}/Metals/MI_GrayGlossy")
        load_asset.assert_any_call(f"{SHARED}/Walls/MI_Gray_01")
        checkerboard.static_mesh_component.set_material.assert_called_once_with(1, replacement)
        unassigned.static_mesh_component.set_material.assert_called_once_with(1, replacement)
        authored.static_mesh_component.set_material.assert_not_called()
        unrelated.static_mesh_component.set_material.assert_not_called()

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

    def test_candidate_reports_are_kept_with_their_map(self):
        candidate = "/Game/Level/PFTFirstFloorReview"
        other = "/Game/Other/PFTFirstFloorReview"
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(
            sys.modules["unreal"], "Paths", SimpleNamespace(project_saved_dir=lambda: folder), create=True,
        ):
            default_report = map_report_path(MAP, "FirstFloorBuild.json")
            candidate_report = map_report_path(candidate, "FirstFloorBuild.json")
            self.assertEqual(default_report, Path(folder) / "FirstFloorBuild.json")
            self.assertNotEqual(candidate_report, map_report_path(other, "FirstFloorBuild.json"))
            write_report(default_report, {"Output": MAP, "SourceRoomIds": {"Room1120": "default"}})
            write_report(candidate_report, {"Output": candidate, "SourceRoomIds": {"Room1120": "review"}})
            self.assertEqual(read_map_build_report(MAP)["SourceRoomIds"]["Room1120"], "default")
            self.assertEqual(read_map_build_report(candidate)["SourceRoomIds"]["Room1120"], "review")
            self.assertEqual(read_map_build_report(other), {})

    def test_output_map_must_be_a_package_outside_the_source_twin(self):
        self.assertTrue(valid_output_map("/Game/Level/PFTFirstFloorReview"))
        self.assertFalse(valid_output_map("/Game/Level/patrickftaylorhall"))
        self.assertFalse(valid_output_map("/Game/Level/../../outside"))
        self.assertFalse(valid_output_map("/Game/Level/Review.Review"))

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

    def test_candidate_dependency_walk_starts_at_the_candidate(self):
        candidate = "/Game/Level/PFTFirstFloorReview"
        graph = {candidate: [SOURCE], SOURCE: []}
        registry = SimpleNamespace(get_dependencies=lambda package, options: graph[package])
        with mock.patch.object(
            sys.modules["unreal"], "AssetRegistryDependencyOptions", return_value=object(), create=True,
        ):
            visited, paths = find_dependencies(registry, candidate)
        self.assertEqual(visited, {candidate, SOURCE})
        self.assertEqual(paths[SOURCE], [candidate, SOURCE])


if __name__ == "__main__":
    unittest.main()
