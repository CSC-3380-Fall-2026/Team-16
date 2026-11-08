"""Create a standalone first-floor map without changing the full twin."""

import argparse
import collections
import json
from pathlib import Path
import re

import unreal


SOURCE_MAP = "/Game/Level/PatrickFTaylorHall"
OUTPUT_MAP = "/Game/Level/PFTFirstFloor"
FLOOR_LAYER = "/Game/DataLayers/Floor1.Floor1"
UPPER_LAYERS = {f"/Game/DataLayers/{name}.{name}" for name in ("Floor2", "Floor3", "Floor4", "Roof")}
ENVIRONMENT = {"DirectionalLight", "SkyLight", "SkyAtmosphere", "ExponentialHeightFog", "VolumetricCloud", "PostProcessVolume"}


def layer_paths(desc):
    return {path.export_text() for path in desc.data_layer_assets}


def first_floor_room(label):
    return re.match(r"^(Room|Hallway)[A-Z]*1\d{3}", label) is not None


def select_actors(descs):
    rooms = [d for d in descs if first_floor_room(str(d.label))]
    if not rooms:
        raise RuntimeError("No first-floor room volumes found; refusing to guess the floor height")
    low = min(d.bounds.min.z for d in rooms) - 50
    high = max(d.bounds.max.z for d in rooms) + 25
    selected = []
    for desc in descs:
        layers = layer_paths(desc)
        kind = desc.native_class.get_name()
        if desc.actor_is_editor_only or layers & UPPER_LAYERS:
            continue
        keep = FLOOR_LAYER in layers or kind in ENVIRONMENT or kind == "PlayerStart"
        if first_floor_room(str(desc.label)):
            keep = True
        if kind == "StaticMeshActor" and (not layers or layers == {"/Game/DataLayers/ToBeFiled.ToBeFiled"}):
            # Include unfiled ground-floor geometry without pulling in the other floors.
            center = (desc.bounds.min.z + desc.bounds.max.z) / 2
            keep = low <= center <= high
        if keep:
            selected.append(desc)
    if not any(FLOOR_LAYER in layer_paths(d) for d in selected):
        raise RuntimeError("The first-floor data layer is empty")
    return selected, (low, high)


def add_room_markers(world):
    folder = Path(unreal.Paths.project_plugins_dir()) / "PFTNavigator" / "Content" / "Data"
    rooms = json.loads((folder / "rooms.json").read_text(encoding="utf-8-sig"))
    bindings = json.loads((folder / "room_bindings.json").read_text(encoding="utf-8-sig"))
    floor_ids = {room["Name"] for room in rooms if room["Floor"] == 1}
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = {a.get_editor_property("room_id") for a in actors.get_all_level_actors() if isinstance(a, unreal.PFTRoomMarker)}
    count = 0
    for binding in bindings:
        if binding["Map"] != SOURCE_MAP or binding["RoomId"] not in floor_ids:
            continue
        if binding["RoomId"] in existing:
            count += 1
            continue
        pos = binding["Location"]
        marker = actors.spawn_actor_from_class(unreal.PFTRoomMarker, unreal.Vector(pos["X"], pos["Y"], pos["Z"]))
        if not marker:
            raise RuntimeError(f"Could not create marker for {binding['RoomId']}")
        marker.set_editor_property("room_id", binding["RoomId"])
        marker.set_actor_label(f"Destination_{binding['RoomId']}")
        count += 1
    return count


def finish_map(world, room_ids):
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    cleared = 0
    for actor in actors:
        if isinstance(actor, unreal.PFTAreaTriggers):
            room_id = room_ids.get(actor.get_actor_label())
            if not room_id:
                raise RuntimeError(f"Missing source room ID for {actor.get_actor_label()}")
            actor.set_editor_property("source_room_id", room_id)
        for component in actor.get_components_by_class(unreal.ActorComponent):
            for prop in ("asset_user_data", "asset_user_data_editor_only"):
                clean_data = []
                for data in component.get_editor_property(prop):
                    if isinstance(data, unreal.DatasmithUserData):
                        metadata = unreal.new_object(unreal.DatasmithUserData, outer=component)
                        metadata.get_editor_property("metadata").update(data.get_editor_property("metadata"))
                        clean_data.append(metadata)
                        cleared += 1
                    else:
                        clean_data.append(data)
                component.set_editor_property(prop, clean_data)
    markers = [a for a in actors if isinstance(a, unreal.PFTRoomMarker)]
    start_marker = next((m for m in markers if m.get_editor_property("room_id") == "PFT_1120"), None)
    if not start_marker:
        raise RuntimeError("The first-floor starting room needs a destination marker")
    for actor in actors:
        if isinstance(actor, unreal.PlayerStart):
            actor.set_actor_location(start_marker.get_actor_location() + unreal.Vector(0, 0, 100), False, True)
    return cleared


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", default=OUTPUT_MAP)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--finish", action="store_true", help="Add destinations in a fresh editor process after generating geometry")
    args = parser.parse_args()
    if "-run=" in unreal.SystemLibrary.get_command_line().lower():
        raise RuntimeError("Use -ExecutePythonScript, not -run=PythonScript; actor duplication needs the full editor")
    if not args.output.startswith("/Game/") or args.output == SOURCE_MAP:
        raise RuntimeError("Output must be a new map under /Game/")
    report_path = Path(unreal.Paths.project_saved_dir()) / "FirstFloorBuild.json"
    if args.finish:
        world = unreal.EditorLoadingAndSavingUtils.load_map(args.output)
        if not world:
            raise RuntimeError("Generate the first-floor geometry before running --finish")
        report = json.loads(report_path.read_text())
        if report["output"] != args.output:
            raise RuntimeError("The generation report belongs to a different output map")
        report["room_markers"] = add_room_markers(world)
        report["cleared_import_templates"] = finish_map(world, report["source_room_ids"])
        if not unreal.EditorLoadingAndSavingUtils.save_map(world, args.output):
            raise RuntimeError("Could not save the room markers; check out the map with p4 edit if it is versioned")
        report["complete"] = True
        report["dry_run"] = False
        report_path.write_text(json.dumps(report, indent=2))
        unreal.log(f"FIRST_FLOOR_BUILD_COMPLETE: {args.output}, {report['selected_actors']} copied actors")
        return
    if not args.dry_run and unreal.EditorAssetLibrary.does_asset_exist(args.output):
        raise RuntimeError("Output already exists. Use --output with a new map name to preserve existing edits")

    settings = unreal.get_default_object(unreal.load_class(None, "/Script/Engine.WorldPartitionEditorPerProjectUserSettings"))
    restore_regions = settings.get_editor_property("bDisableLoadingOfLastLoadedRegions")
    settings.set_editor_property("bDisableLoadingOfLastLoadedRegions", True)
    try:
        source = unreal.EditorLoadingAndSavingUtils.load_map(SOURCE_MAP)
    finally:
        settings.set_editor_property("bDisableLoadingOfLastLoadedRegions", restore_regions)
    if not source:
        raise RuntimeError(f"Could not load {SOURCE_MAP}")
    descs = unreal.WorldPartitionBlueprintLibrary.get_actor_descs()
    selected, band = select_actors(descs)
    report = {
        "source": SOURCE_MAP,
        "output": args.output,
        "source_actors": len(descs),
        "selected_actors": len(selected),
        "floor_height_range": band,
        "classes": dict(collections.Counter(d.native_class.get_name() for d in selected)),
        "layers": dict(collections.Counter("|".join(sorted(layer_paths(d))) or "(none)" for d in selected)),
    }
    if args.dry_run:
        report["dry_run"] = True
        report_path.write_text(json.dumps(report, indent=2))
        unreal.log(f"First floor dry run: {len(selected)} of {len(descs)} actors")
        return

    unreal.log(f"Loading {len(selected)} first-floor actors")
    unreal.WorldPartitionBlueprintLibrary.load_actors([d.guid for d in selected])
    source_actors = [unreal.find_object(None, str(d.actor_path)) for d in selected]
    if any(actor is None for actor in source_actors):
        raise RuntimeError("Some selected actors failed to load; the output has not been saved")
    report["source_room_ids"] = {
        actor.get_actor_label(): actor.get_editor_property("UniqueIDString")
        for actor in source_actors if isinstance(actor, unreal.PFTAreaTriggers)
    }
    folder, name = args.output.rsplit("/", 1)
    target = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.World, unreal.WorldFactory())
    if not target or target.get_world_settings().get_editor_property("world_partition"):
        raise RuntimeError("The output must be a standalone world without World Partition")
    for prop in ("default_game_mode", "world_to_meters", "kill_z", "enable_world_bounds_checks"):
        target.get_world_settings().set_editor_property(prop, source.get_world_settings().get_editor_property(prop))
    copies = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).duplicate_actors(source_actors, target)
    if len(copies) != len(source_actors):
        raise RuntimeError(f"Only {len(copies)} of {len(source_actors)} actors copied; the output has not been saved")
    copied = set(copies)
    for actor in copies:
        parent = actor.get_attach_parent_actor()
        if parent and parent not in copied:
            actor.detach_from_actor(unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD, unreal.DetachmentRule.KEEP_WORLD)
        actor.set_editor_property("data_layer_assets", [])
        actor.set_editor_property("hlod_layer", None)
    if not unreal.EditorLoadingAndSavingUtils.save_map(target, args.output):
        raise RuntimeError("Could not save the first-floor map")

    report["copied_actors"] = len(copies)
    report["complete"] = False
    report["dry_run"] = False
    report_path.write_text(json.dumps(report, indent=2))
    unreal.log(f"FIRST_FLOOR_GEOMETRY_COMPLETE: {args.output}; run again with --finish in a fresh editor process")


if __name__ == "__main__":
    main()
