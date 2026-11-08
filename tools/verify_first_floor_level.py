"""Check that the lightweight map opens independently of the full twin."""

import collections
import json
from pathlib import Path

import unreal


MAP = "/Game/Level/PFTFirstFloor"
SOURCE = "/Game/Level/PatrickFTaylorHall"


def main():
    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP)
    if not world:
        raise RuntimeError(f"Could not load {MAP}")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    classes = collections.Counter(actor.get_class().get_name() for actor in actors)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
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
    forbidden = []
    while pending:
        package = pending.pop()
        if package in visited:
            continue
        visited.add(package)
        if package == SOURCE or package.startswith("/Game/__ExternalActors__/Level/PatrickFTaylorHall/"):
            forbidden.append(package)
        if package.startswith(("/Script/", "/Memory/")):
            continue
        for dependency in registry.get_dependencies(package, options):
            dep = str(dependency)
            if dep not in paths:
                paths[dep] = paths[package] + [dep]
            if dep not in visited:
                pending.append(dep)

    starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
    markers = [actor for actor in actors if isinstance(actor, unreal.PFTRoomMarker)]
    errors = []
    if world.get_world_settings().get_editor_property("world_partition"):
        errors.append("Output still uses World Partition")
    if forbidden:
        errors.append("Output references the full twin")
    if not starts:
        errors.append("Missing PlayerStart")
    if not markers:
        errors.append("Missing room destination markers")
    rooms = [actor for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)]
    if any(not actor.get_editor_property("source_room_id") for actor in rooms):
        errors.append("Copied room volumes are missing source IDs")
    build_report = Path(unreal.Paths.project_saved_dir(), "FirstFloorBuild.json")
    if build_report.exists():
        room_ids = json.loads(build_report.read_text()).get("source_room_ids", {})
        if room_ids and any(actor.get_editor_property("source_room_id") != room_ids.get(actor.get_actor_label()) for actor in rooms):
            errors.append("Copied room IDs differ from the source twin")
    if classes["WorldPartitionHLOD"] or classes["LandscapeStreamingProxy"]:
        errors.append("Output includes full-building HLODs or campus terrain")
    if unreal.find_object(None, SOURCE + ".PatrickFTaylorHall"):
        errors.append("Opening the output loaded the original world")
    report = {
        "map": MAP,
        "actors": len(actors),
        "classes": dict(classes),
        "dependency_packages": len(visited),
        "full_twin_dependencies": forbidden,
        "full_twin_dependency_paths": [paths[package] for package in forbidden],
        "player_starts": [list(actor.get_actor_location().to_tuple()) for actor in starts],
        "room_markers": [actor.get_editor_property("room_id") for actor in markers],
        "preserved_room_ids": len(rooms),
        "errors": errors,
    }
    Path(unreal.Paths.project_saved_dir(), "FirstFloorVerification.json").write_text(json.dumps(report, indent=2))
    if errors:
        raise RuntimeError("; ".join(errors))
    unreal.log(f"FIRST_FLOOR_VERIFIED: {len(actors)} actors; no full-twin dependencies")


if __name__ == "__main__":
    main()
