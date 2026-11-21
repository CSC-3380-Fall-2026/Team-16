"""Make the demo's exterior glazing opaque and remove campus scenery."""

import unreal


INTERIOR_ONLY_TAG = "PFTInteriorOnly"
OPAQUE_GLAZING_TAG = "PFTExteriorGlazingOpaque"
OPAQUE_PANEL_MATERIAL = "/Game/MaterialInstances/Shared/Metals/MI_Gray_03Metal"
GLASS_MATERIALS = (
    "/game/materialinstances/shared/glass/",
    "/basicmastermaterials/materials/materialinstances/surfaces/mi_glass_",
    "/basicmastermaterials/materials/materialinstances/surfaces/mi_cheapglass.",
    "/basicmastermaterials/materials/masters/m_glass_",
)
EXTERIOR_DOOR_MESHES = (
    "sm_entrancedoortype",
    "doors_sgl-al1-panel_ext-",
    "doors_pair-al1-8_in__bottom_rail-panel_ext-",
    "doors_curtain_wall_single_flush_door_curtain_wall_single_flush_door",
)
FACADE_GLAZING_MESHES = (
    "sm_glasswall_",
    "sm_glasswindow_3rdfloor_",
    "sm_wallglasswindow_",
    "doors_double-flush-transom_72__x_84__x_24__transom",
)
CLERESTORY_MIN_HEIGHT = 1400
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


def mesh_name(path):
    """Return the case-insensitive name of the mesh asset."""
    return path.rsplit("/", 1)[-1].split(".", 1)[0].casefold()


def is_exterior_door(path):
    """Recognize the exterior door families used in older demo maps."""
    return mesh_name(path).startswith(EXTERIOR_DOOR_MESHES)


def is_facade_glazing(component, path):
    """Identify the facade assemblies and the glass band above the roof."""
    name = mesh_name(path)
    if name.startswith(FACADE_GLAZING_MESHES):
        return True
    if name == "sm_wallcurtainglass":
        # The same panel also encloses indoor atriums below the roof.
        center, _, _ = unreal.SystemLibrary.get_component_bounds(component)
        return center.z >= CLERESTORY_MIN_HEIGHT
    return False


def glass_slots(component):
    """Yield glass material slots without including the surrounding frame."""
    for index in range(component.get_num_materials()):
        material = component.get_material(index)
        if material and material.get_path_name().casefold().startswith(GLASS_MATERIALS):
            yield index


def exterior_parts(actors, include_glazing):
    """Collect site scenery, exit doors, and fixed glass on the building envelope."""
    scenery = []
    mixed = []
    doors = []
    glazing = []
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
        if include_glazing:
            glazing.extend(component for component, path in components if is_facade_glazing(component, path))
    return scenery, mixed, doors, glazing


def finish_exterior(world, subsystem, actors):
    """Make exterior panes opaque and remove site scenery from the demo."""
    scenery, mixed, doors, glazing = exterior_parts(actors, include_glazing=True)
    if mixed:
        labels = ", ".join(actor.get_actor_label() for actor in mixed)
        raise RuntimeError(f"Exterior scenery shares an actor with the building: {labels}")
    if not doors:
        raise RuntimeError("No exterior doors were found in the first-floor demo")
    if not glazing:
        raise RuntimeError("No facade glazing was found in the first-floor demo")
    exit_panes = [(component, index) for component in doors for index in glass_slots(component)]
    facade_panes = [(component, index) for component in glazing for index in glass_slots(component)]
    panes = exit_panes + facade_panes
    if panes:
        material = unreal.load_asset(OPAQUE_PANEL_MATERIAL)
        if not isinstance(material, unreal.MaterialInterface):
            raise RuntimeError(f"Could not load opaque panel material {OPAQUE_PANEL_MATERIAL}")
        for component, index in panes:
            component.set_material(index, material)
    if scenery and not subsystem.destroy_actors(scenery):
        raise RuntimeError("Could not remove exterior scenery from the demo")
    settings = world.get_world_settings()
    tags = settings.get_editor_property("tags")
    existing_tags = {str(tag) for tag in tags}
    missing_tags = [tag for tag in (INTERIOR_ONLY_TAG, OPAQUE_GLAZING_TAG) if tag not in existing_tags]
    if missing_tags:
        settings.set_editor_property("tags", list(tags) + missing_tags)
    return {
        "OpaqueExitPanels": len(exit_panes),
        "OpaqueFacadePanels": len(facade_panes),
        "RemovedSceneryActors": len(scenery),
    }


def check_exterior_finish(actors, include_glazing=False):
    """Return errors if the saved map still shows site scenery or see-through exterior panes."""
    scenery, mixed, doors, glazing = exterior_parts(actors, include_glazing)
    errors = [f"Exterior scenery remains: {actor.get_actor_label()}" for actor in scenery + mixed]
    if not doors:
        errors.append("Missing exterior doors")
    if include_glazing and not glazing:
        errors.append("Missing facade glazing")
    for component in doors + glazing:
        if next(glass_slots(component), None) is not None:
            errors.append(f"Exterior glazing is still see-through: {component.get_owner().get_actor_label()}")
    return errors
