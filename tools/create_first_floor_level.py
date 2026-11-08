"""Generate a first-floor map in two editor passes, preserving the full twin."""

import argparse
import collections
import json
from pathlib import Path
import re

import unreal


SOURCE_MAP = "/Game/Level/PatrickFTaylorHall"
OUTPUT_MAP = "/Game/Level/PFTFirstFloor"
FLOOR_LAYER = "/Game/DataLayers/Floor1.Floor1"
UNFILED_LAYER = "/Game/DataLayers/ToBeFiled.ToBeFiled"
UPPER_LAYERS = {
    f"/Game/DataLayers/{name}.{name}"
    for name in ("Floor2", "Floor3", "Floor4", "Roof")
}
ENVIRONMENT = {
    "DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog",
    "VolumetricCloud", "PostProcessVolume",
}


def layer_paths(descriptor):
    """Return the data-layer asset paths assigned to an actor descriptor."""
    return {path.export_text() for path in descriptor.data_layer_assets}


def first_floor_room(label):
    """Return whether an actor label identifies a first-floor room or hallway."""
    return re.match(r"^(Room|Hallway)[A-Z]*1\d{3}", label) is not None


def keep_actor(descriptor, low, high):
    """Return whether a descriptor belongs in the first-floor height band."""
    layers = layer_paths(descriptor)
    kind = descriptor.native_class.get_name()
    if descriptor.actor_is_editor_only or layers & UPPER_LAYERS:
        return False
    if kind == "StaticMeshActor" and (not layers or layers == {UNFILED_LAYER}):
        # Unfiled geometry needs a height check because it has no floor assignment.
        center = (descriptor.bounds.min.z + descriptor.bounds.max.z) / 2
        return low <= center <= high
    return (
        FLOOR_LAYER in layers
        or kind in ENVIRONMENT
        or kind == "PlayerStart"
        or first_floor_room(str(descriptor.label))
    )


def select_actors(descriptors):
    """Return selected descriptors and their height band; reject missing floor data."""
    rooms = [item for item in descriptors if first_floor_room(str(item.label))]
    if not rooms:
        raise RuntimeError("No first-floor room volumes found; refusing to guess the floor height")
    low = min(room.bounds.min.z for room in rooms) - 50
    high = max(room.bounds.max.z for room in rooms) + 25
    selected = [item for item in descriptors if keep_actor(item, low, high)]
    if not any(FLOOR_LAYER in layer_paths(item) for item in selected):
        raise RuntimeError("The first-floor data layer is empty")
    return selected, (low, high)


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


def first_floor_bindings():
    """Return source-map bindings for directory rooms assigned to the first floor."""
    folder = Path(unreal.Paths.project_plugins_dir()) / "PFTNavigator" / "Content" / "Data"
    rooms = json.loads((folder / "rooms.json").read_text(encoding="utf-8-sig"))
    bindings = json.loads((folder / "room_bindings.json").read_text(encoding="utf-8-sig"))
    floor_ids = {room["Name"] for room in rooms if room["Floor"] == 1}
    return [
        binding for binding in bindings
        if binding["Map"] == SOURCE_MAP and binding["RoomId"] in floor_ids
    ]


def add_room_markers():
    """Add missing first-floor destinations to the active map; return the total count."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = {
        actor.get_editor_property("room_id") for actor in subsystem.get_all_level_actors()
        if isinstance(actor, unreal.PFTRoomMarker)
    }
    bindings = first_floor_bindings()
    for binding in bindings:
        if binding["RoomId"] in existing:
            continue
        position = binding["Location"]
        location = unreal.Vector(position["X"], position["Y"], position["Z"])
        marker = subsystem.spawn_actor_from_class(unreal.PFTRoomMarker, location)
        if not marker:
            raise RuntimeError(f"Could not create marker for {binding['RoomId']}")
        marker.set_editor_property("room_id", binding["RoomId"])
        marker.set_actor_label(f"Destination_{binding['RoomId']}")
    return len(bindings)


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
    """Move PlayerStart actors above PFT 1120's marker; raise if that marker is absent."""
    start_marker = None
    for actor in actors:
        if isinstance(actor, unreal.PFTRoomMarker):
            if actor.get_editor_property("room_id") == "PFT_1120":
                start_marker = actor
                break
    if not start_marker:
        raise RuntimeError("The first-floor starting room needs a destination marker")
    location = start_marker.get_actor_location() + unreal.Vector(0, 0, 100)
    for actor in actors:
        if isinstance(actor, unreal.PlayerStart):
            actor.set_actor_location(location, False, True)


def finish_map(room_ids):
    """Restore source room IDs, clear reimport links, and place the player; return a count."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    restore_room_ids(actors, room_ids)
    cleared = sum(clear_import_templates(actor) for actor in actors)
    move_player_start(actors)
    return cleared


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
    unreal.WorldPartitionBlueprintLibrary.load_actors([item.guid for item in selected])
    actors = [unreal.find_object(None, str(item.actor_path)) for item in selected]
    if any(actor is None for actor in actors):
        raise RuntimeError("Some selected actors failed to load; the output has not been saved")
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
    report = read_build_report(report_path)
    if report["Output"] != output:
        raise RuntimeError("The generation report belongs to a different output map")
    world = unreal.EditorLoadingAndSavingUtils.load_map(output)
    if not world:
        raise RuntimeError("Generate the first-floor geometry before running --finish")
    report["RoomMarkers"] = add_room_markers()
    report["ClearedImportTemplates"] = finish_map(report["SourceRoomIds"])
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
    report_path = Path(unreal.Paths.project_saved_dir()) / "FirstFloorBuild.json"
    if args.finish:
        finish_saved_map(args.output, report_path)
    else:
        generate_map(args.output, report_path, args.dry_run)


if __name__ == "__main__":
    main()
