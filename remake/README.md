# Remake tables

The generator builds these into the program and checks them when it starts.

`chests.tsv` has a row for every chest of the remake, 212 in all, and for every SoH chest location of the vanilla
dungeon layouts, so it is the list of which chests are shuffled and why the others are not. A SoH loose item that the
remake keeps in a chest has a row too. A location has a second row where the files keep its chest twice, under two
hashes: once in each of two versions of a scene, once in each of two scenes that show the same spot, or as the two
chests one puzzle shows one of. Each row names what its own chest holds. Both chests give the location's item, and the
first row's item is what the location adds to the pool.

- `location`: the SoH location, by SoH's English name for it. Empty for a remake chest that no location stands for,
  which is not shuffled.
- `hash` and `files`: the remake chest's actor hash, and the romfs scene files that place it, comma separated. Both
  are empty when the remake has no chest there.
- `item`: what the chest gives, by the remake's name, and so what it adds to the pool when shuffled. That is its
  `Drop__DropActor`. A plain chest with no `Drop__DropActor` gives a refill of whatever Link is lowest on, and stands
  here as a Large Magic Jar (`Tbox_MagicJar_Large`), the refill one of them gave in the game. The one chest that
  checks what Link owns and has no `Drop__DropActor` gave 5 bombs (`Item_BombBundle_Small`) to a Link without the
  checked item, which may be such a refill too, and stands here as that. Empty for a trap chest, which gives
  nothing.
- `shuffled`: `yes` or `no`. A `yes` chest takes part in the shuffle unless the seed's settings keep it in place;
  every other location keeps its item.
- `note`: for a row that is not shuffled, why, in one sentence. For a shuffled row, what else its chest does, if
  anything.

`items.tsv` maps each remake item name to the SoH item it stands for, by SoH's English name. A remake key, map or
compass names no dungeon, so it has one row per dungeon, and a chest takes the one of its own dungeon.

`stones.tsv` has a row for each gossip stone whose SoH stone is known, and the patcher gives a text only to these.

- `label`: the stone's actor `Name`, which is also the label of the message it says in `EventFlowMsg/GossipStone.msbt`.
- `location`: the SoH gossip stone it is, by SoH's English name.
