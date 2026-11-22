# directory_search.py
# Searches the directory data (rooms, classes, professors, departments) from the
# terminal. Handy for checking the data or finding a room without opening Unreal.
# It uses the same rules as the panel search: not case sensitive, and IDs match
# even if you skip the spaces, dashes or underscores (so "pft1200" finds PFT 1200).
#
# Run from the root of the repo:
#   python3 tools/directory_search.py 1200
#   python3 tools/directory_search.py "data structures"

import sys

from validate_data import load_file, DATA_FOLDER


def squash(text):
    """Lowercases text and takes out spaces, dashes and underscores.
    Params: text - the text to clean up
    Returns: the squashed text, ex: "PFT_1200" -> "pft1200"
    """
    return text.lower().replace(" ", "").replace("-", "").replace("_", "")


def matches(query, *values):
    """Checks if the query shows up in any of the values.
    Params: query - what the user typed, values - the fields to look in
    Returns: True if any field contains the query (case and spacing don't matter)
    """
    typed = squash(query)
    return bool(typed) and any(typed in squash(value or "") for value in values)


def search(query, folder=DATA_FOLDER):
    """Searches every data file for the query.
    Params: query - what to look for, folder - the data folder (tests pass their own)
    Returns: a list of (kind, label, room ID) results, rooms first, then classes,
             professors and departments. Empty if nothing matched or a file is missing.
    """
    query = query.strip()
    rooms = load_file(folder, "rooms.json")
    courses = load_file(folder, "courses.json")
    faculty = load_file(folder, "faculty.json")
    departments = load_file(folder, "departments.json")
    if not query or None in (rooms, courses, faculty, departments):
        return []

    results = []
    for room in rooms:
        if matches(query, room["Name"], room["RoomNumber"], room["DisplayName"]):
            results.append(("Room", room["DisplayName"], room["Name"]))
    for course in courses:
        if matches(query, course["Name"], course["CourseCode"], course["CourseTitle"], course.get("Instructor")):
            label = f"{course['CourseCode']} - {course['CourseTitle']}"
            # Only some classes have times so far, so only show them when we have them
            if course.get("StartTime"):
                label += f" ({course['MeetingDays']} {course['StartTime']}"
                label += f"-{course['EndTime']})" if course.get("EndTime") else ")"
            results.append(("Class", label, course["RoomId"]))
    for professor in faculty:
        if matches(query, professor["FullName"]):
            results.append(("Professor", f"{professor['FullName']} - Office", professor["OfficeRoomId"]))
    for department in departments:
        if matches(query, department["Name"], department["DisplayName"]):
            results.append(("Department", f"{department['DisplayName']} - Department Office", department["OfficeRoomId"]))
    return results


def main():
    """Runs a search with whatever was typed after the script name and prints the results."""
    query = " ".join(sys.argv[1:])
    if not query.strip():
        print('Usage: python3 tools/directory_search.py <search text>, ex: "csc 3102"')
        return 1

    results = search(query)
    if not results:
        print(f'Nothing found for "{query}"')
        return 0
    for kind, label, room_id in results:
        print(f"{kind:<11}{label}  ->  {room_id}")
    print(f"{len(results)} result(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
