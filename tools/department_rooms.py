# department_rooms.py
# Finds the room IDs used by a department


from validate_data import load_file, DATA_FOLDER


def get_department_room_ids(department_id: str, folder: str = DATA_FOLDER) -> list[str]:
    """Returns the room IDs used by the given department.
       This used to only look at courses, so the department office and the
       professors' offices never showed up. Now it checks all three.
       Params: department_id: the ID of the department
               folder: the data folder (defaults to data/, the tests pass in their own)
       Returns: a sorted list of room IDs (classrooms, faculty offices and the department office)
    """

    room_ids: set[str] = set()

    # (file, field with the room) - a missing file just means nothing from that file
    sources = [
        ("courses.json", "RoomId"),
        ("faculty.json", "OfficeRoomId"),
    ]
    for file_name, room_field in sources:
        for row in load_file(folder, file_name) or []:
            if row.get("DepartmentId") == department_id:
                room_ids.add(row.get(room_field))

    for department in load_file(folder, "departments.json") or []:
        if department.get("Name") == department_id:
            room_ids.add(department.get("OfficeRoomId"))

    room_ids.discard(None)

    return sorted(room_ids)


if __name__ == "__main__":
    print(get_department_room_ids("DEPT_CSE"))
