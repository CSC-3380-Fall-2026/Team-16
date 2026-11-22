"""Finish the standalone demo's exterior doors and remove campus scenery."""

import unreal


INTERIOR_ONLY_TAG = "PFTInteriorOnly"
OPAQUE_DOOR_MATERIAL = "/Game/MaterialInstances/Shared/Metals/MI_Gray_03Metal"
GLASS_MATERIALS = "/game/materialinstances/shared/glass/"
EXTERIOR_DOOR_MESHES = (
    "sm_entrancedoortype",
    "doors_sgl-al1-panel_ext-",
    "doors_pair-al1-8_in__bottom_rail-panel_ext-",
    "doors_curtain_wall_single_flush_door_curtain_wall_single_flush_door",
)
SITE_MESH_PATHS = (
    "/game/level/_generated/sm_c1003terrain.",
    "/game/staticmeshes/lsucampus/",
    "/game/staticmeshes/outdoors/sidewalks/",
    "/game/staticmeshes/outdoors/trees/",
    "/game/staticmeshes/outdoors/models/sm_lamppost",
    "/game/staticmeshes/outdoors/models/sm_outdoor",
    "/game/staticmeshes/outdoors/models/sm_pft_outside",
)

# Older standalone maps no longer carry the source data layer for this separate building.
MECHANICAL_BUILDING_ACTORS = {
    "Doors_CP_-_TYPE_A1_-_SINGLE_STEEL_4_-0__x_7_-10__Metal_5_75__x_2__HEAD",
    "Doors_Sgl-AL1-Panel_Ext-3__0_x7__0_x2__16",
    "Floors_Floor_SD_Floor_-_24_",
    "Generic_Models_METAL_DOOR_HOOD_METAL_DOOR_HOOD",
    "Mechanical_Equipment_CWP_CWP",
    "Plumbing_Fixtures_Drain_Roof-Zurn-Z105-IC-Control_Flo-Parabolic_Weir_4_Inch_Outlet_58",
    "Plumbing_Fixtures_Drain_Roof-Zurn-Z105-IC-Control_Flo-Parabolic_Weir_4_Inch_Outlet_59",
    "Roofs_Basic_Roof_LSU_PTH_-_Tapered_Insulation_-_SBS",
    "Stairs_Stair_Concrete_Stairs_Entry",
    "Structural_Columns_Concrete_-_Column-18x18_Concrete_-_Column-18x18",
    "Structural_Framing_Beam_at_Mechanical_Bldg_Beam_at_Mechanical_Bldg",
    "Structural_Framing_Concrete-Rectangular_Beam1_MRB-1_4",
    "Wall_Sweeps_Wall_Sweep_Parapet_Coping_at_Mechanical_Building",
    "Walls_Basic_Wall_D41_220",
    "Walls_Basic_Wall_D41_221",
    "Walls_Basic_Wall_D41_222",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster_2",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster_3",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster_4",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster_5",
    "Walls_Basic_Wall_Exterior_-_7_8__CMP_Plaster_6",
    "Walls_Basic_Wall_Exterior_-_CMU",
    "Walls_Basic_Wall_Exterior_Brick_on_6__CMU",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU_2",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU_3",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU_4",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU_5",
    "Walls_Basic_Wall_SD_Generic_-_Exterior_Brick_on_6__CMU_6",
    "Walls_Basic_Wall_SD_Generic_-__6__Partition",
    "Walls_Basic_Wall_SD_Generic_-__6__Partition_2",
    "Walls_Basic_Wall_SD_Generic_-__6__Partition_3",
}


def mesh_components(actor):
    """Yield static mesh components with their asset paths."""
    for component in actor.get_components_by_class(unreal.StaticMeshComponent):
        mesh = component.get_editor_property("static_mesh")
        if mesh:
            yield component, mesh.get_path_name()


def is_site_mesh(path):
    """Recognize campus scenery assets without relying on the actor's distance."""
    return path.casefold().startswith(SITE_MESH_PATHS)


def is_exterior_door(path):
    """Recognize the exterior door families used in the first-floor map."""
    mesh_name = path.rsplit("/", 1)[-1].split(".", 1)[0]
    return mesh_name.casefold().startswith(EXTERIOR_DOOR_MESHES)


def glass_slots(component):
    """Yield the material slots for glass panes on a door component."""
    for index in range(component.get_num_materials()):
        material = component.get_material(index)
        if material and material.get_path_name().casefold().startswith(GLASS_MATERIALS):
            yield index


def exterior_parts(actors):
    """Separate removable scenery, mixed actors, and exterior door components."""
    scenery = []
    mixed = []
    doors = []
    for actor in actors:
        if actor.get_actor_label() in MECHANICAL_BUILDING_ACTORS:
            scenery.append(actor)
            continue
        components = list(mesh_components(actor))
        site_meshes = [is_site_mesh(path) for _, path in components]
        if any(site_meshes):
            (scenery if all(site_meshes) else mixed).append(actor)
            continue
        doors.extend(component for component, path in components if is_exterior_door(path))
    return scenery, mixed, doors


def finish_exterior(world, subsystem, actors):
    """Make the exit panes opaque and remove only scenery actors from the demo."""
    scenery, mixed, doors = exterior_parts(actors)
    if mixed:
        labels = ", ".join(actor.get_actor_label() for actor in mixed)
        raise RuntimeError(f"Exterior scenery shares an actor with the building: {labels}")
    if not doors:
        raise RuntimeError("No exterior doors were found in the first-floor demo")
    panes = [(component, index) for component in doors for index in glass_slots(component)]
    if panes:
        material = unreal.load_asset(OPAQUE_DOOR_MATERIAL)
        if not isinstance(material, unreal.MaterialInterface):
            raise RuntimeError(f"Could not load door panel material {OPAQUE_DOOR_MATERIAL}")
        for component, index in panes:
            component.set_material(index, material)
    if scenery and not subsystem.destroy_actors(scenery):
        raise RuntimeError("Could not remove exterior scenery from the demo")
    settings = world.get_world_settings()
    tags = settings.get_editor_property("tags")
    if INTERIOR_ONLY_TAG not in [str(tag) for tag in tags]:
        settings.set_editor_property("tags", list(tags) + [INTERIOR_ONLY_TAG])
    return {"OpaqueExitPanels": len(panes), "RemovedSceneryActors": len(scenery)}


def check_exterior_finish(actors):
    """Return errors if an interior-only map still contains site props or exit glass."""
    scenery, mixed, doors = exterior_parts(actors)
    errors = [f"Exterior scenery remains: {actor.get_actor_label()}" for actor in scenery + mixed]
    if not doors:
        errors.append("Missing exterior doors")
    for component in doors:
        if list(glass_slots(component)):
            errors.append(f"Exterior door is still see-through: {component.get_owner().get_actor_label()}")
    return errors
