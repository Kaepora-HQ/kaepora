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

- `places.tsv` names each SoH area that holds a shuffled chest, shop slot, gift or Gold Skulltula. `area` is the prefix
  SoH gives the names of the area's locations, and `label` is the place's label in `LocationMsg/Location.msbt`. `says`
  is what a hint says: the name, with `the` where the game's text writes one before it, or the end of the name, as
  "Ganondorf's Castle" for "Inside Ganondorf's Castle". A test checks that it is.
- `item_names.tsv` names each remake item of `items.tsv`. `label` is in `ActorMsg/PouchContent.msbt`: the game names an
  item by its actor's `ActorNameRef`, which for some items is another actor. `article` is `a`, `an`, `the` or empty.

`chest_hints.tsv` has the sentence a hint about one chest says, for each shuffled chest, in SoH's markup: `#` around
the words in colour, and `[[1]]` where the item goes. Where SoH has the chest and its sentence is true of the remake, it
is SoH's English sentence with the remake's place names. Every other sentence is written in the same style. Each shop
slot that "Shop goods" can shuffle, items 3 and 5 to 8, has a row too, with SoH's one sentence for its shop, and so
does each gift, with SoH's sentence for it where that is true of the remake, and each Gold Skulltula.

`skulltulas.tsv` has a row for each of SoH's 100 Gold Skulltula locations of the vanilla dungeon layouts, each one
remake skulltula. Each scene of the remake holds as many skulltulas as SoH has in the places the scene shows. Each row
was matched by what only that skulltula of its scene has (soft soil, a box, a tree, a grotto, night, the age that sees
it, the only one in a room), then by named neighbours, then by rooms, and by position last.

- `location`: the SoH location, by SoH's English name for it.
- `hash` and `files`: the skulltula's actor hash, and the romfs scene files that place it, comma separated.
- `place`: where the skulltula stands, as an `area` of `places.tsv`, when that is not its location's SoH area, and
  empty otherwise. Hints name it there.
- `pairing`: `certain` or `likely`.
- `note`: what the match rests on. A `likely` row needs one.

With "Gold Skulltulas" at "In dungeons", "In the overworld" or "All", the skulltulas in the ten dungeons that have them,
outside them, or everywhere join the shuffle, as SoH splits them, but for those of a dungeon that "Shuffled dungeons"
leaves out. Each adds a token to the pool, and gives the item the seed places there through a copy of the token that
gives it. One that holds a token drops the game's own. A skulltula never holds the Fairy Bow or the Fairy Slingshot,
which only a chest gives with their quiver or bullet bag (`FilledOnlyByChests`), nor the Bomb Bag, which a token copy
gave without bombs (`KeptFromSkulltulas`). The fill places them before skulltulas join it.

`junk_hints.tsv` has the hints that say nothing about the seed, in SoH's markup, with `@` for the player's name: SoH's
jokes, but for three about SoH's own project, and six of ours.

`prize_lines.tsv` has the lines in which a character names what a place gives. Every seed rewrites each one in our
words, in SoH's markup, with `[[1]]` where the item goes: the item the seed places there, or the place's own item where
the seed keeps it. The game's own line names the unmodified game's item, which would be false.

- `location`: the SoH location, a chest of `chests.tsv`.
- `file` and `label`: the message, in the English text packs. The patcher keeps the control codes the game's message
  opens with, the speaker's voice and face.
- `line_length` and `box_lines`: how much the message's box holds, the widest line in characters and the lines in one
  box, read from the game's own messages at full size in the same file. A test checks that every rewritten line fits.

`character_hints.tsv` has the messages in which a character tells where an item lies, as SoH's hint of the same name
does. A seed writes each one only where the item lies in a place the seed shuffles, so the hint names that place, as
the stones name it. Each sentence is in SoH's markup, with `[[1]]` where the place goes, and keeps the meaning of the
line it replaces.

- `hint`: SoH's location for the hint, by which the spoiler log names it. A stone never names a place a character
  names. With "Kokiri Forest" at "Open" Mido gives no hint: he greets a Link with no sword and no shield as one who has
  both, and never says the lines that carry it (seen in the game).
- `item`: the SoH item, by SoH's English name. The hint names the first location in SoH's order that holds it.
- `file`, `label`, `line_length` and `box_lines`: as in `prize_lines.tsv`, but Mido's 60 characters are not read from
  the game's own lines. His widest line at full size is 53 characters, and a 60-character line of ours was seen in the
  game, whole on one line at normal size inside his box.

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
- A slot's quantity sign shows the count of the bundle it sells, and a slot that sells one item or a rupee shows none:
  in the Bombchu Shop the 3 Deku Nuts read x3 and the Ice Arrows and the Blue Rupee had no sign. The shop sold the Ice
  Arrows from the slot whose sign was taken out.

Read from the game's data and not yet seen:

- A day passing leaves a sold slot sold out, since its count's reset type is 0 where the game's is 2.
- The Bombchu Shop's shopkeeper stands only once Link has the Goron Ruby.
- The Goron Shop refuses a child a tunic by its name: the shop's flow compares the chosen goods with the two tunics,
  then asks whether Link is a child. GC Shop Item 5 still asks for adult Link until that is seen.
- A quantity sign is an actor of its own that the shelf and the shopkeeper link to. The goods tell it to show while
  the slot is on sale and to hide once sold out. With no message it does nothing, so a sign nothing links to would
  show. So the patcher takes a sign out of the scene with every mention of it in the placement file: its links and its
  places in the file's groups. A saved value in the game data list and two records in the scene's baked collision
  still name it, and nothing reads them without the actor.

The fill keeps dungeon items, the Bow, the Slingshot, the Bomb Bag and Bombchus out of shops. A shop may refuse
arrows, seeds or bombs to a Link without the quiver, the bullet bag or the Goron Ruby, and which of these kinds the
four items fall under is unknown.

`shop_goods.tsv` has a row for each item a shop slot can hold, and the generator fails when it starts if one has no row.
A slot can hold what a shuffled chest or a gift holds, but for the dungeon items and the seven `KeptFromShops` keeps
out, the token a shuffled Gold Skulltula adds, and the shop filler. Four items are sold as the game's own goods, named
in `goods`. Every other item is sold from a goods actor of its own, which shows the item's own model. The game's own
goods rest the lowest point of their shop pose between 26 mm under their shelf's origin (the Hylian Shield) and 34 mm
over it (the heart), and the patcher raises each shelf so that its item rests on the origin. It also sizes the item as
the game sizes its own goods, whose shop poses shrink some and grow others: an item taller or reaching farther than the
game's goods shrinks to the largest size that fits them, and one smaller than the smallest of them grows toward its
size, by at most the 1.4 the game grows its masks.

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

`gifts.tsv` has a row for each place in the game's event flows that gives the item of one of 21 SoH locations: the five
House of Skulltula rewards, and the heart pieces, bottles and scales that minigames and characters give. With "Gifts" at
"Shuffled" the 21 join the shuffle. Each adds to the pool what it gives in the game, and the plan writes the placed
item at each of its places.

- `location`: the SoH location, by SoH's English name for it.
- `flow`: the romfs path of the event flow.
- `event`: the event whose call gives the item, by its name, which no other event of the flow has.
- `param`: the call's string parameter that names the item.
- `item`: what the call gives in the unmodified game, by its remake name. Every row of a location names the same item.

A location with two rows gives its item from two events of one flow, such as the frogs' game on a first try and on a
retry, and both give the placed item. Dampé's tour names the `.110` copy of its flow, the copy the game reads: with the
two copies saying different lines, Dampé said the `.110` copy's. After the 20-token reward the house opens the Stone of
Agony's guide whatever the reward gave, so the plan also points that question at the flag the guide sets once shown,
and the guide does not open there. The flow asks that flag once more after the 100-token reward, and the guide opens
then, as no earlier talk has set it (read from the flow).

Seen in the game:

- One talk in the House of Skulltula hands over all five rewards, each another item. One such talk gave the Giant's
  Wallet, the Golden Scale, the Adult's Wallet, the Silver Scale and a Red Rupee; another gave four Empty Bottles, and
  Ruto's Letter then went into a fifth bottle.
- The man on the roof, the lab's dive and the Skull Kid give another item. The lab's dive gave the Boomerang to a child
  and to an adult. Three more gifts give through the Skull Kid's call.
- Dampé's tour showed the Piece of Heart's model in the hole, and taking it gave the seed's Blue Rupee and no Piece of
  Heart.
- The Big Poe buyer's first prize sale gave the Iron Boots and the next sale only rupees. Horseback archery gave the
  Megaton Hammer for 770 points. The frogs' game gave the Boomerang when won on the retry.
- A chest gives Lon Lon Milk to a Link with no bottle, the Echo Gem, the Giant's Wallet and the Silver Scale.

So every way the 21 hand over their item has given another item. Read from the game's files and not yet seen: a prize
talk may still name the usual prize, and the map may keep a heart on these places until each is won.

A shop refuses bottle contents to a Link with no empty bottle, and which kind the Empty Bottle and Lon Lon Milk fall
under is unknown, so the fill keeps both bottles out of shops, and 20 Bombchus with the other Bombchus. The scales, the
wallets and the Echo Gem may go to a shop slot, each with its row in `shop_goods.tsv`. The Kokiri Shop sold the
Adult's Wallet and the Silver Scale, and they left a wallet of 500 and a dive depth of 6.0; that build had no shelf
placement, and none of these has been seen on a shelf where `shop_goods.tsv` places it.

A chest that gives the Fairy Bow or the Fairy Slingshot also adds the quiver or the bullet bag and 30 arrows or 30
seeds, from the chest's own AI (`AI/TreasureBox.module.ainb`, read from the files); a chest's Fairy Bow left 30 arrows.
One talk in the House of Skulltula gave the Bomb Bag, the Fairy Bow and the Fairy Slingshot to a Link with none: the
Bomb Bag came with 20 bombs and room for 20, and the Bow and the Slingshot came with no quiver and no bullet bag, so
with room for no arrows or seeds. So with gifts shuffled the fill puts the Bow and the Slingshot in chests before gifts
join it (`FilledOnlyByChests`), and the Bomb Bag may sit at a gift.
