# test_directory_search.py
# Tests for directory_search.py. Uses the same small good data set as the
# validator tests so the results don't change when we update the real data.
#
# Run from the root of the repo:
#   python -m unittest discover -s tools

import json
import os
import shutil
import tempfile
import unittest

from directory_search import search, squash
from test_validate_data import make_good_data


class TestDirectorySearch(unittest.TestCase):

    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.data = make_good_data()
        # Give the class a schedule so we can check the time shows up
        self.data["courses.json"][0].update({"Instructor": "Test Person", "MeetingDays": "TR", "StartTime": "12:00", "EndTime": "13:20"})
        self.write_files()

    def tearDown(self):
        shutil.rmtree(self.folder)

    def write_files(self):
        """Writes self.data into the temp folder as JSON files."""
        for file_name, rows in self.data.items():
            with open(os.path.join(self.folder, file_name), "w") as file:
                json.dump(rows, file)

    def test_squash(self):
        self.assertEqual(squash("PFT_12-00 A"), "pft1200a")

    def test_room_id_without_spaces(self):
        # "pft1200" should find PFT 1200 just like the panel does
        self.assertEqual(search("pft1200", self.folder), [("Room", "PFT 1200", "PFT_1200")])

    def test_class_by_title_shows_time(self):
        results = search("object-oriented", self.folder)
        self.assertEqual(results, [("Class", "CSC 3380 - Object-Oriented Design (TR 12:00-13:20)", "PFT_1200")])

    def test_professor_finds_office_and_classes(self):
        # The professor teaches the class, so both show up, and both point at real rooms
        kinds = [(kind, room_id) for kind, _, room_id in search("test person", self.folder)]
        self.assertEqual(kinds, [("Class", "PFT_1200"), ("Professor", "PFT_3325")])

    def test_department(self):
        self.assertEqual(search("test department", self.folder),
                         [("Department", "Test Department - Department Office", "PFT_3325")])

    def test_blank_and_no_match(self):
        self.assertEqual(search("   ", self.folder), [])
        self.assertEqual(search("nothing like this", self.folder), [])

    def test_missing_file(self):
        os.remove(os.path.join(self.folder, "faculty.json"))
        self.assertEqual(search("1200", self.folder), [])


if __name__ == "__main__":
    unittest.main()
