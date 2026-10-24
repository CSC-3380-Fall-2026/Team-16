# Data Format

Each file in `data/` is a list of rows that will be imported into Unreal as a DataTable. `Name` is the ID for each row (Unreal uses it as the row name).

Every search ends up at a room, so faculty, courses, and departments all point to a room ID.

## Where the data comes from
We're focused on the first floor right now. First floor room types are from the registrar's [classroom list](https://lsu.edu/registrar/student-services/our-controlled-rooms.php), the College of Engineering's [room reservation page](https://www.lsu.edu/eng/support/index-2.php), and our design doc (checked October 2026). 1120, 1140, and 1232 are still TBD since we couldn't find what they are.

The third floor offices, faculty, and departments are only in here so they're ready once we finish the first floor. The faculty list is from the [CSE faculty page](https://www.lsu.edu/eng/cse/people/faculty/index.php) and the department offices are from each department's contact page. Only faculty with an office in PFT are included.

Courses are still placeholder data until we find where classes actually meet.

Each office's DisplayName has the professor's name or the department in it, like `PFT 3209B - James Ghawaly`. That way searching a name in the app already finds their office, even before faculty search is added. If someone changes offices, update both `faculty.json` and their room in `rooms.json`.

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
