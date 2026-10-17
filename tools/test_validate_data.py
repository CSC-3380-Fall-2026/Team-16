# test_validate_data.py
# Tests for validate_data.py. Each test starts with a small set of good data,
# breaks one thing, and checks that the validator catches it.
#
# Run from the root of the repo:
#   python -m unittest discover -s tools

import json
import os
import shutil
import tempfile
import unittest

import validate_data


def make_good_data():
    """Returns a small set of data that should pass every check."""
    return {
        "rooms.json": [
            {"Name": "PFT_1200", "RoomNumber": "1200", "DisplayName": "PFT 1200", "Floor": 1, "RoomType": "Lecture"},
            {"Name": "PFT_3325", "RoomNumber": "3325", "DisplayName": "PFT 3325", "Floor": 3, "RoomType": "Office"},
        ],
        "faculty.json": [
            {"Name": "FAC_TEST", "FullName": "Test Person", "DepartmentId": "DEPT_TEST", "OfficeRoomId": "PFT_3325"},
        ],
        "courses.json": [
            {"Name": "CSC_3380", "CourseCode": "CSC 3380", "CourseTitle": "Object-Oriented Design", "DepartmentId": "DEPT_TEST", "RoomId": "PFT_1200"},
        ],
        "departments.json": [
            {"Name": "DEPT_TEST", "DisplayName": "Test Department", "OfficeRoomId": "PFT_3325"},
        ],
    }


class TestValidateData(unittest.TestCase):

    def setUp(self):
        # Each test gets its own temp folder so they don't affect each other
        self.folder = tempfile.mkdtemp()
        self.data = make_good_data()

    def tearDown(self):
        shutil.rmtree(self.folder)

    def write_files(self):
        """Writes self.data into the temp folder as JSON files."""
        for file_name, rows in self.data.items():
            with open(os.path.join(self.folder, file_name), "w") as file:
                json.dump(rows, file)

    def test_good_data_passes(self):
        self.write_files()
        self.assertEqual(validate_data.validate(self.folder), [])

    def test_repo_data_passes(self):
        # The actual data we have in the repo should always pass
        self.assertEqual(validate_data.validate(validate_data.DATA_FOLDER), [])

    def test_missing_field(self):
        del self.data["faculty.json"][0]["FullName"]
        self.write_files()
        errors = validate_data.validate(self.folder)
        self.assertEqual(len(errors), 1)
        self.assertIn("missing FullName", errors[0])

    def test_duplicate_id(self):
        self.data["rooms.json"].append(self.data["rooms.json"][0])
        self.write_files()
        errors = validate_data.validate(self.folder)
        self.assertEqual(len(errors), 1)
        self.assertIn("used more than once", errors[0])

    def test_room_that_doesnt_exist(self):
        self.data["courses.json"][0]["RoomId"] = "PFT_9999"
        self.write_files()
        errors = validate_data.validate(self.folder)
        self.assertEqual(len(errors), 1)
        self.assertIn("PFT_9999", errors[0])

    def test_missing_file(self):
        self.write_files()
        os.remove(os.path.join(self.folder, "departments.json"))
        errors = validate_data.validate(self.folder)
        self.assertEqual(len(errors), 1)
        self.assertIn("Could not load departments.json", errors[0])

    def test_wrong_root_type(self):
        self.data["rooms.json"] = {"Name": "PFT_1200"}
        self.write_files()
        self.assertTrue(validate_data.validate(self.folder))

    def test_non_object_row(self):
        self.data["rooms.json"].append(None)
        self.write_files()
        self.assertTrue(validate_data.validate(self.folder))

    def test_wrong_field_types(self):
        for field, value in [("Floor", True), ("Floor", 1.5), ("Floor", 0), ("Name", []), ("RoomNumber", 1200)]:
            with self.subTest(field=field, value=value):
                self.data = make_good_data()
                self.data["rooms.json"][0][field] = value
                self.write_files()
                self.assertTrue(validate_data.validate(self.folder))

    def test_whitespace_field(self):
        self.data["courses.json"][0]["CourseTitle"] = "   "
        self.write_files()
        self.assertTrue(validate_data.validate(self.folder))

    def test_duplicate_room_number(self):
        self.data["rooms.json"][1]["RoomNumber"] = "1200"
        self.write_files()
        self.assertIn("RoomNumber", validate_data.validate(self.folder)[0])

    def test_invalid_json(self):
        self.write_files()
        with open(os.path.join(self.folder, "rooms.json"), "w") as file:
            file.write("[")
        self.assertTrue(validate_data.validate(self.folder))


if __name__ == "__main__":
    unittest.main()
