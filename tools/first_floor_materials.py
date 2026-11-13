"""Restore known checkerboard surfaces with materials already used in the building."""

import unreal


WORLD_GRID = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
SHARED = "/Game/MaterialInstances/Shared"
HARDSCAPING = "/Game/MaterialInstances/Outdoors/Hardscaping"
MESHES = "/Game/StaticMeshes"
COUCHES = f"{MESHES}/Shared/Furniture/DeanSuiteFurniture/CommonAreaCouch"

MATERIALS = {
    f"{MESHES}/Shared/Furniture/SM_SecondFloor_CommonArea_Table": {
        1: f"{SHARED}/Metals/MI_GrayGlossy",
    },
    f"{COUCHES}/SM_PFT_WestWingCommonAreaCouch_2ndFloor_1": {
        1: f"{SHARED}/Metals/MI_Gray_01Metal",
    },
    f"{COUCHES}/SM_PFT_WestWingCommonAreaCouch_2ndFloor_2": {
        1: f"{SHARED}/Metals/MI_Gray_01Metal",
    },
    f"{MESHES}/1stFloor/Zone1000/C1003/SM_Wall_1stFloorHallway": {
        2: f"{SHARED}/Walls/MI_YellowWall_Light",
    },
    f"{MESHES}/1stFloor/Zone1200/1221/SM_1221FloorPiece_01": {
        0: "/Game/MaterialInstances/2ndFloor/Zone2200/C2002/MI_DarkCarpetTemp",
    },
    f"{MESHES}/1stFloor/Zone1300/1363/SM_1363EFloorPiece": {
        0: f"{SHARED}/Plastic/MI_GrayPlastic2",
    },
    f"{MESHES}/1stFloor/Zone1300/1375-Panera/SM_SodaFountain": {
        1: f"{SHARED}/Metals/MI_Gray_01MetalR50",
        2: f"{SHARED}/Plastic/MI_BlackSurface",
        3: f"{SHARED}/Plastic/MI_TrashBrown",
        4: f"{SHARED}/Glass/MI_Glass_Medium",
    },
    f"{MESHES}/Outdoors/SideWalks/SM_ParkingLot_01": {
        0: f"{HARDSCAPING}/MI_DestroyedRoad",
    },
}


def checkerboard_slots(actors):
    """Find replaceable material slots on the standalone map's mesh components."""
    for actor in actors:
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        component = actor.static_mesh_component
        mesh = component.get_editor_property("static_mesh")
        if not mesh:
            continue
        path = mesh.get_path_name().split(".", 1)[0]
        materials = MATERIALS.get(path, {})
        if path.startswith(f"{MESHES}/Foundation/SM_Slab"):
            materials = {0: f"{SHARED}/Cement/MI_Concrete"}
        elif path.lower().startswith(f"{MESHES}/Outdoors/SideWalks/SM_Sidewalk_".lower()):
            materials = {0: f"{HARDSCAPING}/MI_GravelConcrete"}
        for index, replacement in materials.items():
            material = component.get_material(index)
            if material and material.get_path_name() == WORLD_GRID:
                yield component, index, replacement


def restore_surface_materials(actors):
    """Override only checkerboard slots, leaving shared meshes and valid overrides alone."""
    slots = list(checkerboard_slots(actors))
    materials = {}
    for path in sorted({path for _, _, path in slots}):
        material = unreal.load_asset(path)
        if not isinstance(material, unreal.MaterialInterface):
            raise RuntimeError(f"Could not load replacement material {path}")
        materials[path] = material
    for component, index, path in slots:
        component.set_material(index, materials[path])
    return len(slots)
