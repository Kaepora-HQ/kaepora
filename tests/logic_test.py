"""Questions to the logic, answered by kaepora-reach.

Usage: python3 -I -B tests/logic_test.py <kaepora-reach> [test name ...]
Each test places items at locations, leaves every other location as the unmodified game has it, and asks whether
Link can ever collect what is at one location. Names are SoH's English names.
"""

import pathlib
import subprocess
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import reach  # noqa: E402

# The 10 and 30 Gold Skulltula rewards are the only wallet upgrades.
NO_WALLET_UPGRADE = (("Kak 10 Gold Skulltula Reward", "Green Rupee"), ("Kak 30 Gold Skulltula Reward", "Green Rupee"))
ONE_WALLET_UPGRADE = ("Kak 30 Gold Skulltula Reward", "Green Rupee")
NO_HAMMER = ("Fire Temple Megaton Hammer Chest", "Green Rupee")
NO_SILVER_GAUNTLETS = ("Spirit Temple Silver Gauntlets Chest", "Green Rupee")
NO_GOLDEN_GAUNTLETS = ("Ganon's Castle Shadow Trial Golden Gauntlets Chest", "Green Rupee")
SILVER_GAUNTLETS_AT_START = ("Link's Pocket", "Silver Gauntlets")
# Without the Ocarina of Time, Link never becomes adult.
NEVER_ADULT = ("HF Ocarina of Time Item", "Green Rupee")
# Dampe's race gives the first hookshot. The Water Temple's Longshot chest lies past places that need one.
NO_HOOKSHOT = ("Graveyard Hookshot Chest", "Green Rupee")
# The well holds the only Lens of Truth.
NO_LENS = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
LENS_NEVER_NEEDED = {"Lens of Truth": "Never needed"}


class LogicTest(unittest.TestCase):
    reachable = staticmethod(reach.reachable)

    def test_placed_items_change_the_answer(self):
        self.assertTrue(self.reachable("Queen Gohma"))
        self.assertFalse(self.reachable("Queen Gohma", ("KF Kokiri Sword Chest", "Green Rupee")))

    def test_links_pocket_takes_one_item(self):
        result = subprocess.run([reach.PROGRAM, "{}", "Queen Gohma", "Link's Pocket", "Silver Gauntlets",
                                 "Link's Pocket", "Green Rupee"], capture_output=True, text=True)
        self.assertEqual((result.returncode, result.stdout), (2, ""), result.stderr)
        self.assertIn("Link's Pocket", result.stderr)

    def test_child_comes_back_only_with_the_prelude_of_light(self):
        # Only child Link can drain the well, with the Song of Storms that only adult Link learns.
        self.assertTrue(self.reachable("Bottom of the Well Lens of Truth Chest"))
        # With the Fairy Bow in the well, adult Link cannot finish the Forest Temple to learn the Prelude of Light.
        self.assertFalse(self.reachable("Bottom of the Well Lens of Truth Chest",
                                        ("Bottom of the Well Lens of Truth Chest", "Progressive Bow"),
                                        ("Forest Temple Bow Chest", "Green Rupee")))

    def test_only_adult_link_buys_a_tunic(self):
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
        # Without Goron's Bracelet, child Link cannot open Dodongo's Cavern, so he never wins the Goron's Ruby inside
        # it and never becomes adult.
        self.assertFalse(self.reachable("Market Bombchu Shop Item 1", ("GC Darunias Joy", "Green Rupee")))

    def test_dodongos_cavern_opens_to_the_bomb_flower_above_it(self):
        # Bombs from the bag do not break the rock at the mouth. The fruit of the bomb flower on the ledge above does,
        # and picking the flower takes the bracelet's strength.
        child_with_bombs_without_bracelet = (NEVER_ADULT, ("GC Darunias Joy", "Green Rupee"),
                                             ("KF Mido Top Left Chest", "Progressive Bomb Bag"))
        self.assertFalse(self.reachable("Dodongos Cavern Map Chest", *child_with_bombs_without_bracelet))

    def test_biggorons_sword_needs_the_whole_trade_chain(self):
        self.assertTrue(self.reachable("DMT Trade Claim Check"))
        self.assertFalse(self.reachable("DMT Trade Claim Check", ("LH Lab Trade Eyeball Frog", "Green Rupee")))

    def test_dampes_heart_piece_needs_a_hookshot(self):
        self.assertFalse(self.reachable("Graveyard Dampe Race Freestanding PoH",
                                        ("Graveyard Hookshot Chest", "Green Rupee")))

    def test_deku_tree_slingshot_room_side_chest_needs_an_eye_hit(self):
        # An eye in the room drops the ladder to the side chest. Without the slingshot child Link reaches only the
        # room's other chest.
        self.assertTrue(self.reachable("Deku Tree Slingshot Room Side Chest"))
        no_slingshot = ("Deku Tree Slingshot Chest", "Green Rupee")
        self.assertFalse(self.reachable("Deku Tree Slingshot Room Side Chest", no_slingshot))

    def test_kokiri_forest_storms_grotto_chest_needs_the_scrubs_beaten(self):
        # Beating the three Deku Scrub brothers takes a shield to send their nuts back. Without the bow, adult Link never
        # finishes the Forest Temple and never returns to child, so only a Hylian Shield does it.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        no_hylian_shield = (("Market Bazaar Item 1", "Green Rupee"), ("Kak Bazaar Item 1", "Green Rupee"),
                            ("Graveyard Shield Grave Chest", "Green Rupee"))
        self.assertTrue(self.reachable("KF Storms Grotto Chest", no_bow))
        self.assertFalse(self.reachable("KF Storms Grotto Chest", no_bow, *no_hylian_shield))

    # Goron's Bracelet has a slot of its own. The Silver and Golden Gauntlets share another, which keeps the higher
    # tier found, and only adult Link can use them.

    def test_gauntlets_are_not_the_bracelet_for_a_child(self):
        # Without the Ocarina of Time, Link never becomes adult. Without the bracelet, a child cannot pick the bomb
        # flower whose fruit opens Dodongo's Cavern.
        child_without_bracelet = (("HF Ocarina of Time Item", "Green Rupee"), ("GC Darunias Joy", "Green Rupee"))
        self.assertFalse(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                        ("KF Mido Top Left Chest", "Silver Gauntlets"),
                                        ("KF Mido Top Right Chest", "Golden Gauntlets")))
        self.assertTrue(self.reachable("Dodongos Cavern Map Chest", *child_without_bracelet,
                                       ("KF Mido Top Left Chest", "Goron's Bracelet")))

    def test_a_bomb_needs_the_bracelets_strength(self):
        # Link takes a bomb out of the bag only at the strength the bracelet gives, or adult Link's gauntlets give. The
        # rock before the Death Mountain Trail chest takes a bomb or the hammer.
        no_bracelet = ("GC Darunias Joy", "Green Rupee")
        bomb_bag = ("KF Mido Top Left Chest", "Progressive Bomb Bag")
        self.assertFalse(self.reachable("DMT Chest", NEVER_ADULT, no_bracelet, bomb_bag))
        self.assertTrue(self.reachable("DMT Chest", NEVER_ADULT, bomb_bag))
        # Without the bracelet, a child reaches neither the Goron's Ruby nor the Zora's Sapphire, so they come from
        # elsewhere for Link to become adult.
        adult_without_strength = (NO_HAMMER, bomb_bag, no_bracelet, NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS,
                                  ("KF Mido Bottom Left Chest", "Goron's Ruby"),
                                  ("KF Mido Bottom Right Chest", "Zora's Sapphire"))
        self.assertFalse(self.reachable("DMT Chest", *adult_without_strength))
        self.assertTrue(self.reachable("DMT Chest", *adult_without_strength,
                                       ("KF Mido Top Right Chest", "Silver Gauntlets")))

    def test_a_bombchu_needs_no_strength(self):
        # The logic counts bombchus only with a bomb bag and a place to buy them, here a Mido's House chest. The rock
        # before the Death Mountain Trail chest takes a bomb, a bombchu or the hammer.
        child_with_bombs_without_bracelet = (NEVER_ADULT, ("GC Darunias Joy", "Green Rupee"),
                                             ("KF Mido Top Left Chest", "Progressive Bomb Bag"))
        self.assertFalse(self.reachable("DMT Chest", *child_with_bombs_without_bracelet))
        self.assertTrue(self.reachable("DMT Chest", *child_with_bombs_without_bracelet,
                                       ("KF Mido Top Right Chest", "Buy Bombchu (10)")))

    def test_gauntlets_lift_what_the_bracelet_lifts_for_an_adult(self):
        # Pushing the Forest Temple's blocks takes the bracelet's strength. Lifting reads one strength level, which
        # either tier of gauntlets puts above the bracelet's. Without the bracelet, a child reaches neither the Goron's
        # Ruby nor the Zora's Sapphire, so they come from elsewhere.
        for gauntlets in ("Silver Gauntlets", "Golden Gauntlets"):
            self.assertTrue(self.reachable("Phantom Ganon", ("GC Darunias Joy", "Green Rupee"),
                                           ("KF Mido Top Left Chest", gauntlets),
                                           ("KF Mido Bottom Left Chest", "Zora's Sapphire"),
                                           ("KF Mido Top Right Chest", "Goron's Ruby")))

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

    def test_goron_city_maze_center_side_chest_is_reachable(self):
        self.assertTrue(self.reachable("GC Maze Center Side Chest"))

    def test_goron_city_grotto_chest_needs_the_hookshot(self):
        # Two hookshot targets cross the lava room to the grotto. Goron City has no Song of Time block.
        self.assertTrue(self.reachable("GC Grotto Chest"))
        self.assertFalse(self.reachable("GC Grotto Chest", NO_HOOKSHOT))

    def test_the_bracelet_lifts_nothing_that_needs_gauntlets(self):
        # Darunia gives the bracelet.
        self.assertFalse(self.reachable("GC Maze Left Chest", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS, NO_HAMMER))
        self.assertFalse(self.reachable("OGC Great Fairy Reward", NO_SILVER_GAUNTLETS, NO_GOLDEN_GAUNTLETS))
        self.assertTrue(self.reachable("GC Maze Left Chest", NO_GOLDEN_GAUNTLETS, NO_HAMMER,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))

    def test_training_ground_heavy_block_second_chest_needs_the_eye_hit(self):
        # Without the bow, the Forest Temple and so the well's Lens of Truth are out of reach, so the gauntlets and the
        # Lens come from elsewhere. The barred door into the room of all four chests opens to an eye hit as well.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        outside = (("KF Mido Top Left Chest", "Silver Gauntlets"), ("KF Mido Top Right Chest", "Lens of Truth"))
        self.assertFalse(self.reachable("Gerudo Training Ground Heavy Block First Chest", no_bow, *outside))
        self.assertFalse(self.reachable("Gerudo Training Ground Heavy Block Second Chest", no_bow, *outside))
        bow = ("KF Mido Bottom Left Chest", "Progressive Bow")
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block First Chest", no_bow, bow, *outside))

    def test_training_ground_door_behind_the_heavy_block_needs_no_lens(self):
        # An arrow from the floor in front of the barred door, behind the pushed block, hits the eye above it with the
        # Lens of Truth off. The well holds the only Lens. Without it the wasteland stays shut, so the gauntlets that push
        # the block come from elsewhere.
        no_lens = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block First Chest", no_lens,
                                       ("KF Mido Top Left Chest", "Silver Gauntlets")))

    def test_training_ground_lobby_left_chest_needs_no_eye_hit(self):
        # The invisible left chest stands there from the start. The eye switch shows only the right one.
        no_bow = ("Forest Temple Bow Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Lobby Left Chest", no_bow))
        self.assertFalse(self.reachable("Gerudo Training Ground Lobby Right Chest", no_bow))

    def test_jabu_jabu_map_chest_needs_no_boomerang(self):
        # The chest stands beyond the basement below the holes, and no tentacle stands in the way.
        self.assertTrue(self.reachable("Jabu Jabus Belly Map Chest", ("Jabu Jabus Belly Boomerang Chest", "Green Rupee")))

    def test_invisible_chests_and_illusion_walls_need_no_lens(self):
        # An invisible chest opens for Link who walks up to it, and Link walks through an illusion wall.
        no_lens = ("Bottom of the Well Lens of Truth Chest", "Green Rupee")
        self.assertTrue(self.reachable("Gerudo Training Ground Hidden Ceiling Chest", no_lens))
        self.assertTrue(self.reachable("Bottom of the Well Front Left Fake Wall Chest", no_lens))
        self.assertTrue(self.reachable("Shadow Temple Map Chest", no_lens))

    def test_well_locks_each_take_both_keys(self):
        # Both locked doors open off the middle, so either can take the first of the two keys.
        self.assertTrue(self.reachable("Bottom of the Well GS West Inner Room"))
        self.assertTrue(self.reachable("Bottom of the Well GS East Inner Room"))
        one_key = ("Bottom of the Well Right Bottom Fake Wall Chest", "Green Rupee")
        self.assertFalse(self.reachable("Bottom of the Well GS West Inner Room", one_key))
        self.assertFalse(self.reachable("Bottom of the Well GS East Inner Room", one_key))

    def test_well_east_inner_room_skulltula_needs_no_lens(self):
        for settings in ({"Lens of Truth": "Needed"}, LENS_NEVER_NEEDED):
            self.assertTrue(self.reachable("Bottom of the Well GS East Inner Room", NO_LENS, settings=settings))

    def test_lens_never_needed_waives_every_rule_that_asks_for_it(self):
        for location in ("Bottom of the Well Compass Chest", "Bongo Bongo"):
            self.assertFalse(self.reachable(location, NO_LENS), location)
            self.assertTrue(self.reachable(location, NO_LENS, settings=LENS_NEVER_NEEDED), location)

    def test_well_hidden_pits_door_has_no_lock(self):
        self.assertTrue(self.reachable("Bottom of the Well Fire Keese Chest",
                                       ("Bottom of the Well Right Bottom Fake Wall Chest", "Green Rupee"),
                                       ("Bottom of the Well Freestanding Key", "Green Rupee")))

    def test_well_map_chest_needs_an_explosive(self):
        # A child breaks the rocks in front of the basement's Map Chest only with an explosive. The basement has no bomb
        # flower, and Din's Fire leaves the rocks standing. Without the only bomb bag, Link reaches neither the Goron's
        # Ruby nor the Zora's Sapphire nor a magic meter, so they come from elsewhere, with Din's Fire.
        kit = (("KF Mido Top Left Chest", "Goron's Ruby"), ("KF Mido Bottom Left Chest", "Zora's Sapphire"),
               ("KF Mido Bottom Right Chest", "Progressive Magic Meter"), ("KF Mido Top Right Chest", "Din's Fire"))
        self.assertFalse(self.reachable("Bottom of the Well Map Chest",
                                        ("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), *kit))
        self.assertTrue(self.reachable("Bottom of the Well Map Chest", *kit))
        # Without the bracelet a child uses no bomb, but a bombchu needs no strength. Adult Link's Silver Gauntlets push
        # the Forest Temple's blocks and open Dodongo's Cavern, where the bomb bag lies. Without the bracelet, a child
        # reaches neither the Goron's Ruby nor the Zora's Sapphire, so they come from elsewhere.
        self.assertTrue(self.reachable("Bottom of the Well Map Chest", ("GC Darunias Joy", "Green Rupee"),
                                       ("KF Mido Top Left Chest", "Silver Gauntlets"),
                                       ("KF Mido Top Right Chest", "Goron's Ruby"),
                                       ("KF Mido Bottom Left Chest", "Zora's Sapphire")))

    def test_forest_temple_locks_form_one_chain(self):
        # Five locked doors stand one behind the other: after the first room, into the west hallway, at the top of the
        # block push rooms, into the Blue Poe room and into the frozen eye room. Each takes one key more than the last.
        def without(*chests):
            return [(f"Forest Temple {chest} Chest", "Green Rupee") for chest in chests]
        self.assertFalse(self.reachable("Forest Temple First Stalfos Chest", *without("First Room")))
        # The temple's own bow lies behind the third door. One found outside hits the eye switches before it.
        bow = ("KF Mido Top Left Chest", "Progressive Bow")
        self.assertFalse(self.reachable("Forest Temple Eye Switch Chest", bow, *without("First Stalfos", "Well")))
        self.assertFalse(self.reachable("Forest Temple Boss Key Chest", bow, *without("Well", "Floormaster")))
        self.assertTrue(self.reachable("Forest Temple Boss Key Chest", bow, *without("Floormaster", "Red Poe")))
        self.assertFalse(self.reachable("Forest Temple Blue Poe Chest", *without("Floormaster", "Red Poe")))
        self.assertTrue(self.reachable("Forest Temple Blue Poe Chest", *without("Floormaster")))

    def test_shadow_temple_hover_boots_chest_needs_no_hover_boots(self):
        # On a first visit the corridor floor falls under Link, and he climbs out of the pit on Dead Hand's side. The
        # Nocturne of Shadow, the only way into the temple, warps him out.
        no_hover_boots = ("Shadow Temple Hover Boots Chest", "Green Rupee")
        self.assertTrue(self.reachable("Shadow Temple Hover Boots Chest", no_hover_boots))

    def test_shadow_temple_fifth_lock_guards_the_boss_key(self):
        # The four other locks stand on the way to the maze, so the boss key room takes all five keys. The door before
        # the boss has no lock.
        self.assertTrue(self.reachable("Shadow Temple Boss Key Chest"))
        boss_key_for_last_key = ("Shadow Temple Invisible Floormaster Chest", "Shadow Temple Boss Key")
        self.assertFalse(self.reachable("Shadow Temple Boss Key Chest", boss_key_for_last_key))
        self.assertTrue(self.reachable("Bongo Bongo", boss_key_for_last_key))

    def test_fire_temple_locks_take_one_key_more_each(self):
        # A new game starts with the lowest locked door open, and seven locks take a key. Four stand before the narrow
        # path room. Its door into the fire wall chase has no lock, so the chase door into the maze is the fifth. The
        # Song of Time cage door and the remake's lock into the Flare Dancer room come last, in either order.
        def without(*chests):
            return [(f"Fire Temple {chest} Chest", "Green Rupee") for chest in chests]
        # The lower boulder maze holds the map. The walkway above it, reached through the chase, holds a key, which
        # cannot open the narrow path room's door before it.
        self.assertTrue(self.reachable("Fire Temple Boulder Maze Upper Chest"))
        self.assertFalse(self.reachable("Fire Temple Boulder Maze Upper Chest", *without("Boulder Maze Side Room")))
        self.assertTrue(self.reachable("Fire Temple Compass Chest", *without("Boulder Maze Shortcut")))
        self.assertFalse(self.reachable("Fire Temple Compass Chest",
                                        *without("Boulder Maze Shortcut", "Boulder Maze Upper")))
        # With a hammer and Hover Boots found outside, Link crosses the maze platforms to the Flare Dancer door without
        # opening the cage door.
        outside = (("KF Mido Top Left Chest", "Megaton Hammer"), ("KF Mido Top Right Chest", "Hover Boots"))
        only_six_keys = without("Compass", "Highest Goron")
        self.assertTrue(self.reachable("Fire Temple Flame Maze Right Pot 1", *outside, *only_six_keys))
        self.assertFalse(self.reachable("Fire Temple Megaton Hammer Chest", *outside, *only_six_keys))
        self.assertTrue(self.reachable("Fire Temple Megaton Hammer Chest", *outside, *without("Highest Goron")))

    # A new game starts with the Water Temple's west middle-floor door open. Three locks take a key, the waterfall,
    # boulders and central pillar doors. Each opens off the main hall, so any of them can be the third opened.

    def test_water_temple_waterfall_door_takes_the_third_key(self):
        # The door at the top of the waterfall room has no lock. Before the temple's own Longshot, Link reaches three
        # keys: the bow target key needs it.
        self.assertTrue(self.reachable("Water Temple Longshot Chest"))
        self.assertFalse(self.reachable("Water Temple Longshot Chest", ("Water Temple Dragon Chest", "Green Rupee")))

    def test_water_temple_boulders_door_takes_the_third_key(self):
        # The door into the boss key room has no lock. A Longshot found outside crosses the spike moat and reaches the
        # bow target key before any lock.
        longshot = ("KF Mido Top Left Chest", "Progressive Hookshot")
        no_dragon_key = ("Water Temple Dragon Chest", "Green Rupee")
        no_torches_key = ("Water Temple Torches Chest", "Green Rupee")
        self.assertTrue(self.reachable("Water Temple Boss Key Chest", longshot, no_dragon_key))
        self.assertFalse(self.reachable("Water Temple Boss Key Chest", longshot, no_dragon_key, no_torches_key))

    def test_water_temple_central_pillar_door_takes_the_third_key(self):
        # With no bow and no fire source, Link raises the water to the middle only through this door, and reaches two
        # keys before it.
        no_fire = (("Forest Temple Bow Chest", "Green Rupee"), ("DMT Great Fairy Din's Fire", "Green Rupee"))
        self.assertFalse(self.reachable("Water Temple Central Pillar Chest", *no_fire))
        self.assertTrue(self.reachable("Water Temple Central Pillar Chest", *no_fire,
                                       ("Water Temple GS Behind Gate", "Water Temple Small Key")))

    def test_water_temple_bow_target_chest_needs_the_longshot(self):
        # An arrow in the eye opens the gate to the chest's room, and it shuts before the Hover Boots get Link there.
        self.assertTrue(self.reachable("Water Temple Central Bow Target Chest"))
        self.assertFalse(self.reachable("Water Temple Central Bow Target Chest",
                                        ("Water Temple Longshot Chest", "Green Rupee"),
                                        ("KF Mido Top Left Chest", "Hover Boots")))

    # Chests SoH has no location for.

    def test_ganons_castle_lock_takes_the_key_of_the_light_trial_chest(self):
        # The castle's one small key lies in an invisible chest in the Light Trial's Lullaby room, before its one lock.
        # Every trial, the Light Trial's behind that lock among them, stands before the tower.
        no_key = ("Ganon's Castle Light Trial Triforce Room Invisible Chest", "Green Rupee")
        self.assertTrue(self.reachable("Ganon's Castle Light Trial Triforce Room Invisible Chest",
                                       SILVER_GAUNTLETS_AT_START))
        self.assertTrue(self.reachable("Ganon's Castle Tower Boss Key Chest", SILVER_GAUNTLETS_AT_START))
        self.assertFalse(self.reachable("Ganon's Castle Tower Boss Key Chest", SILVER_GAUNTLETS_AT_START, no_key))

    def test_the_treasure_chest_games_prize_needs_the_lens_under_every_setting(self):
        # Each room deals its key to one of two chests at random, and the Lens shows which. Without it a win takes five
        # lucky picks.
        self.assertTrue(self.reachable("Market Treasure Chest Game Reward"))
        self.assertFalse(self.reachable("Market Treasure Chest Game Reward", NO_LENS, settings=LENS_NEVER_NEEDED))

    # Chests SoH has no location for.

    def test_lost_woods_bridge_chest_needs_the_bridge_bean_as_adult(self):
        # Adult Link rides the platform that grows from the magic bean planted by the bridge.
        self.assertTrue(self.reachable("LW Bridge Chest"))
        self.assertFalse(self.reachable("LW Bridge Chest", ("ZR Magic Bean Salesman", "Green Rupee")))
        self.assertFalse(self.reachable("LW Bridge Chest", NEVER_ADULT))

    def test_kakariko_lookout_chest_needs_the_longshot(self):
        self.assertTrue(self.reachable("Kak Lookout Chest"))
        self.assertFalse(self.reachable("Kak Lookout Chest", ("Water Temple Longshot Chest", "Green Rupee")))

    def test_zoras_fountain_scarecrow_chest_needs_the_hookshot(self):
        self.assertTrue(self.reachable("ZF Scarecrow Chest"))
        self.assertFalse(self.reachable("ZF Scarecrow Chest", NO_HOOKSHOT))

    def test_gerudo_valley_storms_grotto_chests_need_the_song_of_storms(self):
        for chest in ("GV Storms Grotto Chest", "GV Storms Grotto Visible Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, ("Song from Windmill", "Green Rupee")))

    def test_gerudo_valley_octorok_grotto_chest_needs_the_iron_boots(self):
        # The chest stands under water, in the grotto under a boulder that takes the Silver Gauntlets.
        self.assertTrue(self.reachable("GV Octorok Grotto Chest", SILVER_GAUNTLETS_AT_START))
        no_iron_boots = ("Ice Cavern Iron Boots Chest", "Green Rupee")
        self.assertFalse(self.reachable("GV Octorok Grotto Chest", SILVER_GAUNTLETS_AT_START, no_iron_boots))

    def test_colossus_grotto_chest_needs_the_beamos_killed(self):
        # Under a boulder that takes the Silver Gauntlets. The chest appears once a Beamos and five Skullwalltulas die,
        # and only explosives kill the Beamos. Only Dodongo's Cavern holds a bomb bag, and bombchus count only with a
        # bag, so without it Link has no explosives; the stones and the magic meter that take them lie elsewhere, so he
        # still reaches the grotto.
        no_bomb_bag = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"),
                       ("KF Mido Top Left Chest", "Goron's Ruby"), ("KF Mido Bottom Left Chest", "Zora's Sapphire"),
                       ("KF Storms Grotto Chest", "Progressive Magic Meter"))
        self.assertTrue(self.reachable("Colossus Grotto Chest", SILVER_GAUNTLETS_AT_START))
        self.assertTrue(self.reachable("Colossus Deku Scrub Grotto Front", SILVER_GAUNTLETS_AT_START, *no_bomb_bag))
        self.assertFalse(self.reachable("Colossus Grotto Chest", SILVER_GAUNTLETS_AT_START, *no_bomb_bag))

    def test_gerudo_fortress_grotto_chest_is_reachable(self):
        self.assertTrue(self.reachable("GF Grotto Chest"))

    def test_sacred_forest_meadow_song_of_time_grotto_chest_needs_the_song_of_time(self):
        # Song of Time blocks stand before the chest, in the grotto under a rock near Saria.
        self.assertTrue(self.reachable("SFM Song of Time Grotto Chest"))
        self.assertFalse(self.reachable("SFM Song of Time Grotto Chest", ("Song from Ocarina of Time", "Green Rupee")))

    def test_hyrule_field_poe_chests_need_adult_link(self):
        for chest in ("HF South Poe Chest", "HF North Poe Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, NEVER_ADULT))

    def test_hyrule_field_southwest_poe_chest_needs_the_lake_refilled(self):
        # Beating Morpha refills the lake, and Morpha's door takes the Water Temple's boss key.
        self.assertTrue(self.reachable("HF Southwest Poe Chest"))
        self.assertFalse(self.reachable("HF Southwest Poe Chest", ("Water Temple Boss Key Chest", "Green Rupee")))

    def test_hyrule_field_redead_grotto_chest_needs_the_song_of_storms(self):
        self.assertTrue(self.reachable("HF Redead Grotto Chest"))
        self.assertFalse(self.reachable("HF Redead Grotto Chest", ("Song from Windmill", "Green Rupee")))

    def test_hyrule_field_fairy_thought_chest_needs_both_ages(self):
        # Adult Link reads the Fairy Thought beside the chest, and the chest then opens for child Link. Without the
        # Prelude of Light, adult Link never returns to child.
        self.assertTrue(self.reachable("HF Fairy Thought Chest"))
        self.assertFalse(self.reachable("HF Fairy Thought Chest", NEVER_ADULT))
        self.assertFalse(self.reachable("HF Fairy Thought Chest", ("Sheik at Temple", "Green Rupee")))

    def test_lake_hylia_grotto_chest_needs_fire_for_four_candles(self):
        # A child lights the candles with Din's Fire and a Deku Stick, an adult with Din's Fire or fire arrows alone.
        no_dins_fire = ("DMT Great Fairy Din's Fire", "Green Rupee")
        no_fire_arrows = ("LH Sun", "Green Rupee")
        self.assertTrue(self.reachable("LH Grotto Chest", no_dins_fire))
        self.assertFalse(self.reachable("LH Grotto Chest", no_dins_fire, no_fire_arrows))

    def test_hyrule_field_tower_chest_needs_the_hookshot(self):
        self.assertTrue(self.reachable("HF Tower Chest", ("Water Temple Longshot Chest", "Green Rupee")))
        self.assertFalse(self.reachable("HF Tower Chest", NO_HOOKSHOT))

    def test_crater_hammer_grotto_chest_needs_the_hammer(self):
        self.assertTrue(self.reachable("DMC Hammer Grotto Chest"))
        self.assertFalse(self.reachable("DMC Hammer Grotto Chest", NO_HAMMER))

    def test_dodongos_cavern_lobby_alcove_chest_needs_a_blast(self):
        # Beside the map chest, behind a cracked wall that no bomb flower stands near. The bracelet alone opens the way
        # to the map chest, and only Dodongo's Cavern holds a bomb bag.
        no_blast = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), NO_HAMMER)
        self.assertTrue(self.reachable("Dodongos Cavern Lobby Alcove Chest"))
        self.assertTrue(self.reachable("Dodongos Cavern Map Chest", *no_blast))
        self.assertFalse(self.reachable("Dodongos Cavern Lobby Alcove Chest", *no_blast))

    def test_dodongos_cavern_stairs_niche_chest_needs_a_blast(self):
        # At the top of a ladder above the staircase, behind a cracked wall. Link cannot climb a ladder holding a bomb
        # flower, which is enough to drop the staircase and pass the Armos room to the compass chest.
        no_blast = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"), NO_HAMMER)
        self.assertTrue(self.reachable("Dodongos Cavern Stairs Niche Chest"))
        self.assertTrue(self.reachable("Dodongos Cavern Compass Chest", *no_blast))
        self.assertFalse(self.reachable("Dodongos Cavern Stairs Niche Chest", *no_blast))

    def test_ice_cavern_block_ledge_chest_is_reachable(self):
        self.assertTrue(self.reachable("Ice Cavern Block Ledge Chest"))

    def test_bottom_of_the_well_chest_before_dead_hand_is_reachable(self):
        self.assertTrue(self.reachable("Bottom of the Well Before Dead Hand Chest"))

    def test_fire_temple_scarecrow_ledge_chests_need_what_the_scarecrow_chest_needs(self):
        # Both stand beside the Scarecrow Chest from the start. The way up takes the scarecrow's song and a hookshot.
        for chest in ("Fire Temple Scarecrow Ledge Left Chest", "Fire Temple Scarecrow Ledge Right Chest"):
            self.assertTrue(self.reachable(chest))
            self.assertFalse(self.reachable(chest, NO_HOOKSHOT))

    def test_training_ground_cracked_wall_chest_needs_explosives(self):
        # The only bomb bag lies in Dodongo's Cavern, and bombchus count only with a bag. Without one, Link reaches
        # neither the Goron's Ruby nor the Zora's Sapphire nor a magic meter, so those come from elsewhere, with the
        # gauntlets and the Lens of Truth that the way into the room takes.
        way_in = (("KF Mido Top Right Chest", "Silver Gauntlets"), ("KF Mido Bottom Right Chest", "Lens of Truth"))
        no_bomb_bag = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"),
                       ("KF Mido Top Left Chest", "Goron's Ruby"), ("KF Mido Bottom Left Chest", "Zora's Sapphire"),
                       ("KF Storms Grotto Chest", "Progressive Magic Meter"), *way_in)
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block Cracked Wall Chest", *way_in))
        self.assertTrue(self.reachable("Gerudo Training Ground Heavy Block Third Chest", *no_bomb_bag))
        self.assertFalse(self.reachable("Gerudo Training Ground Heavy Block Cracked Wall Chest", *no_bomb_bag))

    def test_spirit_temple_climb_room_door_needs_no_explosives(self):
        # A child reaches the statue room, where the Map Chest stands, only through the climb room's door, which stands
        # open from the start. Without the only bomb bag, Link reaches neither the Goron's Ruby nor the Zora's Sapphire
        # nor a magic meter, so those come from elsewhere.
        no_bomb_bag = (("Dodongos Cavern Bomb Bag Chest", "Green Rupee"),
                       ("KF Mido Top Left Chest", "Goron's Ruby"), ("KF Mido Bottom Left Chest", "Zora's Sapphire"),
                       ("KF Mido Bottom Right Chest", "Progressive Magic Meter"))
        self.assertTrue(self.reachable("Spirit Temple Map Chest", *no_bomb_bag))


if __name__ == "__main__":
    reach.PROGRAM = sys.argv[1]
    unittest.main(argv=[sys.argv[0], *sys.argv[2:]])
