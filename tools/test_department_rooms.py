# test_department_rooms.py
# Tests for department_rooms.py. Each test writes its own small courses.json
# to a temp folder so the results don't change when we update the real data.
#
# Run from the root of the repo:
#   python -m unittest discover -s tools

import json
import os
import shutil
import tempfile
import unittest

from department_rooms import get_department_room_ids


class TestDepartmentRooms(unittest.TestCase):

    def setUp(self):
        self.folder = tempfile.mkdtemp()

    def tearDown(self):
        shutil.rmtree(self.folder)

    def write_courses(self, courses):
        """Writes the given courses into the temp folder as courses.json."""
        with open(os.path.join(self.folder, "courses.json"), "w") as file:
            json.dump(courses, file)

    def test_finds_department_rooms(self):
        self.write_courses([
            {"Name": "CSC_3380", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1200"},
            {"Name": "CSC_1351", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1240"},
            {"Name": "ME_2000", "DepartmentId": "DEPT_ME", "RoomId": "PFT_1100"},
        ])
        self.assertEqual(get_department_room_ids("DEPT_CSE", self.folder), ["PFT_1200", "PFT_1240"])

    def test_room_only_listed_once(self):
        # Two courses in the same room should still only highlight it once
        self.write_courses([
            {"Name": "CSC_3380", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1200"},
            {"Name": "CSC_1351", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1200"},
        ])
        self.assertEqual(get_department_room_ids("DEPT_CSE", self.folder), ["PFT_1200"])

    def test_unknown_department(self):
        self.write_courses([{"Name": "CSC_3380", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1200"}])
        self.assertEqual(get_department_room_ids("DEPT_NOPE", self.folder), [])

    def test_course_with_no_room(self):
        # A course missing its RoomId shouldn't end up as None in the list
        self.write_courses([
            {"Name": "CSC_3380", "DepartmentId": "DEPT_CSE"},
            {"Name": "CSC_1351", "DepartmentId": "DEPT_CSE", "RoomId": "PFT_1240"},
        ])
        self.assertEqual(get_department_room_ids("DEPT_CSE", self.folder), ["PFT_1240"])

    def test_missing_courses_file(self):
        # Nothing written to the folder, so there's no courses.json to load
        self.assertEqual(get_department_room_ids("DEPT_CSE", self.folder), [])


if __name__ == "__main__":
    unittest.main()
