"""Generate a first-floor map in two editor passes, preserving the full twin."""

import argparse
import collections
import json
from pathlib import Path
import re
import sys

import unreal


SOURCE_MAP = "/Game/Level/PatrickFTaylorHall"
OUTPUT_MAP = "/Game/Level/PFTFirstFloorComplete"
FLOOR_LAYER = "/Game/DataLayers/Floor1.Floor1"
ROOF_LAYER = "/Game/DataLayers/Roof.Roof"
UNFILED_LAYER = "/Game/DataLayers/ToBeFiled.ToBeFiled"
UPPER_LAYERS = {
    f"/Game/DataLayers/{name}.{name}"
    for name in ("Floor2", "Floor3", "Floor4", "Roof")
}
SITE_LAYERS = {
    f"/Game/DataLayers/{name}.{name}"
    for name in ("Landscapes", "MechanicalBuilding", "OtherLSUBuildings", "LSUCampusWide")
}
HIDDEN_LAYERS = {
    f"/Game/DataLayers/{name}.{name}"
    for name in ("MEP", "Cinematic")
}
UPPER_ROOM_CONTENT = (
    "Casework_", "Electrical_Fixtures_N-Raceway", "Furniture_", "Specialty_Equipment_",
)
SITE_MARGIN = 3000
BUILDING_VISUALS = {"StaticMeshActor", "PointLight", "RectLight", "SpotLight"}
WORLD_PARTITION_ACTORS = {"WorldPartitionHLOD", "LandscapeStreamingProxy", "Landscape"}
ENVIRONMENT = {
    "DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog",
    "VolumetricCloud", "PostProcessVolume",
}
MAIN_HALLWAY_FLOOR = "Floor_MainHallway"
CAMPUS_ROAD_OVERLAY = "SM_road_full_block"
C1003_TERRAIN = "/Game/Level/_GENERATED/SM_C1003Terrain"
C1003_TERRAIN_LABEL = "C1003_ExteriorTerrain"
DEMO_START_LOCATION = (9714, -6296, 101)
DEMO_START_YAW = -46.912
REQUIRED_MATERIAL_ASSETS = (
    "/BasicMasterMaterials/Materials/Masters/M_OpaqueBasic",
    "/BasicMasterMaterials/Textures/MaterialsBase/T_Base_ORMS",
    "/DTGenericCatalog/Materials/MasterMaterials/M_OpaqueNoTextures",
    "/Game/ARCH-LSU-PFT-Buildout_DetachedR25_002/Materials/Phase-Exist",
)


def layer_paths(descriptor):
    """Return the data-layer asset paths assigned to an actor descriptor."""
    return {path.export_text() for path in descriptor.data_layer_assets}


def first_floor_room(label):
    """Return whether an actor label identifies a first-floor room or hallway."""
    return re.match(r"^(Room|Hallway)[A-Z]*1\d{3}", label) is not None


def near_building(bounds, footprint):
    """Return whether an actor overlaps the building or its immediate surroundings."""
    west, south, east, north = footprint
    return (
        bounds.min.x <= east + SITE_MARGIN and bounds.max.x >= west - SITE_MARGIN
        and bounds.min.y <= north + SITE_MARGIN and bounds.max.y >= south - SITE_MARGIN
    )


def keep_actor(descriptor, floor_bottom, floor_top, footprint):
    """Return whether the actor belongs in the first-floor map based on its layer and bounds."""
    layers = layer_paths(descriptor)
    kind = descriptor.native_class.get_name()
    label = str(descriptor.label)
    if descriptor.actor_is_editor_only or kind in WORLD_PARTITION_ACTORS:
        return False
    if kind == "StaticMeshActor" and label == CAMPUS_ROAD_OVERLAY:
        return False
    if kind == "PFTAreaTriggers":
        return first_floor_room(label)
    if kind in ENVIRONMENT or kind == "PlayerStart" or FLOOR_LAYER in layers:
        return True
    # The main hallway floor is a group of meshes on a plain Actor.
    if kind == "Actor" and label == MAIN_HALLWAY_FLOOR:
        return True
    if layers and layers <= HIDDEN_LAYERS:
        return False
    if kind not in BUILDING_VISUALS:
        return False
    if ROOF_LAYER in layers:
        return True
    if layers & SITE_LAYERS and not layers & UPPER_LAYERS:
        return near_building(descriptor.bounds, footprint)
    bounds = descriptor.bounds
    if bounds.min.z <= floor_top and bounds.max.z >= floor_bottom:
        return bool(layers & UPPER_LAYERS) or near_building(bounds, footprint)
    # Keep ambiguous upper geometry because it may close an atrium or be visible through glazing.
    if layers & UPPER_LAYERS or not layers or UNFILED_LAYER in layers:
        return near_building(bounds, footprint) and not label.startswith(UPPER_ROOM_CONTENT)
    return False


def select_actors(descriptors):
    """Return selected descriptors and their height band; reject missing floor data."""
    rooms = [item for item in descriptors if first_floor_room(str(item.label))]
    if not rooms:
        raise RuntimeError("No first-floor room volumes found; refusing to guess the floor height")
    floor_bottom = min(room.bounds.min.z for room in rooms) - 50
    floor_top = max(room.bounds.max.z for room in rooms) + 25
    footprint = (
        min(room.bounds.min.x for room in rooms), min(room.bounds.min.y for room in rooms),
        max(room.bounds.max.x for room in rooms), max(room.bounds.max.y for room in rooms),
    )
    selected = [item for item in descriptors if keep_actor(item, floor_bottom, floor_top, footprint)]
    if not any(FLOOR_LAYER in layer_paths(item) for item in selected):
        raise RuntimeError("The first-floor data layer is empty")
    return selected, (floor_bottom, floor_top)


def read_build_report(report_path):
    """Return a build report with PascalCase fields, accepting earlier snake_case fields."""
    report = {}
    for name, value in json.loads(report_path.read_text(encoding="utf-8")).items():
        field = "".join(part.capitalize() for part in name.split("_")) if name.islower() else name
        report[field] = value
    report.setdefault("Name", "FirstFloorBuild")
    return report


def write_report(report_path, report):
    """Write a report dictionary as readable UTF-8 JSON; return nothing."""
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def missing_material_assets():
    """Return the required material and texture assets missing from the workspace."""
    asset_editor = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    return [path for path in REQUIRED_MATERIAL_ASSETS if not asset_editor.does_asset_exist(path)]


def restore_room_ids(actors, room_ids):
    """Apply source IDs by actor label; raise when a copied room has no matching ID."""
    for actor in actors:
        if not isinstance(actor, unreal.PFTAreaTriggers):
            continue
        room_id = room_ids.get(actor.get_actor_label())
        if not room_id:
            raise RuntimeError(f"Missing source room ID for {actor.get_actor_label()}")
        actor.set_editor_property("source_room_id", room_id)


def copy_metadata(component, user_data):
    """Return metadata-only Datasmith data for a component, retaining other user data."""
    if not isinstance(user_data, unreal.DatasmithUserData):
        return user_data
    metadata = unreal.new_object(unreal.DatasmithUserData, outer=component)
    metadata.get_editor_property("metadata").update(user_data.get_editor_property("metadata"))
    return metadata


def clear_import_templates(actor):
    """Replace an actor's reimport data with metadata-only copies; return their count."""
    cleared = 0
    for component in actor.get_components_by_class(unreal.ActorComponent):
        for property_name in ("asset_user_data", "asset_user_data_editor_only"):
            user_data = component.get_editor_property(property_name)
            cleared += sum(isinstance(item, unreal.DatasmithUserData) for item in user_data)
            clean_data = [copy_metadata(component, item) for item in user_data]
            component.set_editor_property(property_name, clean_data)
    return cleared


def move_player_start(actors):
    """Place the demo's single player start in C1003 facing the Zone 1200 signs."""
    starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    if len(starts) != 1:
        raise RuntimeError(f"Expected one first-floor PlayerStart, found {len(starts)}")
    starts[0].set_actor_location_and_rotation(
        unreal.Vector(*DEMO_START_LOCATION), unreal.Rotator(yaw=DEMO_START_YAW), False, True,
    )


def local_lights(actors):
    """Yield light components on the imported point, spot, and rect light actors."""
    for actor in actors:
        if isinstance(actor, (unreal.PointLight, unreal.SpotLight, unreal.RectLight)):
            yield actor.get_component_by_class(unreal.LightComponent)


def disable_fixture_shadows(actors):
    """Keep fixture lighting while removing local shadow passes from the demo map."""
    disabled = 0
    for light in local_lights(actors):
        if light.get_editor_property("cast_shadows"):
            light.set_editor_property("cast_shadows", False)
            disabled += 1
    return disabled


def add_entrance_terrain(subsystem, actors):
    """Place the textured source terrain at its baked world coordinates."""
    mesh = unreal.EditorAssetLibrary.load_asset(C1003_TERRAIN)
    if not mesh:
        raise RuntimeError(f"Missing {C1003_TERRAIN}; sync or bake the C1003 terrain first")
    terrain = next((actor for actor in actors if actor.get_actor_label() == C1003_TERRAIN_LABEL), None)
    if terrain is None:
        terrain = subsystem.spawn_actor_from_object(mesh, unreal.Vector(), unreal.Rotator())
        if terrain is None:
            raise RuntimeError("Could not place the C1003 exterior terrain")
        terrain.set_actor_label(C1003_TERRAIN_LABEL)
    terrain.static_mesh_component.set_static_mesh(mesh)
    terrain.static_mesh_component.set_collision_profile_name("NoCollision")
    return terrain


def baked_terrain_textures(material):
    """Return the material instance's saved textures without requiring a graphics device."""
    if not isinstance(material, unreal.MaterialInstanceConstant):
        return []
    library = unreal.MaterialEditingLibrary
    textures = []
    for name in library.get_texture_parameter_names(material):
        texture = library.get_material_instance_texture_parameter_value(material, name)
        if texture and texture.get_path_name().startswith(C1003_TERRAIN + "."):
            textures.append(texture)
    return textures


def finish_map(room_ids):
    """Finish room IDs, metadata, player placement, terrain, and lighting; return counts."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    restore_room_ids(actors, room_ids)
    cleared = sum(clear_import_templates(actor) for actor in actors)
    move_player_start(actors)
    add_entrance_terrain(subsystem, actors)
    return cleared, disable_fixture_shadows(actors)


def load_source_map():
    """Return the full twin without restoring previously loaded editor regions."""
    settings_class = unreal.load_class(None, "/Script/Engine.WorldPartitionEditorPerProjectUserSettings")
    settings = unreal.get_default_object(settings_class)
    restore_regions = settings.get_editor_property("bDisableLoadingOfLastLoadedRegions")
    settings.set_editor_property("bDisableLoadingOfLastLoadedRegions", True)
    try:
        world = unreal.EditorLoadingAndSavingUtils.load_map(SOURCE_MAP)
    finally:
        settings.set_editor_property("bDisableLoadingOfLastLoadedRegions", restore_regions)
    if not world:
        raise RuntimeError(f"Could not load {SOURCE_MAP}")
    return world


def build_report(output, descriptors, selected, band):
    """Return actor counts and selection details for the chosen output map."""
    layers = ["|".join(sorted(layer_paths(item))) or "(none)" for item in selected]
    return {
        "Name": "FirstFloorBuild",
        "Source": SOURCE_MAP,
        "Output": output,
        "SourceActors": len(descriptors),
        "SelectedActors": len(selected),
        "FloorHeightRange": band,
        "Classes": dict(collections.Counter(item.native_class.get_name() for item in selected)),
        "Layers": dict(collections.Counter(layers)),
    }


def load_selected_actors(selected):
    """Load selected descriptors and return their actors; raise if any actor is missing."""
    selected_layers = {layer for item in selected for layer in layer_paths(item)}
    layer_editor = unreal.get_editor_subsystem(unreal.DataLayerEditorSubsystem)
    layers_to_load = []
    for layer in layer_editor.get_all_data_layers():
        asset = layer.get_asset()
        if asset and asset.get_path_name() in selected_layers:
            layers_to_load.append(layer)
    # The actor loader skips actors whose layers are disabled in the editor.
    layer_editor.set_data_layers_is_loaded_in_editor(layers_to_load, True, False)
    unreal.WorldPartitionBlueprintLibrary.load_actors([item.guid for item in selected])
    actors = [unreal.find_object(None, str(item.actor_path)) for item in selected]
    missing = [item for item, actor in zip(selected, actors) if actor is None]
    if missing:
        examples = ", ".join(f"{item.label} ({item.actor_package})" for item in missing[:10])
        raise RuntimeError(f"Could not load {len(missing)} selected actors: {examples}")
    return actors


def copy_actors(source, actors, output):
    """Copy actors and world settings into a new standalone output world; return it."""
    folder, name = output.rsplit("/", 1)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    target = tools.create_asset(name, folder, unreal.World, unreal.WorldFactory())
    if not target or target.get_world_settings().get_editor_property("world_partition"):
        raise RuntimeError("The output must be a standalone world without World Partition")
    for property_name in ("default_game_mode", "world_to_meters", "kill_z", "enable_world_bounds_checks"):
        value = source.get_world_settings().get_editor_property(property_name)
        target.get_world_settings().set_editor_property(property_name, value)
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    copies = subsystem.duplicate_actors(actors, target)
    if len(copies) != len(actors):
        raise RuntimeError(f"Only {len(copies)} of {len(actors)} actors copied; output not saved")
    copied = set(copies)
    for actor in copies:
        if actor.get_attach_parent_actor() and actor.get_attach_parent_actor() not in copied:
            rule = unreal.DetachmentRule.KEEP_WORLD
            actor.detach_from_actor(rule, rule, rule)
        actor.set_editor_property("data_layer_assets", [])
        actor.set_editor_property("hlod_layer", None)
    return target


def generate_map(output, report_path, dry_run):
    """Write selection results or generate output geometry; leave the full twin unsaved."""
    if not dry_run and unreal.EditorAssetLibrary.does_asset_exist(output):
        raise RuntimeError(
            "Output already exists. Use --output with a new map name to preserve existing edits"
        )
    source = load_source_map()
    descriptors = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    selected, band = select_actors(descriptors)
    report = build_report(output, descriptors, selected, band)
    report["DryRun"] = dry_run
    if dry_run:
        write_report(report_path, report)
        unreal.log(f"First floor dry run: {len(selected)} of {len(descriptors)} actors")
        return
    unreal.log(f"Loading {len(selected)} first-floor actors")
    actors = load_selected_actors(selected)
    report["SourceRoomIds"] = {
        actor.get_actor_label(): actor.get_editor_property("UniqueIDString")
        for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)
    }
    target = copy_actors(source, actors, output)
    if not unreal.EditorLoadingAndSavingUtils.save_map(target, output):
        raise RuntimeError("Could not save the first-floor map")
    report.update({"CopiedActors": len(actors), "Complete": False})
    write_report(report_path, report)
    unreal.log(f"FIRST_FLOOR_GEOMETRY_COMPLETE: {output}; run --finish in a fresh editor process")


def finish_saved_map(output, report_path):
    """Finish saved geometry using its build report, then save markers and source IDs."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from first_floor_destinations import add_room_markers

    report = read_build_report(report_path)
    if report["Output"] != output:
        raise RuntimeError("The generation report belongs to a different output map")
    world = unreal.EditorLoadingAndSavingUtils.load_map(output)
    if not world:
        raise RuntimeError("Generate the first-floor geometry before running --finish")
    unreal.SystemLibrary.execute_console_command(world, "Editor.AsyncStaticMeshCompilationFinishAll")
    report["RoomMarkers"] = add_room_markers(world)
    cleared_templates, disabled_shadows = finish_map(report["SourceRoomIds"])
    report["ClearedImportTemplates"] = cleared_templates
    report["FixtureShadowsDisabled"] = disabled_shadows
    if not unreal.EditorLoadingAndSavingUtils.save_map(world, output):
        raise RuntimeError("Could not save room markers; use p4 edit if the map is versioned")
    report.update({"Complete": True, "DryRun": False})
    write_report(report_path, report)
    unreal.log(f"FIRST_FLOOR_BUILD_COMPLETE: {output}, {report['SelectedActors']} copied actors")


def main():
    """Read CLI options and run one generation pass; raise on invalid editor settings."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=OUTPUT_MAP)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--finish", action="store_true", help="Finish geometry in a fresh editor process")
    args = parser.parse_args()
    if "-run=" in unreal.SystemLibrary.get_command_line().lower():
        raise RuntimeError("Use -ExecutePythonScript, not -run=PythonScript; duplication needs the editor")
    if not args.output.startswith("/Game/") or args.output == SOURCE_MAP:
        raise RuntimeError("Output must be a new map under /Game/")
    if not args.dry_run:
        missing_assets = missing_material_assets()
        if missing_assets:
            raise RuntimeError("Sync the missing material assets before building: " + ", ".join(missing_assets))
    report_path = Path(unreal.Paths.project_saved_dir()) / "FirstFloorBuild.json"
    if args.finish:
        finish_saved_map(args.output, report_path)
    else:
        generate_map(args.output, report_path, args.dry_run)


if __name__ == "__main__":
    main()
