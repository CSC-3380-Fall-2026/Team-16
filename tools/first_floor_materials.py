"""Restore missing surfaces with materials already used in the building."""

import unreal


WORLD_GRID = "/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial"
SHARED = "/Game/MaterialInstances/Shared"
HARDSCAPING = "/Game/MaterialInstances/Outdoors/Hardscaping"
MESHES = "/Game/StaticMeshes"
COUCHES = f"{MESHES}/Shared/Furniture/DeanSuiteFurniture/CommonAreaCouch"
UPPER_FLOOR_CARPET = f"{SHARED}/Flooring/MI_GrayCarpet_Dark_2_WS"

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
    f"{MESHES}/2ndFloor/Zone2200/2203/SM_FloorPiece_2203D": {
        0: f"{SHARED}/Flooring/MI_TerrazoWS",
    },
    f"{MESHES}/3rdFloor/Zone3200/3201/SM_3201_FloorPiece_02": {0: UPPER_FLOOR_CARPET},
    f"{MESHES}/3rdFloor/Zone3200/3202/SM_3202FloorPiece_03": {0: UPPER_FLOOR_CARPET},
    f"{MESHES}/Shared/Furniture/DeanSuiteFurniture/SM_DeanConferenceTableChair": {
        0: "/Game/MaterialInstances/2ndFloor/Zone2200/2214A/MI_DeanConferenceRoomPack",
    },
    f"{MESHES}/Shared/Furniture/SM_Cushion": {1: f"{SHARED}/Walls/MI_Gray_01"},
    f"{MESHES}/Shared/Furniture/DeanSuiteFurniture/SM_DeansSuiteCouch": {
        1: f"{SHARED}/Plastic/MI_GrayPlastic3",
    },
    f"{MESHES}/RevitBuildoutModels/Casework_Lab_Bench_-_TB_TB-60": {
        0: f"{SHARED}/Walls/MI_Gray_01",
    },
}


def fallback_material_slots(actors):
    """Yield the component, mesh path, index, and material for each fallback slot."""
    for actor in actors:
        if not isinstance(actor, unreal.StaticMeshActor):
            continue
        component = actor.static_mesh_component
        mesh = component.get_editor_property("static_mesh")
        if not mesh:
            continue
        path = mesh.get_path_name().split(".", 1)[0]
        for index in range(component.get_num_materials()):
            material = component.get_material(index)
            if material is None or material.get_path_name() == WORLD_GRID:
                yield component, path, index, material


def replacement_material(mesh_path, index):
    """Return the matching building material for a known mesh slot."""
    if index == 0 and mesh_path.startswith(f"{MESHES}/Foundation/SM_Slab"):
        return f"{SHARED}/Cement/MI_Concrete"
    if index == 0 and mesh_path.lower().startswith(f"{MESHES}/Outdoors/SideWalks/SM_Sidewalk_".lower()):
        return f"{HARDSCAPING}/MI_GravelConcrete"
    return MATERIALS.get(mesh_path, {}).get(index)


def restore_surface_materials(actors):
    """Fill known fallback slots on map components and return the repair count."""
    slots = []
    for component, mesh_path, index, _ in fallback_material_slots(actors):
        replacement = replacement_material(mesh_path, index)
        if replacement:
            slots.append((component, index, replacement))
    materials = {}
    for path in sorted({path for _, _, path in slots}):
        material = unreal.load_asset(path)
        if not isinstance(material, unreal.MaterialInterface):
            raise RuntimeError(f"Could not load replacement material {path}")
        materials[path] = material
    for component, index, path in slots:
        component.set_material(index, materials[path])
    return len(slots)
