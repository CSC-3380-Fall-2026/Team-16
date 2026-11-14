# validate_data.py
# Checks the JSON files in data/ before they get imported into Unreal.
# Makes sure every row has the fields it needs, there are no duplicate IDs,
# and that any room or department a row points to actually exists.
#
# Run from the root of the repo:
#   python tools/validate_data.py

import json
import math
import os
import re
import sys
from collections import deque

DATA_FOLDER = os.path.join(os.path.dirname(__file__), "..", "data")
WALKING_EDGES = ((1, 0), (0, 1), (-1, 0), (0, -1))

# The fields every row in each file needs to have
REQUIRED_FIELDS = {
    "rooms.json": ["Name", "RoomNumber", "DisplayName", "Floor", "RoomType"],
    "faculty.json": ["Name", "FullName", "DepartmentId", "OfficeRoomId"],
    "courses.json": ["Name", "CourseCode", "CourseTitle", "DepartmentId", "RoomId"],
    "departments.json": ["Name", "DisplayName", "OfficeRoomId"],
}


def load_file(folder, file_name):
    """Loads one of the data files.
    Params: folder - the data folder, file_name - the file to load
    Returns: the list of rows, or None if the file is missing or isn't valid JSON
    """
    path = os.path.join(folder, file_name)
    try:
        with open(path, encoding="utf-8-sig") as file:
            return json.load(file)
    except (OSError, UnicodeError, json.JSONDecodeError):
        return None


def get_ids(rows):
    """Gets all the IDs (the "Name" field) from a file's rows.
    Params: rows - the rows from one file
    Returns: a set of the IDs, used to check if a reference exists
    """
    return {row.get("Name") for row in rows}


def check_required_fields(file_name, rows):
    """Makes sure every row has all of its required fields and none are empty.
    Params: file_name - which file the rows are from, rows - the rows to check
    Returns: a list of errors (empty if everything is fine)
    """
    errors = []
    for row in rows:
        row_id = row.get("Name", "(row with no Name)")
        for field in REQUIRED_FIELDS[file_name]:
            value = row.get(field)
            if value is None or value == "" or isinstance(value, str) and not value.strip():
                errors.append(f"{file_name}: {row_id} is missing {field}")
    return errors


def check_field_types(file_name, rows):
    errors = []
    for row in rows:
        for field in REQUIRED_FIELDS[file_name]:
            value = row.get(field)
            if value is None or value == "":
                continue
            if field == "Floor":
                valid = type(value) is int and 1 <= value <= 2147483647
            else:
                valid = isinstance(value, str) and value == value.strip()
            if not valid:
                errors.append(f"{file_name}: {row.get('Name')} has invalid {field}")
    return errors


TIME_PATTERN = r"(?:[01][0-9]|2[0-3]):[0-5][0-9]"


def check_course_schedules(rows):
    """Validate optional instructor, meeting days, and start time as a group.
    EndTime is optional too, but it only makes sense with the rest of the schedule
    and it has to be after StartTime, or the app can't tell if a class is going on right now.
    """
    errors = []
    for row in rows:
        if not {"Instructor", "MeetingDays", "StartTime", "EndTime"} & row.keys():
            continue
        instructor, days, start = (row.get(key) for key in ("Instructor", "MeetingDays", "StartTime"))
        if not isinstance(instructor, str) or not instructor or instructor != instructor.strip():
            errors.append(f"courses.json: {row.get('Name')} has invalid Instructor")
        if not isinstance(days, str) or not days or not re.fullmatch(r"M?T?W?R?F?S?U?", days):
            errors.append(f"courses.json: {row.get('Name')} has invalid MeetingDays")
        start_ok = isinstance(start, str) and re.fullmatch(TIME_PATTERN, start)
        if not start_ok:
            errors.append(f"courses.json: {row.get('Name')} has invalid StartTime")
        if "EndTime" in row:
            end = row["EndTime"]
            # HH:MM strings sort the same way the times do, so a plain compare works here
            if not isinstance(end, str) or not re.fullmatch(TIME_PATTERN, end) or (start_ok and end <= start):
                errors.append(f"courses.json: {row.get('Name')} has invalid EndTime")
    return errors


def check_duplicate_ids(file_name, rows):
    """Makes sure no two rows in a file have the same ID.
    Unreal uses the Name as the row name in a DataTable, so it has to be unique.
    Params: file_name - which file the rows are from, rows - the rows to check
    Returns: a list of errors (empty if everything is fine)
    """
    errors = []
    seen = set()
    for row in rows:
        row_id = row.get("Name")
        if row_id in seen:
            errors.append(f"{file_name}: {row_id} is used more than once")
        seen.add(row_id)
    return errors


def check_references(data):
    """Makes sure every room or department a row points to actually exists.
    Every search ends up at a room, so a bad reference would be a search
    result that leads nowhere.
    Params: data - all the loaded files, ex: data["rooms.json"]
    Returns: a list of errors (empty if everything is fine)
    """
    room_ids = get_ids(data["rooms.json"])
    department_ids = get_ids(data["departments.json"])

    # (file, field that holds the reference, IDs it's allowed to point to)
    references = [
        ("faculty.json", "OfficeRoomId", room_ids),
        ("faculty.json", "DepartmentId", department_ids),
        ("courses.json", "RoomId", room_ids),
        ("courses.json", "DepartmentId", department_ids),
        ("departments.json", "OfficeRoomId", room_ids),
    ]

    errors = []
    for file_name, field, valid_ids in references:
        for row in data[file_name]:
            value = row.get(field)
            # Empty values are already caught by check_required_fields
            if value and value not in valid_ids:
                errors.append(f"{file_name}: {row.get('Name')} points to {field} {value}, which doesn't exist")
    return errors


def validate(folder):
    """Runs all the checks on the data folder.
    Params: folder - the folder with the JSON files
    Returns: a list of every error found (empty if the data is good)
    """
    errors = []
    data = {}

    for file_name in REQUIRED_FIELDS:
        rows = load_file(folder, file_name)
        if rows is None:
            errors.append(f"Could not load {file_name} (missing or not valid JSON)")
            continue
        if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows):
            errors.append(f"{file_name}: expected a list of objects")
            continue
        data[file_name] = rows
        errors += check_required_fields(file_name, rows)
        type_errors = check_field_types(file_name, rows)
        errors += type_errors
        if file_name == "courses.json":
            errors += check_course_schedules(rows)
        if not type_errors:
            errors += check_duplicate_ids(file_name, rows)
        if file_name == "rooms.json" and not type_errors:
            numbers = [row.get("RoomNumber") for row in rows]
            if len(numbers) != len(set(numbers)) and len(get_ids(rows)) == len(rows):
                errors.append("rooms.json: a RoomNumber is used more than once")

    # References need every file loaded, so skip them if one failed to load
    if len(data) == len(REQUIRED_FIELDS) and not errors:
        errors += check_references(data)
        binding_path = os.path.join(folder, "room_bindings.json")
        if os.path.exists(binding_path):
            errors += check_bindings(load_file(folder, "room_bindings.json"), get_ids(data["rooms.json"]))
        route_file = "first_floor_routes.json"
        if os.path.exists(os.path.join(folder, route_file)):
            first_floor_ids = {room["Name"] for room in data["rooms.json"] if room["Floor"] == 1}
            errors += check_walking_graph(load_file(folder, route_file), first_floor_ids)

    return errors


def check_bindings(rows, room_ids):
    if not isinstance(rows, list) or not all(isinstance(row, dict) for row in rows):
        return ["room_bindings.json: expected a list of objects"]
    errors, keys = [], set()
    for row in rows:
        room_id, map_path, location = row.get("RoomId"), row.get("Map"), row.get("Location")
        if not isinstance(room_id, str) or room_id not in room_ids or not isinstance(map_path, str) or not map_path.startswith("/Game/"):
            errors.append("room_bindings.json: invalid room or map")
            continue
        key = (room_id, map_path)
        if key in keys:
            errors.append(f"room_bindings.json: duplicate binding for {room_id}")
        keys.add(key)
        if not isinstance(location, dict) or not all(type(location.get(axis)) in (int, float) and math.isfinite(location[axis]) for axis in ("X", "Y", "Z")):
            errors.append(f"room_bindings.json: invalid location for {room_id}")
    return errors


def is_grid_cell(values):
    """Check the two integral grid coordinates accepted by the Navigator."""
    return all(type(value) is int and abs(value) <= 10000 for value in values)


def check_walking_graph(graph, room_ids):
    """Check that the generated graph connects real first-floor destinations."""
    prefix = "first_floor_routes.json:"
    if not isinstance(graph, dict):
        return [f"{prefix} expected a walking graph object"]
    map_path, step = graph.get("Map"), graph.get("Step")
    if not isinstance(map_path, str) or not re.fullmatch(r"/Game/(?:[A-Za-z0-9_]+/)*[A-Za-z0-9_]+", map_path):
        return [f"{prefix} invalid Map package path"]
    if type(step) is not int or not 20 <= step <= 100:
        return [f"{prefix} Step must be an integer from 20 to 100"]

    rows, destinations = graph.get("Nodes"), graph.get("Destinations")
    if not isinstance(rows, list) or not rows:
        return [f"{prefix} Nodes must be a nonempty array"]
    if not isinstance(destinations, dict) or len(destinations) < 2 or "PFT_COMMONS" not in destinations:
        return [f"{prefix} expected the Commons and at least one other destination"]

    nodes = {}
    for index, row in enumerate(rows, start=1):
        if not isinstance(row, list) or len(row) != 4 or not is_grid_cell(row[:2]):
            return [f"{prefix} invalid grid coordinates in node {index}"]
        x, y, height, links = row
        if type(height) not in (int, float) or not -1e308 <= height <= 1e308:
            return [f"{prefix} invalid floor height at {(x, y)}"]
        if type(links) is not int or not 0 <= links <= 15:
            return [f"{prefix} invalid edge mask at {(x, y)}"]
        if (x, y) in nodes:
            return [f"{prefix} duplicate node {(x, y)}"]
        nodes[x, y] = links

    for room, cell in destinations.items():
        if room not in room_ids:
            return [f"{prefix} {room} is not a first-floor directory room"]
        if not isinstance(cell, list) or len(cell) != 2 or not is_grid_cell(cell) or tuple(cell) not in nodes:
            return [f"{prefix} {room} does not point to a walking node"]

    start = tuple(destinations["PFT_COMMONS"])
    visited = {start}
    pending = deque([start])
    while pending:
        x, y = pending.popleft()
        for index, (dx, dy) in enumerate(WALKING_EDGES):
            if not nodes[x, y] & (1 << index):
                continue
            neighbor = (x + dx, y + dy)
            reverse = 1 << ((index + 2) % len(WALKING_EDGES))
            if not nodes.get(neighbor, 0) & reverse:
                return [f"{prefix} edge from {(x, y)} to {neighbor} has no return edge"]
            if neighbor not in visited:
                visited.add(neighbor)
                pending.append(neighbor)
    for room, cell in destinations.items():
        if tuple(cell) not in visited:
            return [f"{prefix} {room} is disconnected from the Commons"]
    if len(visited) != len(nodes):
        return [f"{prefix} {len(nodes) - len(visited)} walking nodes are disconnected from the Commons"]
    return []


def main():
    folder = DATA_FOLDER
    if len(sys.argv) > 1:
        folder = sys.argv[1]

    errors = validate(folder)

    if errors:
        for error in errors:
            print("ERROR: " + error)
        print(f"{len(errors)} problem(s) found")
        # Exit with 1 so the GitHub check shows as failed
        sys.exit(1)

    print("All data files look good!")


if __name__ == "__main__":
    main()
