# Remake tables

The generator builds these into the program and checks them when it starts. The patcher builds in `stones.tsv` and
`shop_goods.tsv` too. With the game file, `tests/remake_tables_test.py` checks the tables against the game's own files.

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

`stones.tsv` has a row for each gossip stone with a location in the logic. Only these get a hint, and the patcher gives
a text only to these.

- `label`: the stone's actor `Name`, which is also the label of the message it says in `EventFlowMsg/GossipStone.msbt`.
- `location`: the SoH gossip stone it is, by SoH's English name, or for a stone that pairs with no SoH stone, the
  location added for it.

A hint names places and items as the game's English text does, so it matches the map and the item screen. With the
game file, a test checks each `name` against the game's message at `label`, in both English text packs, leaving out the
codes the game puts before a noun.

- `places.tsv` names each SoH area that holds a shuffled chest. `area` is the prefix SoH gives the names of the area's
  locations, and `label` is the place's label in `LocationMsg/Location.msbt`. `says` is what a hint says: the name,
  with `the` where the game's text writes one before it, or the end of the name, as "Ganondorf's Castle" for "Inside
  Ganondorf's Castle". A test checks that it is.
- `item_names.tsv` names each remake item of `items.tsv`. `label` is in `ActorMsg/PouchContent.msbt`: the game names an
  item by its actor's `ActorNameRef`, which for some items is another actor. `article` is `a`, `the` or empty.

`chest_hints.tsv` has the sentence a hint about one chest says, for each shuffled chest, in SoH's markup: `#` around
the words in colour, and `[[1]]` where the item goes. Where SoH has the chest and its sentence is true of the remake, it
is SoH's English sentence with the remake's place names. Every other sentence is written in the same style. Each shop
slot that "Shop goods" can shuffle has a row too, with SoH's one sentence for its shop.

`junk_hints.tsv` has the hints that say nothing about the seed, in SoH's markup, with `@` for the player's name: SoH's
jokes, but for three about SoH's own project, and six of ours.

`shops.tsv` has a row for each of SoH's 64 shop locations, by shop and then by item number. Each row is one slot of a
remake shop. With "Shop goods" at "Four per shop shuffled", items 5 to 8 of each shop are shuffled, the four that SoH's
shopsanity takes at that count. Each shuffled slot adds a Red Rupee to the pool and takes one of SoH's cheap balanced
prices, 0 to 95 rupees. Every other slot keeps the game's goods and price.

- `location`: the SoH location, by SoH's English name for it.
- `number`: the location's item number in its shop, 1 to 8, as SoH numbers them.
- `slot`: the slot's index, 0 to 7, in each shopkeeper's goods list.
- `shopkeepers`: each shopkeeper actor that sells the slot, with the goods list in its pack, as `actor:goods list`,
  comma separated. Each pack holds its own copy of the list, so the patcher edits each. Each shopkeeper counts its own
  sales, so a shuffled slot sells its item once from each.
- `shelves`: the shelf actor that shows the slot in each scene, as `file:hash`, comma separated.
- `goods`: the goods actor that the game sells in the slot.

Seen in the game:

- A slot sells another goods actor at its own price, once, and stays sold out after a reload.
- A Kokiri Shop slot sells from the start once the patcher lifts its gate on meeting the Deku Tree, from the shopkeeper
  who stands until then too.
- A child can buy an adult's item: the Market Bazaar sold him the Silver Gauntlets.

Read from the game's data and not yet seen:

- A day passing leaves a sold slot sold out, since its count's reset type is 0 where the game's is 2.
- The Bombchu Shop's shopkeeper stands only once Link has the Goron Ruby.
- The Goron Shop refuses a child a tunic by its name: the shop's flow compares the chosen goods with the two tunics,
  then asks whether Link is a child. GC Shop Item 5 still asks for adult Link until that is seen.

The fill keeps dungeon items, the Bow, the Slingshot, the Bomb Bag and Bombchus out of shops. A shop may refuse
arrows, seeds or bombs to a Link without the quiver, the bullet bag or the Goron Ruby, and which of these kinds the
four items fall under is unknown.

`shop_goods.tsv` has a row for each item a shop slot can hold, and the generator fails when it starts if one has no
row. A slot can hold what a shuffled chest holds, but for the dungeon items and the four `KeptFromShops` keeps out, and
the shop filler. Four items are sold as the game's own goods, named in `goods`. Every other item is sold from a goods
actor of its own, which shows the item's own model. The game's own goods rest the lowest point of their shop pose
between 26 mm under their shelf's origin (the Hylian Shield) and 34 mm over it (the heart), and the patcher raises each
shelf so that its item rests on the origin. It also sizes the item as the game sizes its own goods, whose shop poses
shrink some and grow others: an item taller or reaching farther than the game's goods shrinks to the largest size that
fits them, and one smaller than the smallest of them grows toward its size, by at most the 1.4 the game grows its
masks.

- `item`: the remake item.
- `goods`: the game's goods actor that sells the item, or empty for goods of its own.
- `pose`: the pose of the item's model that a shop plays: `Pose_Shop` where its animations have one, else
  `Pose_Common`.
- `bottom` and `top`: the model's lowest and highest points in that pose, over the goods' origin, in millimetres.
- `width`: the model's width along its X axis, across the shelf, in that pose, in millimetres.
- `reach`: how far the model reaches from the upright through that origin, in millimetres.

`python3 -I -B tests/shop_goods.py <game.nup> <item>...` prints the row of each item, and the tables test checks every
row the same way. It reads the model and animation files the item's `ActorInfo` row names, poses the model's bones
with the pose's one frame and skins each vertex to them, and counts only the shapes the item's own bone visibility set
shows. In the bind pose that gives the `ActorInfo` bounds of every item of the table and every goods of the eight
shops, to 0.02 mm, but one. The Business Scrub's seed has its `ActorInfo` bounds posed: `Pose_Common`, with the model
lifted 50 mm, as `Rotate_Obj` lifts its root. On a shelf the goods play `Rotate_Stoped`, which lifts nothing.

That a shop plays `Pose_Shop`, and `Pose_Common` for a model with no `Pose_Shop`, was inferred from where the game's
goods rest: the masks, the Deku Shield and the tunics have no `Pose_Shop`, and in their bind pose the masks and the
Deku Shield would sink into the shelf or lie flat, where in `Pose_Common` they rest on it. Seen in the game, with all 25
items on shelves in the eight shops: each stands on its board at the size the rule gives it, so the game draws goods at
their shelf's `Scale`. The Lens of Truth shows, grown by 1.4. The Hammer leans in its `Pose_Common`, the end of its
handle on the board and its head up in the niche.
