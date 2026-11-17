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
NO_HAMMER = ("Fire Temple Megaton Hammer Chest", "Green Rupee")
NO_SILVER_GAUNTLETS = ("Spirit Temple Silver Gauntlets Chest", "Green Rupee")
NO_GOLDEN_GAUNTLETS = ("Ganon's Castle Shadow Trial Golden Gauntlets Chest", "Green Rupee")


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

    def test_a_potion_shop_is_never_the_first_blue_fire(self):
        self.assertTrue(self.reachable("Market Potion Shop Item 2"))
        # Without the Ocarina of Time, Link never becomes adult, and only adult Link reaches other Blue Fire.
        self.assertFalse(self.reachable("Market Potion Shop Item 2", ("HF Ocarina of Time Item", "Green Rupee"),
                                        ("KF Mido Top Left Chest", "Empty Bottle")))

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

    # Goron's Bracelet has a slot of its own. The Silver and Golden Gauntlets share another, which keeps the higher
    # tier found, and only adult Link can use them.

    def test_gauntlets_are_not_the_bracelet_for_a_child(self):
        # Without the Ocarina of Time, Link never becomes adult. Without the bracelet, a child cannot open Dodongo's
        # Cavern, as the only bomb bag is inside it.
        child_without_bracelet = (("HF Ocarina of Time Item", "Green Rupee"), ("GC Darunias Joy", "Green Rupee"))
        self.assertFalse(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                        ("KF Mido Top Left Chest", "Silver Gauntlets"),
                                        ("KF Mido Top Right Chest", "Golden Gauntlets")))
        self.assertTrue(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                       ("KF Mido Top Left Chest", "Goron's Bracelet")))

    def test_gauntlets_lift_what_the_bracelet_lifts_for_an_adult(self):
        # Pushing the Forest Temple's blocks takes the bracelet's strength. Lifting reads one strength level, which
        # either tier of gauntlets puts above the bracelet's.
        for gauntlets in ("Silver Gauntlets", "Golden Gauntlets"):
            self.assertTrue(self.reachable("Phantom Ganon", ("GC Darunias Joy", "Green Rupee"),
                                           ("KF Mido Top Left Chest", gauntlets),
                                           ("KF Mido Bottom Left Chest", "Progressive Bomb Bag")))

    def test_golden_gauntlets_lift_what_silver_gauntlets_lift(self):
        # Only the Silver Gauntlets or the hammer clear the way to the leftmost chest of the Goron City maze.
        self.assertTrue(self.reachable("GC Maze Left Chest", NO_SILVER_GAUNTLETS, NO_HAMMER,
                                       ("KF Mido Top Left Chest", "Golden Gauntlets")))

    def test_silver_gauntlets_do_not_lift_what_golden_gauntlets_lift(self):
        # Only the Golden Gauntlets clear the way to the Great Fairy fountain outside Ganon's Castle.
        self.assertFalse(self.reachable("OGC Great Fairy Reward", NO_GOLDEN_GAUNTLETS,
                                        ("KF Mido Top Left Chest", "Silver Gauntlets")))
        self.assertTrue(self.reachable("OGC Great Fairy Reward", NO_GOLDEN_GAUNTLETS,
                                       ("KF Mido Top Left Chest", "Golden Gauntlets")))

    def test_goron_city_maze_right_chest_needs_silver_gauntlets(self):
        # Boulders only the Silver Gauntlets lift ring the chest. Explosives and the hammer do not open the ring.
        self.assertFalse(self.reachable("GC Maze Right Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS))
        self.assertTrue(self.reachable("GC Maze Right Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))

    def test_the_bracelet_lifts_nothing_that_needs_gauntlets(self):
        # Darunia gives the bracelet.
        self.assertFalse(self.reachable("GC Maze Left Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS, NO_HAMMER))
        self.assertFalse(self.reachable("OGC Great Fairy Reward", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS))
        self.assertTrue(self.reachable("GC Maze Left Chest", NO_GOLDEN_GAUNTLETS, NO_HAMMER,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))


if __name__ == "__main__":
    REACH = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
