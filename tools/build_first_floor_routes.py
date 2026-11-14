"""Bake the first-floor walking graph from the demo map's pawn collision."""

from collections import deque
import argparse
import json
import math
from pathlib import Path

import unreal


MAP = "/Game/Level/PFTFirstFloorComplete"
STEP = 60
RADIUS = 42
HALF_HEIGHT = 92
EDGES = ((1, 0), (0, 1), (-1, 0), (0, -1))
COMMONS = (14900, -5160)
HALLWAY_DOORS = {
    "PFT_1218": ("HallwayC1003", "SM_LongWindowDoor_2"),
}


def bake(map_path, output):
    """Bake collision data for the selected map and write it to output."""
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if world is None:
        raise RuntimeError(f"Could not open {map_path}")
    unreal.SystemLibrary.execute_console_command(world, "Editor.AsyncStaticMeshCompilationFinishAll")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    markers = [actor for actor in actors if isinstance(actor, unreal.PFTRoomMarker)]
    rooms = {actor.get_actor_label(): actor for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)}
    actors_by_label = {actor.get_actor_label(): actor for actor in actors}
    spawn = next(actor.get_actor_location() for actor in actors if isinstance(actor, unreal.PlayerStart))
    up = unreal.Vector(0, 0, 1)

    def floor(x, y):
        hit = unreal.SystemLibrary.line_trace_single_by_profile(
            world, unreal.Vector(x, y, 65), unreal.Vector(x, y, -22),
            "Pawn", False, [], unreal.DrawDebugTrace.NONE, False,
        )
        if hit:
            result = hit.to_dict()
            if not result["initial_overlap"] and result["impact_normal"].z >= 0.75:
                return result["impact_point"]
        return None

    def clear(start, end):
        return unreal.SystemLibrary.capsule_trace_single_by_profile(
            world, start, end, RADIUS, HALF_HEIGHT, "Pawn", False, [], unreal.DrawDebugTrace.NONE, False,
        ) is None

    bounds = [room.get_actor_bounds(False) for room in rooms.values()]
    low_x = math.floor(min(center.x - extent.x for center, extent in bounds) / STEP)
    high_x = math.ceil(max(center.x + extent.x for center, extent in bounds) / STEP)
    low_y = math.floor(min(center.y - extent.y for center, extent in bounds) / STEP)
    high_y = math.ceil(max(center.y + extent.y for center, extent in bounds) / STEP)
    positions = {}
    for x in range(low_x, high_x + 1):
        for y in range(low_y, high_y + 1):
            point = floor(x * STEP, y * STEP)
            if point is not None:
                center = point + up * (HALF_HEIGHT + 4)
                if clear(center, center + up):
                    positions[x, y] = center

    start = min(positions, key=lambda point: math.dist(positions[point].to_tuple(), spawn.to_tuple()))
    if not clear(spawn + up * 4, positions[start]):
        raise RuntimeError("The first-floor spawn does not connect to the walking graph")
    queue = deque([start])
    links = {start: 0}
    while queue:
        node = queue.popleft()
        a = positions[node]
        for index, (dx, dy) in enumerate(EDGES):
            other = node[0] + dx, node[1] + dy
            b = positions.get(other)
            if b is None or abs(a.z - b.z) > 16 or links[node] & (1 << index):
                continue
            if floor((a.x + b.x) / 2, (a.y + b.y) / 2) is None or not clear(a, b):
                continue
            if other not in links:
                links[other] = 0
                queue.append(other)
            links[node] |= 1 << index
            links[other] |= 1 << ((index + 2) % len(EDGES))

    destinations = {}
    for marker in sorted(markers, key=lambda actor: actor.get_editor_property("room_id")):
        room_id = marker.get_editor_property("room_id")
        if room_id in HALLWAY_DOORS:
            hallway, door_label = HALLWAY_DOORS[room_id]
            door = actors_by_label[door_label]
            brush = rooms[hallway].get_component_by_class(unreal.BrushComponent)
            target, _ = door.get_actor_bounds(False)
            nearby = sorted(links, key=lambda node: math.dist(positions[node].to_tuple(), target.to_tuple()))
            for node in nearby:
                point = positions[node]
                if math.dist(point.to_tuple(), target.to_tuple()) > 180:
                    break
                distance, _ = brush.get_closest_point_on_collision(point)
                if not 0 <= distance < 0.1:
                    continue
                hit = unreal.SystemLibrary.line_trace_single_by_profile(
                    world, point, target, "Pawn", False, [door], unreal.DrawDebugTrace.NONE, False,
                )
                if hit is None:
                    destinations[room_id] = node
                    break
            if room_id not in destinations:
                raise RuntimeError(f"No walkable hallway point faces the door to {room_id}")
            continue
        room_label = {"PFT_COMMONS": "HallwayC1008", "PFT_1140": "HallwayC1001"}.get(
            room_id, f"Room{room_id.removeprefix('PFT_')}")
        room = rooms[room_label]
        brush = room.get_component_by_class(unreal.BrushComponent)
        center, extent = room.get_actor_bounds(False)
        target = COMMONS if room_id == "PFT_COMMONS" else marker.get_actor_location().to_tuple()[:2]
        candidates = []
        for node in links:
            point = positions[node]
            if abs(point.x - center.x) > extent.x or abs(point.y - center.y) > extent.y:
                continue
            distance, _ = brush.get_closest_point_on_collision(point)
            if 0 <= distance < 0.1:
                if room_id != "PFT_1140" or math.dist((point.x, point.y), target) <= 180:
                    candidates.append(node)
        if candidates:
            def distance_to_target(node):
                return math.dist((node[0] * STEP, node[1] * STEP), target)

            inside = set(candidates)
            # A connected edge across the room boundary marks a walkable entrance.
            entrances = [node for node in candidates if any(
                links[node] & (1 << index) and (node[0] + dx, node[1] + dy) not in inside
                for index, (dx, dy) in enumerate(EDGES)
            )]
            if room_id in {"PFT_COMMONS", "PFT_1140"}:
                entrances = []
            destinations[room_id] = min(entrances or candidates, key=distance_to_target)

    if "PFT_COMMONS" not in destinations or len(destinations) < 2:
        raise RuntimeError(f"Too few rooms connect to the walking graph: {sorted(destinations)}")
    nodes = (json.dumps([x, y, round(positions[x, y].z - HALF_HEIGHT - 4, 2), links[x, y]], separators=(",", ":"))
             for x, y in sorted(links))
    text = json.dumps({"Map": map_path, "Step": STEP, "Destinations": destinations}, separators=(",", ":"))
    output.write_text(text[:-1] + ',"Nodes":[\n' + ",\n".join(nodes) + "\n]}\n", encoding="utf-8")
    unreal.log(f"PFT routes: {len(links)} connected points, {len(destinations)} destinations: {', '.join(destinations)}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", type=Path)
    parser.add_argument("--map", default=MAP, help="First-floor Unreal map package")
    args = parser.parse_args()
    if args.map != MAP and args.output is None:
        parser.error("An explicit output path is required when baking a candidate map.")
    output = args.output or Path(unreal.Paths.project_plugins_dir(), "PFTNavigator", "Content", "Data", "first_floor_routes.json")
    bake(args.map, output)
