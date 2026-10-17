# validate_data.py
# Checks the JSON files in data/ before they get imported into Unreal.
# Makes sure every row has the fields it needs, there are no duplicate IDs,
# and that any room or department a row points to actually exists.
#
# Run from the root of the repo:
#   python tools/validate_data.py

import json
import os
import sys

DATA_FOLDER = os.path.join(os.path.dirname(__file__), "..", "data")

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
                valid = type(value) is int and value > 0
            else:
                valid = isinstance(value, str) and value == value.strip()
            if not valid:
                errors.append(f"{file_name}: {row.get('Name')} has invalid {field}")
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
        if not type_errors:
            errors += check_duplicate_ids(file_name, rows)
        if file_name == "rooms.json" and not type_errors:
            numbers = [row.get("RoomNumber") for row in rows]
            if len(numbers) != len(set(numbers)) and len(get_ids(rows)) == len(rows):
                errors.append("rooms.json: a RoomNumber is used more than once")

    # References need every file loaded, so skip them if one failed to load
    if len(data) == len(REQUIRED_FIELDS) and not errors:
        errors += check_references(data)

    return errors


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
