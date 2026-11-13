"""Place room markers and their preview cameras in the first-floor map."""

from collections import Counter
import json
import math
from pathlib import Path

import unreal


CAMERA_RADIUS = 35
CAMERA_HEIGHT = 155
MARKER_RADIUS = 20
MARKER_HEIGHT = 115
SIGHTLINE_RADIUS = 12
MIN_VIEW_DISTANCE = 70
MAX_VIEW_DISTANCE = 550
ROOM_SAMPLES = (0, -0.25, 0.25, -0.5, 0.5, -0.75, 0.75)
COMMONS_VOLUME = "HallwayC1008"
# The room contains gas cylinders. Preview its door from the adjacent hall.
ROOM_1140_ENTRANCE = (7720, -5607)
ROOM_PREVIEW_YAWS = {
    "PFT_1245": 136,
    "PFT_1278": -124,
    "PFT_1280": 0,
    "PFT_COMMONS": 90,
}


def first_floor_rooms():
    """Return the directory entries assigned to the first floor."""
    path = Path(unreal.Paths.project_plugins_dir(), "PFTNavigator", "Content", "Data", "rooms.json")
    rooms = json.loads(path.read_text(encoding="utf-8-sig"))
    return [room for room in rooms if room["Floor"] == 1]


def room_sources(actors):
    """Match first-floor directory entries to their room or atrium volume."""
    volumes = [actor for actor in actors if isinstance(actor, unreal.PFTAreaTriggers)]
    counts = Counter(actor.get_actor_label() for actor in volumes)
    sources = {actor.get_actor_label(): actor for actor in volumes}
    matches = {}
    for room in first_floor_rooms():
        number = room["RoomNumber"]
        if room["Name"] == "PFT_COMMONS":
            label = COMMONS_VOLUME
        elif number.isdigit():
            label = f"Room{number}"
        else:
            continue
        if counts[label] != 1:
            raise RuntimeError(f"Expected one volume named {label}, found {counts[label]}")
        if room["Name"] == "PFT_1140":
            label = "HallwayC1001"
            if counts[label] != 1:
                raise RuntimeError(f"Expected one volume named {label}, found {counts[label]}")
        matches[room["Name"]] = sources[label]
    return matches


def floor_point(world, x, y, bottom):
    """Return a nearby horizontal walking surface below the requested point."""
    start = unreal.Vector(x, y, bottom + 70)
    end = unreal.Vector(x, y, bottom - 80)
    hit = unreal.SystemLibrary.line_trace_single_by_profile(
        world, start, end, "Pawn", False, [], unreal.DrawDebugTrace.NONE, False,
    )
    if hit is None:
        return None
    result = hit.to_dict()
    return result["impact_point"] if not result["initial_overlap"] and result["impact_normal"].z >= 0.65 else None


def inside_room(brush, point):
    """Check the actual brush rather than its axis-aligned bounds."""
    distance, _ = brush.get_closest_point_on_collision(point)
    return 0 <= distance < 0.1


def clear_view(world, start, end, radius):
    """Check a sphere along a view segment using the exploration pawn profile."""
    hit = unreal.SystemLibrary.sphere_trace_single_by_profile(
        world, start, end, radius, "Pawn", False, [], unreal.DrawDebugTrace.NONE, False,
    )
    return hit is None


def sample_room_floor(world, room, focus):
    """Sample collision-backed floor points inside the room volume, nearest the center first."""
    center, extent = room.get_actor_bounds(False)
    x, y = focus or (center.x, center.y)
    width, depth = (400, 400) if focus else (extent.x, extent.y)
    brush = room.get_component_by_class(unreal.BrushComponent)
    positions = []
    for fx in ROOM_SAMPLES:
        for fy in ROOM_SAMPLES:
            floor = floor_point(world, x + width * fx, y + depth * fy, center.z - extent.z)
            if floor is not None and inside_room(brush, floor + unreal.Vector(0, 0, MARKER_HEIGHT)):
                positions.append(floor)
    return sorted(positions, key=lambda position: math.hypot(position.x - x, position.y - y))


def framed_marker(world, brush, camera, yaw):
    """Place a marker in the preferred direction if the room and sightline allow it."""
    angle = math.radians(yaw)
    direction = unreal.Vector(math.cos(angle), math.sin(angle), -0.14)
    for distance in (300, 250, 200, 150, 100, 85, 70):
        marker = camera + direction * distance
        if (inside_room(brush, marker)
                and clear_view(world, marker, marker + unreal.Vector(0, 0, 1), MARKER_RADIUS)
                and clear_view(world, camera, marker, SIGHTLINE_RADIUS)):
            return marker
    return None


def preferred_room_view(world, brush, camera, cameras, yaw):
    """Try the default camera first, then the closest clear alternatives."""
    nearby = sorted(cameras, key=lambda position: math.dist(position.to_tuple(), camera.to_tuple()))
    for position in nearby:
        marker = framed_marker(world, brush, position, yaw)
        if marker is not None:
            return marker, position
    return None


def find_room_view(world, room, focus=None, preferred_yaw=None):
    """Return a marker and an eye-level camera with a clear view inside the room."""
    brush = room.get_component_by_class(unreal.BrushComponent)
    floor = sample_room_floor(world, room, focus)
    up = unreal.Vector(0, 0, 1)
    cameras = [point + up * CAMERA_HEIGHT for point in floor]
    cameras = [point for point in cameras if inside_room(brush, point)
               and clear_view(world, point, point + up, CAMERA_RADIUS)]
    for point in floor:
        if focus and math.hypot(point.x - focus[0], point.y - focus[1]) > 110:
            break
        marker = point + up * MARKER_HEIGHT
        if not clear_view(world, marker, marker + up, MARKER_RADIUS):
            continue
        nearby = [(math.dist(marker.to_tuple(), camera.to_tuple()), camera) for camera in cameras]
        nearby = sorted(nearby, key=lambda candidate: abs(candidate[0] - 300))
        for distance, camera in nearby:
            if not (MIN_VIEW_DISTANCE <= distance <= MAX_VIEW_DISTANCE
                    and clear_view(world, camera, marker, SIGHTLINE_RADIUS)):
                continue
            if preferred_yaw is not None:
                preferred = preferred_room_view(world, brush, camera, cameras, preferred_yaw)
                return preferred or (marker, camera)
            return marker, camera
    return None


def check_room_markers(world, actors, markers):
    """Return errors for missing markers, invalid placement, or blocked previews."""
    sources = room_sources(actors)
    counts = Counter(marker.get_editor_property("room_id") for marker in markers)
    errors = [f"Expected one preview marker for {room_id}, found {counts[room_id]}"
              for room_id in sources if counts[room_id] != 1]
    for marker in markers:
        room_id = marker.get_editor_property("room_id")
        if room_id not in sources:
            continue
        location = marker.get_actor_location()
        camera = location + marker.get_editor_property("view_offset")
        distance = math.dist(location.to_tuple(), camera.to_tuple())
        brush = sources[room_id].get_component_by_class(unreal.BrushComponent)
        if not (MIN_VIEW_DISTANCE <= distance <= MAX_VIEW_DISTANCE
                and inside_room(brush, location) and inside_room(brush, camera)):
            errors.append(f"The {room_id} preview is outside its room or has an invalid distance")
            continue
        up = unreal.Vector(0, 0, 1)
        if not (clear_view(world, camera, camera + up, CAMERA_RADIUS)
                and clear_view(world, location, location + up, MARKER_RADIUS)
                and clear_view(world, camera, location, SIGHTLINE_RADIUS)):
            errors.append(f"The {room_id} preview camera or its sightline is blocked")
    return errors


def add_room_markers(world):
    """Create or update destination markers after verifying every camera position."""
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    views = {}
    for room_id, room in room_sources(actors).items():
        focus = ROOM_1140_ENTRANCE if room_id == "PFT_1140" else None
        views[room_id] = find_room_view(world, room, focus, ROOM_PREVIEW_YAWS.get(room_id))
    unresolved = [room_id for room_id, view in views.items() if view is None]
    if unresolved:
        raise RuntimeError(f"No clear first-floor preview for {', '.join(unresolved)}")
    existing = {}
    for actor in actors:
        if not isinstance(actor, unreal.PFTRoomMarker):
            continue
        room_id = actor.get_editor_property("room_id")
        if room_id in existing:
            raise RuntimeError(f"Multiple first-floor markers for {room_id}")
        existing[room_id] = actor
    for room_id, (location, camera) in views.items():
        marker = existing.get(room_id)
        if marker is None:
            marker = subsystem.spawn_actor_from_class(unreal.PFTRoomMarker, location)
            if not marker:
                raise RuntimeError(f"Could not create marker for {room_id}")
            marker.set_editor_property("room_id", room_id)
            marker.set_actor_label(f"Destination_{room_id}")
        marker.set_actor_location(location, False, True)
        marker.set_editor_property("view_offset", camera - location)
    return len(views)
