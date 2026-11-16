"""Questions to the logic, answered by kaepora-reach.

Usage: python3 -I tests/logic_test.py <kaepora-reach> [test name ...]
Each test places items at locations, leaves every other location as the unmodified game has it, and asks whether
Link can ever collect what is at one location. Names are those of the spoiler log.
"""

import subprocess
import sys
import unittest

REACH = None


class LogicTest(unittest.TestCase):
    def reachable(self, location, *placed):
        """Whether Link can collect what is at location, with each (location, item) pair in placed."""
        result = subprocess.run([REACH, location, *(name for pair in placed for name in pair)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(result.stdout, ("yes\n", "no\n"))
        return result.stdout == "yes\n"

    def test_placed_items_change_the_answer(self):
        self.assertTrue(self.reachable("Queen Gohma"))
        self.assertFalse(self.reachable("Queen Gohma", ("KF Kokiri Sword Chest", "Green Rupee")))


if __name__ == "__main__":
    REACH = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
