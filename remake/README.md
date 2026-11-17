# Remake tables

The generator builds these into the program and checks them when it starts.

`chests.tsv` has a row for every SoH chest location of the vanilla dungeon layouts, and for each SoH loose item that
the remake keeps in a chest. A location has a second row where the files keep its chest once in each of two versions
of a scene, under two hashes: both rows name the same item, and both chests give the location's item.

- `location`: the SoH location, by its spoiler log name.
- `hash` and `files`: the remake chest's actor hash, and the romfs scene files that place it, comma separated. Both
  are empty when the remake has no chest there.
- `item`: what the chest gives, by the remake's name, and so what it adds to the pool when shuffled. That is its
  `Drop__DropActor`. A plain chest with no `Drop__DropActor` gives a refill of whatever Link is lowest on, and stands
  here as a Large Magic Jar (`Tbox_MagicJar_Large`), the refill one of them gave in the game. Empty for a chest that
  gives nothing a row can name: a trap chest, or a chest that checks what Link owns and names no item.
- `shuffled`: `yes` or `no`. A shuffled chest takes part in the shuffle; every other location keeps its item.
- `note`: why a row is not shuffled.

`items.tsv` maps each remake item name to the SoH item it stands for, by spoiler log name. A remake key, map or
compass names no dungeon, so it has one row per dungeon, and a chest takes the one of its own dungeon.
