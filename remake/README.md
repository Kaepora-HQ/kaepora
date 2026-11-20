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

- `places.tsv` names each SoH area that holds a shuffled chest, shop slot, gift, Gold Skulltula or song place. `area`
  is the prefix SoH gives the names of the area's locations, and `label` is the place's label in
  `LocationMsg/Location.msbt`. `says` is what a hint says: the name, with `the` where the game's text writes one before
  it, or the end of the name, as "Ganondorf's Castle" for "Inside Ganondorf's Castle". A test checks that it is.
- `item_names.tsv` names each remake item of `items.tsv`. `label` is in `ActorMsg/PouchContent.msbt`: the game names an
  item by its actor's `ActorNameRef`, which for some items is another actor. `article` is `a`, `an`, `the` or empty.

`chest_hints.tsv` has the sentence a hint about one chest says, for each shuffled chest, in SoH's markup: `#` around
the words in colour, and `[[1]]` where the item goes. Where SoH has the chest and its sentence is true of the remake, it
is SoH's English sentence with the remake's place names. Every other sentence is written in the same style. Each shop
slot that "Shop goods" can shuffle, items 3 and 5 to 8, has a row too, with SoH's one sentence for its shop. So does
each gift, with SoH's sentence for it where that is true of the remake, each Gold Skulltula, each item of
`loose_items.tsv`, and each song place, with SoH's sentence for it. No crate stands by the graveyard's heart piece in
the remake, so its sentence is our own.

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
gives it. One that holds a token drops the game's own. With shop goods as in the game, a skulltula may hold the Bomb
Bag, the Fairy Bow and the Fairy Slingshot: a bag fills whenever it raises Link's limit, whoever gives it, and a
skulltula's Bomb Bag gave 20 bombs to a Link whose limit was 0. The Bow and the Slingshot fill the same way, from rows
every mod changes (below). With shop goods shuffled no skulltula holds anything `KeptFromShops` keeps, these three
among them: the fill places those before skulltulas join it.

`loose_items.tsv` has a row for each item that lies loose in the remake and can be shuffled: the 14 heart pieces, the
8 heart containers the bosses leave, the loose keys of the Bottom of the Well and the Shadow Temple, and the Fire
Arrows the sun over Lake Hylia gives. Each is one placed pickup. Two loose items are left out: Ruto's Letter at the
bottom of the lake has no saved record of its own, and the Gerudo Fortress double cell key stands under two hashes
with its record in its own AI.

- `location`: the SoH location, by SoH's English name for it.
- `hash` and `files`: the pickup's actor hash, and every romfs scene file that places it, comma separated. Ten heart
  pieces stand in both a child's and an adult's file under one hash.
- `item`: what the pickup gives, by the remake's name, and so what it adds to the pool when shuffled.

With "Loose items" at "Shuffled", each row joins the shuffle as a chest does, and its item joins the pool. The two keys
follow "Small and boss keys" as their dungeon's chests do, and a dungeon left out of the shuffle keeps its loose items.
A boss's heart container is a place in its dungeon, as in SoH, so it may hold that dungeon's key, map or compass. A spot
that a seed gives its own item keeps the game's pickup. Any other spot holds the game's own pickup for a heart piece or
a heart container, and for any other item a copy of the game's Hylian Shield pickup that gives it, which Link takes by
touch.

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
with room for no arrows or seeds. The Bomb Bag's own row in `RSDB/PouchActorInfo` gives its bombs: for any row that
lists actors to fill, the item's add raises the limit the row names and fills each listed actor to it (read from the
program). So in every mod the Bow's and the Slingshot's rows list what the small quiver's and bullet bag's rows list,
and the container too, which a shop asks for before it sells arrows or seeds (`DisplayShop.bfevfl`, read). The add
skips all of it when Link's limit is already at or above the row's, so a weapon that comes after a bigger quiver or
bullet bag brings neither its container nor ammunition (read from the program). Seen in the game by an agent, with the
rows changed: one talk in the House of Skulltula gave the Bow and the Slingshot, each with its small container, a limit
of 30 and 30 arrows or seeds, and the Bow shot; a chest's Bow left 30 arrows of 30. That the chest's AI adds the
container before the weapon, so that the weapon's own fill does nothing, is inferred; either order ends the same. The
fill may place the Bow, the Slingshot and the Bomb Bag at a gift, a loose spot or a Gold Skulltula, but for a
skulltula with shop goods shuffled (above).

`songs.tsv` has a row for each of SoH's 12 song locations. Each is a scene that teaches a song with the shared lesson in
`Dm_SY_Ocarina`: the teacher plays an example tune, Link plays its notes, and the scene's gift names the song. With
"Songs" at "Shuffled among themselves", the twelve songs go to the twelve places, as SoH's song locations setting puts
them, and each place teaches the song placed there: its lesson takes that song's melody type and line, and its gift
the song.

- `location`: the SoH location, by SoH's English name for it.
- `song`: the song the place teaches in the game, by the remake's name.
- `melody`: that song's melody type. The shared lesson plays the tune and asks for the notes of the type it is given.
  It adds the song of that type to what Link knows only within its own group: types 0 to 5 and 12 in a lesson of the
  other teachers, the six warp songs, types 6 to 11, in Sheik's (read from `Dm_SY_Ocarina`). A lesson that teaches a
  song of the other group adds it by its gift alone.
- `lesson`: the scene's event flow.
- `examples`: the events that call the lesson's example, comma separated. Each passes the melody type and the line
  shown before the tune, as `MessageId`. The Sun's Song has two, for child and adult Link.
- `follows`: the events that call the lesson's notes, comma separated, each with the melody type.
- `line`: the message the examples show, by its file and label. The line goes with the song to the place that teaches
  it.
- `gift`: the flow and event that give the song with its window, as `flow:event`, by `ActorName`. The Sun's Song's
  gift is in a flow of its own.

Every seed that shuffles the songs also changes two scenes. The windmill man starts his lesson while it has not played,
not while Link does not know the Song of Storms. The Bolero's scene marks its own lesson played where it marked the
Bolero known.

And it holds two ways out of the Desert Colossus back, for both ages, until the Spirit quest is past Step02, which
Link reaches by entering the Wasteland from the Gerudo Fortress: the way into the Wasteland and the Spirit Temple's
front entrance. With the Requiem learned elsewhere, Link can warp to the Colossus before the fortress. Walking out into
the Wasteland and back plays the Wasteland's arrival, which moves the quest to Step04, and the temple's hand chest, Iron
Knuckle and Twinrova move it on too. Past Step01 the fortress's carpenters no longer call for help, no thief comes, and
the card is lost. The rules ask the same: the temple and the Wasteland from the Colossus wait for an adult who has
entered the Wasteland from the fortress.

Seen in the game:

- Impa's lesson with the Minuet of Forest's melody type, line and gift taught the Minuet and not Zelda's Lullaby, also
  to a Link with no ocarina, and her send-off followed.
- The windmill's lesson with Epona's Song taught it to a Link who knew the Song of Storms, and gave no second lesson.
- Sheik's lesson in the crater taught Epona's Song, and the Bolero stayed unknown. No tune played at the end of his
  scene. That none plays for the other songs outside his six is read from the same switch.
- The royal tomb taught the Song of Time, and Zelda's ocarina at the moat taught the Minuet, which then warped Link to
  the Sacred Forest Meadow.
- Malon taught Saria's Song and moved Epona's quest on, and the adult then called Epona with her song and raced Ingo.
- Without the Colossus's change, an adult who had met the carpenters' boss, warped to the Colossus and walked into the
  Wasteland and back found no thief at the fortress. With it, he could enter neither the temple nor the Wasteland, and
  the first carpenter's thief came. Arriving from the Wasteland at Step03, the arrival, Sheik's lesson and the temple
  played as in the unmodified game, which stops the quest at Step04 there too.
- Malon taught the Requiem of Spirit, and it warped a child to the Colossus. Sheik's scene played for him and taught
  Zelda's Lullaby, the temple's doorway and the way into the Wasteland stayed shut, and the Nocturne took him out to
  the graveyard's ledge. An adult who had met the carpenters' boss found the same.

Read from the game's data and not yet seen: Saria's lesson, and Sheik's in the forest, the Ice Cavern, Kakariko and the
Temple of Time.

`rewards.tsv` has a row for each place in the game's event flows that hands over a Spiritual Stone or a medallion: the
scenes after the eight bosses, and Rauru's in the Chamber of Sages, SoH's "Gift from Rauru". Its columns are those of
`gifts.tsv`. The five sages' scenes are written in their `.110` copies, as Dampé's tour is. Each scene's call gives
the item and moves no quest: the blue warp's chain completes the boss's quest by an event of its own, before or after
the scene, and the scene shows the usual stone or medallion as a model of its own (read from the game's flows).
