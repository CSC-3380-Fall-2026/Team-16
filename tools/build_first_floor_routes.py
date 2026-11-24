"""Bake the first-floor walking graph from the demo map's pawn collision."""

from collections import deque
import argparse
import json
import math
from pathlib import Path

import unreal


MAP = "/Game/Level/PFTFirstFloorComplete"
STEP = 60
WALKWAY_YAW = 15.45
RADIUS = 42
HALF_HEIGHT = 92
EDGES = ((1, 0), (0, 1), (-1, 0), (0, -1))
COMMONS = (14900, -5160)
LANDMARK_ACTORS = {"SM_Zone1200SouthWest3": "Zone 1200 sign"}
SEATING_MESH_PREFIX = "Floors_Floor_CPT-3_-_CARPET_TILE_-_24__x_24_"
SEATING_MATERIAL = "MI_GrayCarpet_Dark_WS"


def hallway_distances(links, hallway):
    distances = dict.fromkeys(hallway, 0)
    pending = deque(hallway)
    while pending:
        x, y = pending.popleft()
        for index, (dx, dy) in enumerate(EDGES):
            neighbor = x + dx, y + dy
            if links[x, y] & (1 << index) and neighbor not in distances:
                distances[neighbor] = distances[x, y] + 1
                pending.append(neighbor)
    return distances


def seating_cells(world, hallway, step):
    """Find the seating carpet that interrupts the gray hallway floor."""
    samples = {}

    def on_carpet(x, y):
        key = x, y
        if key in samples:
            return samples[key]
        hit = unreal.SystemLibrary.line_trace_single_by_profile(
            world, unreal.Vector(x, y, 65), unreal.Vector(x, y, -22),
            "Pawn", True, [], unreal.DrawDebugTrace.NONE, False,
        )
        samples[key] = False
        if hit is None:
            return False
        result = hit.to_dict()
        component = result["hit_component"]
        if not isinstance(component, unreal.StaticMeshComponent) or result["face_index"] < 0:
            return False
        mesh = component.get_editor_property("static_mesh")
        if mesh is None or not mesh.get_name().startswith(SEATING_MESH_PREFIX):
            return False
        material, _ = component.get_material_from_collision_face_index(result["face_index"])
        samples[key] = material is not None and material.get_name() == SEATING_MATERIAL
        return samples[key]

    # Check the cell edges as well as its center to leave room for the arrow arms.
    offsets = (0, -step / 2, step / 2)
    return [cell for cell in sorted(hallway)
            if any(on_carpet(cell[0] * step + dx, cell[1] * step + dy) for dx in offsets for dy in offsets)]


def bake(map_path, output):
    """Bake collision data for the selected map and write it to output."""
    world = unreal.EditorLoadingAndSavingUtils.load_map(map_path)
    if world is None:
        raise RuntimeError(f"Could not open {map_path}")
    unreal.SystemLibrary.execute_console_command(world, "Editor.AsyncStaticMeshCompilationFinishAll")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    markers = [actor for actor in actors if isinstance(actor, unreal.PFTRoomMarker)]
    rooms = {actor.get_actor_label(): actor for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)}
    spawn = next(actor.get_actor_location() for actor in actors if isinstance(actor, unreal.PlayerStart))
    up = unreal.Vector(0, 0, 1)
    mode = world.get_world_settings().get_editor_property("default_game_mode")
    if not mode:
        settings = unreal.get_default_object(unreal.GameMapsSettings)
        mode = unreal.load_class(None, str(settings.get_editor_property("global_default_game_mode").get_asset_path_name()))
    pawn = unreal.get_default_object(mode).get_editor_property("default_pawn_class")
    capsule = unreal.get_default_object(pawn).get_component_by_class(unreal.CapsuleComponent)
    door_radius = capsule.get_scaled_capsule_radius()
    door_half_height = capsule.get_scaled_capsule_half_height()

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

    def clear(start, end, radius=RADIUS, half_height=HALF_HEIGHT):
        return unreal.SystemLibrary.capsule_trace_single_by_profile(
            world, start, end, radius, half_height, "Pawn", False, [], unreal.DrawDebugTrace.NONE, False,
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

    def contains(brush, point):
        distance, _ = brush.get_closest_point_on_collision(point)
        return 0 <= distance < 0.1

    def volume_cells(volume):
        center, extent = volume.get_actor_bounds(False)
        brush = volume.get_component_by_class(unreal.BrushComponent)
        for x in range(math.ceil((center.x - extent.x) / STEP), math.floor((center.x + extent.x) / STEP) + 1):
            for y in range(math.ceil((center.y - extent.y) / STEP), math.floor((center.y + extent.y) / STEP) + 1):
                node = x, y
                if node in links and contains(brush, positions[node]):
                    yield node

    hallway = {node for label, room in rooms.items() if label.startswith("Hallway") for node in volume_cells(room)}
    seating = seating_cells(world, hallway, STEP)
    from_hallway = hallway_distances(links, hallway)
    doors = []
    for actor in actors:
        if "door" not in actor.get_actor_label().lower():
            continue
        center, extent = actor.get_actor_bounds(False)
        if extent.z >= 75 and -22 <= center.z - extent.z <= 65 and max(extent.x, extent.y) <= 180:
            angle = math.radians(actor.get_actor_rotation().yaw)
            tangent = unreal.Vector(math.cos(angle), math.sin(angle), 0)
            doors.append((center, tangent))

    def door_stop(door, tangent, marker, brush):
        normal = unreal.Vector(-tangent.y, tangent.x, 0)
        if (marker.x - door.x) * normal.x + (marker.y - door.y) * normal.y < 0:
            normal = normal * -1
        x, y = round(door.x / STEP), round(door.y / STEP)
        anchors = []
        for nx in range(x - 3, x + 4):
            for ny in range(y - 3, y + 4):
                cell = nx, ny
                if cell in from_hallway:
                    point = positions[cell]
                    if (point.x - door.x) * normal.x + (point.y - door.y) * normal.y < -5:
                        anchors.append(cell)
        # Prefer a short approach on the grid, then use the actual pawn capsule through the doorway.
        anchors.sort(key=lambda cell: (from_hallway[cell], math.hypot(cell[0] * STEP - door.x, cell[1] * STEP - door.y), cell))
        # Leave the 65 cm arrival radius on the inside of the doorway.
        for depth in (100, 85, 120):
            for shift in (0, -10, 10, -20, 20, -30, 30):
                point = door + normal * depth + tangent * shift
                stop = floor(point.x, point.y)
                if stop is None or not contains(brush, stop + up * door_half_height):
                    continue
                for cell in anchors:
                    start = positions[cell] + up * (door_half_height - HALF_HEIGHT)
                    length = math.hypot(stop.x - start.x, stop.y - start.y)
                    if length > STEP * 4 or not clear(start, stop + up * (door_half_height + 4), door_radius, door_half_height):
                        continue
                    start_floor = positions[cell].z - HALF_HEIGHT - 4
                    steps = math.ceil(length / (STEP / 2))
                    for i in range(1, steps):
                        alpha = i / steps
                        sample = start + (stop - start) * alpha
                        surface = floor(sample.x, sample.y)
                        if surface is None or abs(surface.z - (start_floor + (stop.z - start_floor) * alpha)) > 7:
                            break
                    else:
                        return cell, stop
        return None

    destinations = {}
    stops = {}
    for marker in sorted(markers, key=lambda actor: actor.get_editor_property("room_id")):
        room_id = marker.get_editor_property("room_id")
        if room_id == "PFT_COMMONS":
            cell = min(volume_cells(rooms["HallwayC1008"]),
                       key=lambda node: math.dist((node[0] * STEP, node[1] * STEP), COMMONS), default=None)
            if cell is not None:
                destinations[room_id] = cell
            continue
        room = rooms[f"Room{room_id.removeprefix('PFT_')}"]
        brush = room.get_component_by_class(unreal.BrushComponent)
        center, extent = room.get_actor_bounds(False)
        location = center if room_id == "PFT_1140" else marker.get_actor_location()
        candidates = []
        for door, tangent in doors:
            if abs(door.x - center.x) <= extent.x + 180 and abs(door.y - center.y) <= extent.y + 180:
                result = door_stop(door, tangent, location, brush)
                if result is not None:
                    cell, stop = result
                    candidates.append((from_hallway[cell], math.hypot(door.x - location.x, door.y - location.y), cell, stop))
        if candidates:
            _, _, cell, stop = min(candidates, key=lambda item: item[:3])
            destinations[room_id] = cell
            stops[room_id] = [round(value, 2) for value in stop.to_tuple()]
        elif room_id == "PFT_1140":
            target = marker.get_actor_location().to_tuple()[:2]
            cell = min(volume_cells(rooms["HallwayC1001"]),
                       key=lambda node: math.dist((node[0] * STEP, node[1] * STEP), target), default=None)
            if cell is not None and math.dist((cell[0] * STEP, cell[1] * STEP), target) <= 180:
                destinations[room_id] = cell

    if "PFT_COMMONS" not in destinations or len(destinations) < 2:
        raise RuntimeError(f"Too few rooms connect to the walking graph: {sorted(destinations)}")
    landmarks = {}
    for actor in actors:
        label = LANDMARK_ACTORS.get(actor.get_actor_label())
        if label:
            center, _ = actor.get_actor_bounds(False)
            if 0 <= center.z <= 350:
                landmarks[label] = [round(center.x), round(center.y)]
    nodes = (json.dumps([x, y, round(positions[x, y].z - HALF_HEIGHT - 4, 2), links[x, y]], separators=(",", ":"))
             for x, y in sorted(links))
    graph = {"Map": map_path, "Step": STEP, "WalkwayYaw": WALKWAY_YAW,
             "Destinations": destinations, "RoomStops": stops, "Landmarks": landmarks}
    if seating:
        graph["SeatingCells"] = seating
    text = json.dumps(graph, separators=(",", ":"))
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
