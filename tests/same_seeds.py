"""Write the spoiler log and the plan of every seed below under every group of settings below, so that CI can compare
the files Linux and Windows make.

Usage: python3 -I tests/same_seeds.py <kaepora-seedgen> <folder>
Writes <folder>/<settings>/<n>.spoiler.json and <n>.plan.json, where n is the seed's place in SEEDS. Stops at the
first seed that fails.
"""

import concurrent.futures
import hashlib
import json
import os
import pathlib
import string
import subprocess
import sys

ALPHABET = string.ascii_uppercase + string.digits
FIXED_SEED = "6332265119"
# The window takes seeds of up to this many characters.
LONGEST_SEED = 255


# From hashes of n rather than Python's random, so the seeds are the same on every system and Python version.
def digest(n):
    return hashlib.sha256(f"kaepora {n}".encode()).digest()


def ten_digits(n):
    return str(int.from_bytes(digest(n), "big") % 10**10).zfill(10)


def letters_and_digits(n, length):
    return "".join(ALPHABET[byte % len(ALPHABET)] for byte in (digest(n) * 8)[:length])


SEEDS = ([FIXED_SEED, "0", "7", "OWL", "Z9"]
         + [ten_digits(n) for n in range(150)]
         + [letters_and_digits(n, 1 + n % 40) for n in range(150, 230)]
         + [letters_and_digits(n, LONGEST_SEED) for n in range(230, 235)])
EVERY_DUNGEON = ["Deku Tree", "Dodongo's Cavern", "Jabu-Jabu's Belly", "Forest Temple", "Fire Temple", "Water Temple",
                 "Shadow Temple", "Spirit Temple", "Bottom of the Well", "Ice Cavern", "Gerudo Training Ground",
                 "Ganon's Castle"]
IN_OWN_CHESTS = "In their own chests"
SETTINGS = {
    "default": {},
    "keys-in-their-own-chests": {"Small and boss keys": IN_OWN_CHESTS},
    "maps-and-compasses-in-their-own-chests": {"Maps and compasses": IN_OWN_CHESTS},
    "lens-never-needed": {"Lens of Truth": "Never needed"},
    "one-dungeon": {"Shuffled dungeons": ["Ganon's Castle"]},
    "three-dungeons": {"Shuffled dungeons": ["Deku Tree", "Fire Temple", "Spirit Temple"]},
    "child-dungeons-keys-in-their-own-chests": {
        "Shuffled dungeons": EVERY_DUNGEON[:3] + ["Bottom of the Well", "Ice Cavern"],
        "Small and boss keys": IN_OWN_CHESTS},
    "every-setting-changed": {
        "Shuffled dungeons": [name for name in EVERY_DUNGEON if name not in ("Water Temple", "Ice Cavern")],
        "Small and boss keys": IN_OWN_CHESTS, "Maps and compasses": IN_OWN_CHESTS, "Lens of Truth": "Never needed"},
}


def main():
    seedgen, folder = sys.argv[1], pathlib.Path(sys.argv[2])
    runs = []
    for name, settings in SETTINGS.items():
        (folder / name).mkdir(parents=True)
        for n, seed in enumerate(SEEDS):
            out = folder / name / f"{n:03}"
            runs.append([seedgen, seed, f"{out}.spoiler.json", f"{out}.plan.json", json.dumps(settings)])
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count()) as pool:
        for _ in pool.map(lambda args: subprocess.run(args, check=True, stdout=subprocess.DEVNULL), runs):
            pass
    print(f"{len(SEEDS)} seeds under {len(SETTINGS)} groups of settings written to {folder}")


if __name__ == "__main__":
    main()
