"""Check that the lightweight map opens independently of the full twin."""

import collections
from pathlib import Path
import sys

import unreal

# Unreal commandlets do not add the script folder to Python's import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from create_first_floor_level import (
    C1003_TERRAIN, C1003_TERRAIN_LABEL, CAMPUS_ROAD_OVERLAY, DEMO_START_LOCATION, DEMO_START_YAW,
    MAIN_HALLWAY_FLOOR, OUTPUT_MAP, baked_terrain_textures, fixture_draw_limit, local_lights,
    missing_material_assets, read_build_report, write_report,
)
from first_floor_destinations import check_room_markers


MAP = OUTPUT_MAP
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
    if len(starts) != 1:
        errors.append("Expected one first-floor PlayerStart")
    if not markers:
        errors.append("Missing room destination markers")
    if classes["WorldPartitionHLOD"] or classes["LandscapeStreamingProxy"]:
        errors.append("Output includes full-building HLODs or campus terrain")
    if unreal.find_object(None, SOURCE + ".PatrickFTaylorHall"):
        errors.append("Opening the output loaded the original world")
    return errors


def check_entrance_terrain(actors, visited):
    """Return errors for a missing, untextured, or colliding entrance terrain mesh."""
    terrain = [actor for actor in actors if actor.get_actor_label() == C1003_TERRAIN_LABEL]
    if len(terrain) != 1 or C1003_TERRAIN not in visited:
        return ["Missing baked C1003 exterior terrain"]
    if not isinstance(terrain[0], unreal.StaticMeshActor):
        return ["C1003 exterior terrain must be a static mesh"]
    component = terrain[0].static_mesh_component
    mesh = component.get_editor_property("static_mesh")
    if not mesh or mesh.get_path_name().split(".")[0] != C1003_TERRAIN:
        return ["C1003 exterior terrain has the wrong mesh"]
    if (str(component.get_collision_profile_name()) != "NoCollision"
            or component.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION):
        return ["C1003 exterior terrain must leave first-floor collision to the building"]
    if not baked_terrain_textures(component.get_material(0)):
        return ["C1003 exterior terrain is missing its baked textures"]
    return []


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
    start_locations = [list(actor.get_actor_location().to_tuple()) for actor in starts]
    start_yaws = [actor.get_actor_rotation().yaw for actor in starts]
    if len(starts) == 1:
        position_error = sum(abs(actual - expected) for actual, expected in zip(start_locations[0], DEMO_START_LOCATION))
        yaw_error = abs((start_yaws[0] - DEMO_START_YAW + 180) % 360 - 180)
        if position_error > 1 or yaw_error > 1:
            errors.append("PlayerStart must face Zone 1200 from the C1003 hallway")
    fixtures = list(local_lights(actors))
    shadowed_fixtures = sum(light.get_editor_property("cast_shadows") for light in fixtures)
    if shadowed_fixtures:
        errors.append(f"{shadowed_fixtures} local fixture lights still cast shadows")
    fixtures_without_limits = sum(
        not 0 < light.get_editor_property("max_draw_distance") <= fixture_draw_limit(light) + 0.01
        for light in fixtures
    )
    if fixtures_without_limits:
        errors.append(f"{fixtures_without_limits} local fixture lights lack a valid draw distance")
    missing_materials = missing_material_assets()
    if missing_materials:
        errors.append("Missing material dependencies; sync both shared plugins and the project material assets")
    if not any(actor.get_actor_label() == MAIN_HALLWAY_FLOOR for actor in actors):
        errors.append("Missing the original main hallway floor")
    if any(actor.get_actor_label() == CAMPUS_ROAD_OVERLAY for actor in actors):
        errors.append("Output includes the elevated campus road overlay")
    errors.extend(check_entrance_terrain(actors, visited))
    errors.extend(check_room_markers(actors, markers))
    report_path = Path(unreal.Paths.project_saved_dir(), "FirstFloorBuild.json")
    errors.extend(check_room_ids(rooms, report_path))
    return {
        "Name": "FirstFloorVerification",
        "Map": MAP,
        "Actors": len(actors),
        "Classes": dict(classes),
        "DependencyPackages": len(visited),
        "MissingMaterialDependencies": missing_materials,
        "ShadowCastingFixtures": shadowed_fixtures,
        "FixturesWithDrawLimits": len(fixtures) - fixtures_without_limits,
        "FullTwinDependencies": forbidden,
        "FullTwinDependencyPaths": [paths[package] for package in forbidden],
        "PlayerStarts": start_locations,
        "PlayerStartYaws": start_yaws,
        "RoomMarkers": [actor.get_editor_property("room_id") for actor in markers],
        "EntranceTerrain": C1003_TERRAIN if C1003_TERRAIN in visited else None,
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
