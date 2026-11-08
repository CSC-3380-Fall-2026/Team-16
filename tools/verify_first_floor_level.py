"""Check that the lightweight map opens independently of the full twin."""

import collections
from pathlib import Path
import sys

import unreal

# Unreal commandlets do not add the script folder to Python's import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_first_floor_level import read_build_report, write_report


MAP = "/Game/Level/PFTFirstFloor"
SOURCE = "/Game/Level/PatrickFTaylorHall"


def find_dependencies(registry):
    """Return the map's reachable packages and reference paths, handling dependency cycles."""
    options = unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=True,
        include_soft_package_references=True,
        include_searchable_names=False,
        include_hard_management_references=False,
        include_soft_management_references=False,
    )
    visited = set()
    pending = [MAP]
    paths = {MAP: [MAP]}
    while pending:
        package = pending.pop()
        if package in visited:
            continue
        visited.add(package)
        if package.startswith(("/Script/", "/Memory/")):
            continue
        for dependency in registry.get_dependencies(package, options):
            dependency = str(dependency)
            if dependency not in paths:
                paths[dependency] = paths[package] + [dependency]
            if dependency not in visited:
                pending.append(dependency)
    return visited, paths


def check_room_ids(rooms, report_path):
    """Return room-ID errors, comparing source IDs when a saved build report is available."""
    errors = []
    if any(not actor.get_editor_property("source_room_id") for actor in rooms):
        errors.append("Copied room volumes are missing source IDs")
    if not report_path.exists():
        return errors
    room_ids = read_build_report(report_path).get("SourceRoomIds", {})
    for actor in rooms:
        source_id = actor.get_editor_property("source_room_id")
        if room_ids and source_id != room_ids.get(actor.get_actor_label()):
            errors.append("Copied room IDs differ from the source twin")
            break
    return errors


def check_map(world, classes, forbidden, starts, markers):
    """Return errors for partitioning, missing destinations, and unwanted source-world content."""
    errors = []
    if world.get_world_settings().get_editor_property("world_partition"):
        errors.append("Output still uses World Partition")
    if forbidden:
        errors.append("Output references the full twin")
    if not starts:
        errors.append("Missing PlayerStart")
    if not markers:
        errors.append("Missing room destination markers")
    if classes["WorldPartitionHLOD"] or classes["LandscapeStreamingProxy"]:
        errors.append("Output includes full-building HLODs or campus terrain")
    if unreal.find_object(None, SOURCE + ".PatrickFTaylorHall"):
        errors.append("Opening the output loaded the original world")
    return errors


def verification_report(world, actors, visited, paths):
    """Return counts, source dependency paths, and validation errors for the loaded map."""
    classes = collections.Counter(actor.get_class().get_name() for actor in actors)
    starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    markers = [actor for actor in actors if isinstance(actor, unreal.PFTRoomMarker)]
    rooms = [actor for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)]
    forbidden = [
        package for package in visited
        if package == SOURCE
        or package.startswith("/Game/__ExternalActors__/Level/PatrickFTaylorHall/")
    ]
    errors = check_map(world, classes, forbidden, starts, markers)
    report_path = Path(unreal.Paths.project_saved_dir(), "FirstFloorBuild.json")
    errors.extend(check_room_ids(rooms, report_path))
    return {
        "Name": "FirstFloorVerification",
        "Map": MAP,
        "Actors": len(actors),
        "Classes": dict(classes),
        "DependencyPackages": len(visited),
        "FullTwinDependencies": forbidden,
        "FullTwinDependencyPaths": [paths[package] for package in forbidden],
        "PlayerStarts": [list(actor.get_actor_location().to_tuple()) for actor in starts],
        "RoomMarkers": [actor.get_editor_property("room_id") for actor in markers],
        "PreservedRoomIds": len(rooms),
        "Errors": errors,
    }


def main():
    """Load the first-floor map, write a verification report, and raise on failed checks."""
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    if not world:
        raise RuntimeError(f"Could not load {MAP}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    visited, paths = find_dependencies(registry)
    report = verification_report(world, actors, visited, paths)
    write_report(Path(unreal.Paths.project_saved_dir(), "FirstFloorVerification.json"), report)
    if report["Errors"]:
        raise RuntimeError("; ".join(report["Errors"]))
    unreal.log(f"FIRST_FLOOR_VERIFIED: {len(actors)} actors; no full-twin dependencies")


if __name__ == "__main__":
    main()
