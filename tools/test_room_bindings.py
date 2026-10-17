import unittest

from export_room_bindings import build_bindings
from validate_data import check_bindings


class TestRoomBindings(unittest.TestCase):
    def test_unique_trigger_maps_room(self):
        actors = [{"Label": "Room_1200", "Min": [0, 20, 30], "Max": [100, 220, 230]}]
        bindings, missing = build_bindings([{"Name": "PFT_1200", "RoomNumber": "1200"}], actors, "/Game/Test")
        self.assertEqual(missing, [])
        self.assertEqual(bindings[0]["Location"], {"X": 50, "Y": 120, "Z": 130})

    def test_missing_and_ambiguous_rooms_are_not_guessed(self):
        actor = {"Label": "Room_1200", "Min": [0, 0, 0], "Max": [100, 100, 100]}
        room = [{"Name": "PFT_1200", "RoomNumber": "1200"}]
        for actors in [[], [actor, actor], [dict(actor, Label="Room_12000")], [dict(actor, Label="Room1200A1")], [dict(actor, Label="Room1200A")]]:
            bindings, missing = build_bindings(room, actors, "/Game/Test")
            self.assertEqual(bindings, [])
            self.assertEqual(missing, ["PFT_1200"])

    def test_bad_bindings_are_rejected(self):
        good = {"RoomId": "PFT_1200", "Map": "/Game/Test", "Location": {"X": 1, "Y": 2, "Z": 3}}
        self.assertEqual(check_bindings([good], {"PFT_1200"}), [])
        for rows in [[good, good], [dict(good, RoomId="missing")], [dict(good, Location={"X": True})], [dict(good, Map="wrong")], None]:
            self.assertTrue(check_bindings(rows, {"PFT_1200"}))
