"""Questions to the logic, answered by kaepora-reach.

Usage: python3 -I tests/logic_test.py <kaepora-reach> [test name ...]
Each test places items at locations, leaves every other location as the unmodified game has it, and asks whether
Link can ever collect what is at one location. Names are those of the spoiler log.
"""

import subprocess
import sys
import unittest

REACH = None
# The 10 and 30 Gold Skulltula rewards are the only wallet upgrades.
NO_WALLET_UPGRADE = (("Kak 10 Gold Skulltula Reward", "Green Rupee"), ("Kak 30 Gold Skulltula Reward", "Green Rupee"))
ONE_WALLET_UPGRADE = ("Kak 30 Gold Skulltula Reward", "Green Rupee")


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

    def test_child_comes_back_only_with_the_prelude_of_light(self):
        # Only child Link can drain the well, with the Song of Storms that only adult Link learns.
        self.assertTrue(self.reachable("Bottom of the Well Lens of Truth Chest"))
        # With the Fairy Bow in the well, adult Link cannot finish the Forest Temple to learn the Prelude of Light.
        self.assertFalse(self.reachable("Bottom of the Well Lens of Truth Chest",
                                        ("Bottom of the Well Lens of Truth Chest", "Progressive Bow"),
                                        ("Forest Temple Bow Chest", "Green Rupee")))

    def test_only_adult_link_buys_a_tunic(self):
        # Without the Ocarina of Time, Link never becomes adult.
        never_adult = ("HF Ocarina of Time Item", "Green Rupee")
        self.assertFalse(self.reachable("ZD Shop Item 1", never_adult))
        self.assertFalse(self.reachable("GC Shop Item 5", never_adult))

    def test_a_tunic_needs_the_largest_wallet(self):
        # Each tunic costs 600.
        for location in ("ZD Shop Item 1", "GC Shop Item 5"):
            self.assertFalse(self.reachable(location, ONE_WALLET_UPGRADE))
            self.assertTrue(self.reachable(location))
        # The price belongs to the shop slot, so it holds whatever is placed there.
        self.assertFalse(self.reachable("GC Shop Item 5", ONE_WALLET_UPGRADE, ("GC Shop Item 5", "Buy Goron Tunic")))

    def test_shops_charge_the_remakes_prices(self):
        # A fish costs 50 at the Market Potion Shop. 20 bombchus cost 240.
        self.assertTrue(self.reachable("Market Potion Shop Item 8", *NO_WALLET_UPGRADE))
        self.assertFalse(self.reachable("Market Bombchu Shop Item 5", *NO_WALLET_UPGRADE))

    def test_wallets_hold_200_then_500(self):
        # A poe costs 200 at the Market Potion Shop. 15 bombchus cost 210 and 30 cost 300.
        self.assertTrue(self.reachable("Market Potion Shop Item 7", *NO_WALLET_UPGRADE))
        self.assertFalse(self.reachable("Market Bombchu Shop Item 7", *NO_WALLET_UPGRADE))
        self.assertTrue(self.reachable("Market Bombchu Shop Item 8", ONE_WALLET_UPGRADE))

    def test_crater_great_fairy_needs_magic(self):
        self.assertFalse(self.reachable("DMC Great Fairy Reward", ("DMT Great Fairy Reward", "Green Rupee")))

    def test_bombchu_shop_opens_after_dodongos_cavern(self):
        # With no Goron's Bracelet and the only bomb bag inside it, Dodongo's Cavern stays shut.
        self.assertFalse(self.reachable("Market Bombchu Shop Item 1", ("GC Darunias Joy", "Green Rupee")))

    def test_biggorons_sword_needs_the_whole_trade_chain(self):
        self.assertTrue(self.reachable("DMT Trade Claim Check"))
        self.assertFalse(self.reachable("DMT Trade Claim Check", ("LH Lab Trade Eyeball Frog", "Green Rupee")))

    def test_dampes_heart_piece_needs_a_hookshot(self):
        self.assertFalse(self.reachable("Graveyard Dampe Race Freestanding PoH",
                                        ("Graveyard Hookshot Chest", "Green Rupee")))


if __name__ == "__main__":
    REACH = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
