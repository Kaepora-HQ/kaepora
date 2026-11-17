# Remake tables

The generator builds these into the program and checks them when it starts.

`chests.tsv` has a row for every SoH chest location of the vanilla dungeon layouts, and for each SoH loose item that
the remake keeps in a chest. A location has a second row where the files keep its chest once in each of two versions
of a scene, under two hashes: both rows name the same item, and both chests give the location's item.

- `location`: the SoH location, by SoH's English name for it.
- `hash` and `files`: the remake chest's actor hash, and the romfs scene files that place it, comma separated. Both
  are empty when the remake has no chest there.
- `item`: the chest's `Drop__DropActor`, empty when it has none.
- `shuffled`: `yes` or `no`. A shuffled chest takes part in the shuffle; every other location keeps its item.
- `note`: why a row is not shuffled.

`items.tsv` maps each remake item name to the SoH item it stands for, by SoH's English name. A remake key, map or
compass names no dungeon, so it has one row per dungeon, and a chest takes the one of its own dungeon.
