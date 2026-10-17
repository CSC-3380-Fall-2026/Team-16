# Data Format

Each file in `data/` is a list of rows that will be imported into Unreal as a DataTable. `Name` is the ID for each row (Unreal uses it as the row name).

Every search ends up at a room, so faculty, courses, and departments all point to a room ID.

All the data right now is placeholder data until we confirm the real rooms and offices.

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

Run `python tools/validate_data.py` after changing anything in here.
