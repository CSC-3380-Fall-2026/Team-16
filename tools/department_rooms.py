# department_rooms.py
# Finds the room IDs used by a department


from validate_data import load_file, DATA_FOLDER


def get_department_room_ids(department_id: str) -> list[str]:
    """Returns the room IDs used by the given department.
       Params: department_id: the ID of the department
       Returns: a list of room IDs used by the department
    """

    courses = load_file(DATA_FOLDER, "courses.json")

    if courses is None:
        return []

    room_ids = set()

    for course in courses:
        if course.get("DepartmentId") == department_id:
            room_ids.add(course.get("RoomId"))

    room_ids.discard(None)

    return sorted(room_ids)


if __name__ == "__main__":
    print(get_department_room_ids("DEPT_CSE")) 