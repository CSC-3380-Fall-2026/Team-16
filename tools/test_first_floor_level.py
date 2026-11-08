import sys
import types
import unittest
from types import SimpleNamespace

sys.modules.setdefault("unreal", types.ModuleType("unreal"))
from CreateFirstFloorLevel import FLOOR_LAYER, select_actors


def actor(label, layer="", kind="StaticMeshActor", low=0, high=100, editor_only=False):
    path = SimpleNamespace(export_text=lambda: layer)
    return SimpleNamespace(
        label=label,
        native_class=SimpleNamespace(get_name=lambda: kind),
        data_layer_assets=[path] if layer else [],
        actor_is_editor_only=editor_only,
        bounds=SimpleNamespace(min=SimpleNamespace(z=low), max=SimpleNamespace(z=high)),
    )


class FirstFloorSelectionTests(unittest.TestCase):
    def setUp(self):
        self.room = actor("Room1100", FLOOR_LAYER, "PFTAreaTriggers", 0, 400)

    def test_upper_floors_roof_and_mep_are_excluded(self):
        excluded = [actor(name, f"/Game/DataLayers/{name}.{name}") for name in ("Floor2", "Floor3", "Floor4", "Roof", "MEP")]
        selected, _ = select_actors([self.room] + excluded)
        self.assertEqual([self.room], selected)

    def test_unfiled_and_unlayered_geometry_use_first_floor_height(self):
        ground = actor("GroundWall", "/Game/DataLayers/ToBeFiled.ToBeFiled", low=0, high=350)
        upper = actor("UpperWall", low=900, high=1200)
        selected, _ = select_actors([self.room, ground, upper])
        self.assertEqual([self.room, ground], selected)

    def test_shared_lighting_and_room_volumes_are_kept(self):
        light = actor("SkyLight", "/Game/DataLayers/LSUCampusWide.LSUCampusWide", "SkyLight", 5000, 5000)
        room = actor("HallwayC1010", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers")
        upper_room = actor("Room3325", "/Game/DataLayers/TriggerVolumes.TriggerVolumes", "PFTAreaTriggers", 1000, 1400)
        selected, _ = select_actors([self.room, light, room, upper_room])
        self.assertEqual([self.room, light, room], selected)

    def test_editor_only_actors_are_excluded(self):
        selected, _ = select_actors([self.room, actor("EditorHelper", FLOOR_LAYER, editor_only=True)])
        self.assertEqual([self.room], selected)

    def test_missing_room_volumes_fail(self):
        with self.assertRaises(RuntimeError):
            select_actors([actor("Floor", FLOOR_LAYER)])

    def test_missing_first_floor_layer_fails(self):
        with self.assertRaises(RuntimeError):
            select_actors([actor("Room1100", kind="PFTAreaTriggers")])


if __name__ == "__main__":
    unittest.main()
