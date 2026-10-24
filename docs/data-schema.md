# Data Format

Each file in `data/` is a list of rows that will be imported into Unreal as a DataTable. `Name` is the ID for each row (Unreal uses it as the row name).

Every search ends up at a room, so faculty, courses, and departments all point to a room ID.

All the data right now is placeholder data until we confirm the real rooms and offices.

The rooms from our [design scope](design.md) are in `rooms.json`. If a room has a name (like Panera), it goes in the DisplayName after the number so it shows up when someone searches for it. The Commons doesn't have a room number, so its RoomNumber is just `"Commons"`.

## rooms.json
| Field | Example |
|---|---|
| Name | `PFT_1200` |
| RoomNumber | `"1200"` |
| DisplayName | `"PFT 1200"` |
| Floor | `1` |
| RoomType | `"Lecture"` |

## faculty.json
| Field | Example |
|---|---|
| Name | `FAC_SMITH` |
| FullName | `"John Smith"` |
| DepartmentId | `DEPT_CSE` (from departments.json) |
| OfficeRoomId | `PFT_3325` (from rooms.json) |

## courses.json
| Field | Example |
|---|---|
| Name | `CSC_3380` |
| CourseCode | `"CSC 3380"` |
| CourseTitle | `"Object-Oriented Design"` |
| DepartmentId | `DEPT_CSE` (from departments.json) |
| RoomId | `PFT_1200` (from rooms.json) |

## departments.json
| Field | Example |
|---|---|
| Name | `DEPT_CSE` |
| DisplayName | `"Computer Science and Engineering"` |
| OfficeRoomId | `PFT_3325` (from rooms.json) |

Keys use PascalCase instead of camelCase so they match the Unreal struct names.

Every file must contain a JSON array of objects. Required fields are nonempty
strings without leading/trailing spaces, except `Floor`, which is a positive
integer. IDs are unique within each file; room numbers are unique. References
must point to existing rows. Keep room numbers as strings to preserve formatting.

Task 3 reuses the team's existing schema and placeholder rows. The validator
also rejects malformed JSON, wrong row/field types, blank strings, duplicate room
numbers, and broken references. Placeholder courses do not represent verified
class schedules. Runtime loading and room bindings are separate tasks.

Run `python tools/validate_data.py` after changing anything in here.
