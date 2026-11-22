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

`chest_hints.tsv` has the sentence a hint about one chest says, for each shuffled chest, in SoH's markup: `#` around the
words in colour, and `[[1]]` where the item goes. Where SoH has the chest and its sentence is true of the remake, it is
SoH's English sentence with the remake's place names. Every other sentence is written in the same style. Each shop slot
that "Shop goods" can shuffle has a row too, with SoH's one sentence for its shop: items 1 and 3 to 8, and in the Kokiri
Shop, which keeps its Deku Shield and its Deku Stick, items 2, 3 and 5 to 8. So does each gift, with SoH's sentence for
it where that is true of the remake, each Gold Skulltula, each item of `loose_items.tsv`, and each song place, with
SoH's sentence for it. No crate stands by the graveyard's heart piece in the remake, so its sentence is our own.

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

`prize_lines.tsv` has the lines in which a character names what a place gives. A seed that shuffles the place rewrites
each one in our words, in SoH's markup, with `[[1]]` where the item goes: the item the seed places there. The game's own
line names the unmodified game's item, which is true only where the seed keeps it.

- `location`: the SoH location, a chest of `chests.tsv` or a gift of `gifts.tsv`.
- `file` and `label`: the message, in the English text packs. The patcher keeps the control codes the game's message
  opens with, the speaker's voice and face.
- `copy_of` and `shown_at`: for a line under a label the file lacks, the message it is a copy of, and the
  `flow:event:parameter` that shows it. Every language's pack gets the copy: the English ones with our words after the
  copied message's control codes, the others with the copied message whole. The plan points the event at the copy. The
  crater's scrub and the field's show one offer, so the crater's offer is a copy.
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

`gifts.tsv` has a row for each place in the game's event flows that gives the item of one of 38 SoH locations: the five
House of Skulltula rewards, the heart pieces, bottles and scales that minigames and characters give, the eight prizes
that make a quiver, bullet bag, bomb bag, stick bag or nut bag bigger, three places where the story moves (the scene
after the last carpenter in the Gerudo Fortress, King Zora's thanks once he is thawed, and Zelda's scene in the Temple
of Time), the two eggs, and four Business Scrubs that sell once: the stick bag by the Lost Woods bridge, the nut bag in
the Lost Woods grotto, and a Piece of Heart in a Hyrule Field grotto and in the crater. Each group ticked in "Gifts"
puts its locations in the shuffle. Each adds its item to the pool, and the plan writes the placed item at each of its
places. A location of a group left out keeps what the game gives, and the plan names none of its places.

- `location`: the SoH location, by SoH's English name for it.
- `flow`: the romfs path of the event flow.
- `event`: the event whose call gives the item, by its name, which no other event of the flow has. A heart scrub's
  sale also names its item in the string `Shop_SelectItemName`, which the program reads for the screen's recovery
  preview, so that event has a row too.
- `param`: the event's string parameter that names the item.
- `gives`: what the call gives in the unmodified game, by its remake name.
- `item`: what the location adds to the pool, by its remake name. Every row of a location names the same item.
- `group`: the group of "Gifts" that shuffles the location, by its name in a seed's settings. Every row of a location
  names the same group. Released seeds' "Shuffled" names the four groups of its time, so it keeps the eggs and the Deku
  Scrubs.

A bag prize or sale gives the bigger of two sizes from one event and the biggest from another: the biggest only to a
Link who already holds the bigger (read from the flows). It adds one size to the pool: the Market and Kakariko
galleries, the bowling alley and the Deku theater add the bigger bag, and the Lost Woods target, horseback archery, the
rolling Goron and the two bag scrubs the biggest. The places of one bag share a group: a kept place gives by what Link
holds and a shuffled one gives one size, so with the theater shuffled and the scrubs kept, a Link who bought from a
scrub first would get the bigger bag twice and never the biggest. No rule of SoH's logic reads what these ten items set:
their logic values, and for the stick and nut bags the inventory slot of sticks or nuts (`logic.cpp:2317`). So a bigger
quiver or bullet bag is no Bow or Slingshot to the logic, and the logic makes the rolling Goron wait for the Bomb Bag
itself. That is stricter than the game, where a bigger bomb bag brings the small one he asks for. No shelf has been seen
to sell a bag, so the fill keeps them out of shops as it keeps the Bomb Bag. For a place whose group is left out, the
logic counts what SoH's location list says the place gives, so a row's item must be that, and the list gives the ten
places their bags, the 40-token reward its 20 Bombchus, and the scrubs the remake's goods and prices.

Adult Anju gives the Pocket Egg in Kakariko. While "Biggoron Quest" shuffles her place, the egg is an item of the pool,
and her place holds whatever the seed puts there:

- Her talk writes StartSubEvent of the trade quest, whatever she gives, and her offer asks nothing of the egg: she
  offered as usual to a Link who held one and gave her place's item (seen).
- The egg's timer in the program runs only for a Link whose save says he found the egg, at that step, and hatches it
  at the next dawn (read from the program). Nothing else starts the hatch. So the Pocket Cucco waits for the egg found
  and her talk, in either order, and adult Talon and her trade for Cojiro wait for the egg as SoH's rules have them.
  Seen: a Pocket Egg from a chest set its record and hatched into the Pocket Cucco, taking the egg from the pouch,
  found after her talk and found before it.
- A child who finds the egg keeps it until the adult's talk (read).
- The egg goes only to a chest, a gift or a reward's place, as the Gerudo's card does: no copy of it on the ground or on
  a Gold Skulltula, and no shelf, has been seen to give one. A chest gave one (seen); a gift's call or a reward's place
  gives it through the same pouch add (read).

Malon gives the Weird Egg at the castle. While "Malon's Egg" shuffles her place, the egg is an item of the pool, and
her place holds whatever the seed puts there:

- Her talk writes Step03 of the castle quest, whatever she gives. The egg's timer in the program runs only for a Link
  whose save says he found the egg, at that step, and hatches it wherever he is (read from the program). Seen: a Weird
  Egg from a chest set its record, and the timer hatched the Chicken on the walk to the castle, around the Market. Both
  hatches seen fit a timer of about 90 seconds: it stood at 90.018 where it hatched the egg and at 78.648 where the area
  did first (inferred from the two saves). Eight areas of the yard behind the gate, where Link sneaks past the guards,
  hold the timer while he stands in one: `HyruleCastle_HatchTimerStopAreaPrefab_8c10` sets
  `HyruleCastle_StopWeirdEggHatchTimer`, which the program asks before the hatch (read). The gate guard's way leads into
  that yard and around the vine area, so a Link who pays him gets the Chicken from the timer once he has left the yard
  (inferred).
- At Step03 an area at the top of the vine climb, by the gossip stone, also hatches the Chicken when Link walks in
  (`Area_73af`, which `HyruleCastle_Prefab_ee1b` watches), for any Link: seen with no egg held. So a mod that shuffles
  her place gives that area a presence on `IsGet.Item_WeirdEgg`, and it stands only for a Link who found the egg. The
  game gives an area of Hyrule Field a presence of that kind, on `IsGet.Item_Hookshot`. Seen: with the presence and no
  egg, two climbs gave no Chicken; an egg from Malon's own hand, inside the grounds, hatched at the top of the climb at
  once.
- So Talon at the castle wakes for a Link who found the egg and talked to Malon, in either order. The gate guard, who
  opens the gate for 10 rupees to a Link whose save says he got the egg, asks for the egg in the rules too.
- The moat's two places, the Ocarina of Time and its song, wait for Zelda met, and the way to Zelda climbs past Talon,
  so a seed that shuffles her place needs the egg early, as a child.
- The egg goes only to a chest, a gift or a reward's place, as the Pocket Egg does. A chest gave one (seen).

Each egg's caption still says who gave it ("Malon gave you this egg", "The cucco lady gave you this special egg").

A Business Scrub that sells once names its item in a call to the one-time sale of `Enemy_Scrub_Business_Common`. The
sale charges the call's price, 40 rupees at the bridge and the grotto and 100 at the field and the crater, adds the item
through `Dm_SY_GetInEvent:GetInEvent_NotTurn_2DShop`, the add a gift's call reaches, and then sets the scrub's own saved
flag, and the scrub is gone for good (seen). Its AI module creates it only while that flag is false. The flow asks
nothing of the item before the sale, and the bridge scrub's two questions of what Link holds only pick which of its
three calls gives (read from the flows and the modules). The field's scrub sells only to child Link, and only once Link
has read the password on the Fairy Thought outside, which only the adult field holds; to an adult its talk says a fairy
bought what it sold (read). An adult buys at the bridge, from the Lost Woods' copy of the scrub (seen). The sale is
talk, which no cutscene setting skips.

A location with two or three rows gives its item from that many events of one flow, such as the frogs' game on a first
try and on a retry, and both give the placed item. Dampé's tour names the `.110` copy of its flow, the copy the game
reads: with the two copies saying different lines, Dampé said the `.110` copy's. After the 20-token reward the house
opens the Stone of Agony's guide whatever the reward gave, so the plan also points that question at the flag the guide
sets once shown, and the guide does not open there. The flow asks that flag once more after the 100-token reward, and
the guide opens then, as no earlier talk has set it (read from the flow).

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
- The Lost Woods target's scrub gave a placed Boomerang. He held the seed bag's model while the Boomerang's own get
  scene played, and the Boomerang was kept after a reload. The rolling Goron gave a placed Boomerang, and his scene
  played out. The other six bag prizes hand over through the same entries as gifts seen above (read from the flows).
- With the vine area as the game has it, Malon gave the Bomb Bag, and the Chicken hatched at the top of the climb with
  no egg held and woke Talon. For a Link with no record of the egg no way to Talon passes that area, as the gate guard
  opens his gate only for one with the record.
- With the vine area's presence, Malon gave another item and two climbs with no egg gave no Chicken. A Weird Egg from
  Mido's chest set its record, and the timer hatched the Chicken around the Market on the walk back, with the area's
  event not played; Talon woke and the castle quest reached Step04. A Weird Egg from Malon's own hand hatched at the top
  of the climb at once, the area's event played; Talon woke.
- Adult Anju gave an Empty Bottle. With no record of the Pocket Egg, no Pocket Cucco came over a couple of days. With
  `IsGet.Item_PocketEgg` set by hand and no egg held, the egg hatched at dawn and the Pocket Cucco woke Talon.
- A Pocket Egg from Kakariko's open grotto chest set its record and hatched into the Pocket Cucco, and Talon's talk
  moved the trade quest to Step01_01. Found before her talk, it was held when she offered as usual and gave her place's
  item, a Red Rupee, and one egg was held; it hatched as well.
- The bridge's scrub sold a placed Piece of Heart for 40 rupees once, and no scrub stood on either side of the bridge
  afterwards, nor after a load. From the same save it sold a placed Zora's Sapphire, which showed in the menu, and the
  Water quest's step stayed at Ready. The crater's scrub offered a placed Boomerang for 100 rupees under the crater's
  own label, with nothing of a heart in the screen's preview, sold it to a child once and was gone after a load. Each
  sale took its price and set the item's record and the scrub's own flag.

So every way the 29, Malon, adult Anju and the Business Scrubs that sell once hand over their item has given another
item. Read from the game's files and
not yet seen: a prize talk may still name the usual prize, and the map may keep a heart on these places until each is
won.

The three story places write their quest's step whatever they give: the carpenters' scene moves the Spirit quest to
Step02, King Zora's talk the Water quest to Step05, and Zelda's scene the AllSage quest to Complete and the Ganon quest
to Step01 (read from the flows). The fortress's guards, the Wasteland's gate keeper and the archery range follow the
Spirit quest, the Training Ground's keeper asks for 30 rupees and no card, and the bridge to Ganon's Castle follows the
Ganon quest, so the rules ask for the scenes, not the Gerudo's card or the Light Arrows. Of the game's flows and logic
modules, one reads the card: a module that takes two of the fortress's sneak areas away. The program also reads it,
inferred to be for the fortress's key counter on the screen, and a map picture's table picks its light by it (read).

Seen in the game: the scene after the fourth carpenter gave a placed Zora Tunic, the Spirit quest then stood at Step02
with no card held, no guard caught Link, the gate keeper opened the gate to the Wasteland, and walking into the
Wasteland wrote Step03. With the pass already found, the rescue played the same and gave its item once, and Link still
held one pass. King Zora gives through horseback archery's call and Zelda through the reward scenes', and each of those
has given another item (seen); their own scenes have not. Zelda's scene still shows the Light Arrows: two of its events
pick the fanfare and the sparkle by the arrows' name, and three more bind the arrows' model, set its detail and turn it
(read). Its window names the seed's item.

In the game King Zora's ice stands only while the Water Sage's quest is not complete, and his thanks come only after the
thaw (read from the placement files and his talk). A Link who finished the Water Temple first would never get what King
Zora hands over: his talk then sends a Link with no Zora Tunic to the Zora shop's keeper, who gives one, whatever King
Zora's place holds. Once that quest is complete an adult sees a domain of its own, `ZoraDomain_Adult_Alternative`, whose
ice has a presence on the quest complete, negated, so it never stands there. Every plan points that presence at the
thaw's played flag, `HavePlayedEvent.SageOfWater_KingZora_IsMelted_Ice`, still negated. Seen in the game: after the
temple, a King Zora never thawed stands in the ice, and Blue Fire thaws him. His scene gives the seed's item, and after
a save and a load the ice is gone and his talk gives nothing. The thaw's request has three inputs: the ice hit by Blue
Fire, the thaw's scene not yet played, and the saved Bool `SageOfWater_KingZora_RedIce` false. That Bool starts false,
and no game file writes it (read). The decoded program holds neither its name nor its hash (measured). Seen in the game,
in the game's own order: a King Zora thawed before the temple stands in no ice after it. The thawed domain's own ice key
stays clear in that order, so the presence alone keeps the ice away (read from the saves). After the temple Link walks
and climbs through Zora's Fountain to the Ice Cavern and fills a bottle with Blue Fire.

The Gerudo's card is a key item of the pouch, as the stones are, so it goes to a chest, a gift or a reward's place only.
A shop asks whether Link is a child before it sells either tunic (`DisplayShop.bfevfl`, read; both tunic shops refused a
child, seen), and a slot's rule asks for no age, so no shop sells the Zora Tunic. The Light Arrows may go wherever the
Fire and Ice Arrows go. Seen in the game: a chest gave the pass; a chest, a Gold Skulltula and a loose spot gave the
Zora Tunic; a Gold Skulltula and a Kokiri Shop shelf gave the Light Arrows, the shelf to a child. A seed that starts
with the Zora Tunic leaves King Zora's tunic out of the pool, with a Red Rupee in its stead.

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
and the container too, which a shop asks for before it sells arrows or seeds (`DisplayShop.bfevfl`, read). The add skips
all of it when Link's limit is already at or above the row's, so a weapon that comes after a bigger quiver or bullet bag
brings neither its container nor ammunition (read from the program). So in every mod each medium and large quiver,
bullet bag, bomb bag, stick bag and nut bag also lists the small one of its kind, after its ammunition: one that comes
first brings the small one. Shops ask for the small quiver or bullet bag before they sell arrows or seeds, and the
rolling Goron for the small Bomb Bag; nothing in the game's files reads the small stick or nut bag (read). Seen in the
game, with the rows changed: a large quiver given to an adult with no Bow brought the small quiver, and the save held 50
arrows and a limit of 50 while the screen showed 0/50 with no Bow. The Bow after it, from a chest and from a gift, left
the 50 arrows and the limit at 50. A bigger quiver given to a Link with the Bow and 17 of 30 arrows left a limit of 40
and 40 arrows, with the Bow and both quivers held. In the same talk, a bigger bomb bag and then the Bomb Bag left 30 of
30 bombs, with both bags held. Seen in the game by an agent, with the weapon rows changed: one talk in the House of
Skulltula gave the Bow and the Slingshot, each with its small container, a limit of 30 and 30 arrows or seeds, and the
Bow shot; a chest's Bow left 30 arrows of 30. That the chest's AI adds the container before the weapon, so that the
weapon's own fill does nothing, is inferred; either order ends the same. The fill may place the Bow, the Slingshot and
the Bomb Bag at a gift, a loose spot or a Gold Skulltula, but for a skulltula with shop goods shuffled (above).

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
`gifts.tsv` but `group`. The five sages' scenes are written in their `.110` copies, as Dampé's tour is. Each scene's
call gives the item and moves no quest: the blue warp's chain completes the boss's quest by an event of its own, before
or after the scene, and the scene shows the usual stone or medallion as a model of its own (read from the game's flows).

With "Dungeon rewards" at "Shuffled" the nine join the shuffle, each written as a gift is, and the nine rewards join the
pool. A dungeon left out of "Shuffled dungeons" keeps its boss's reward. Seen in the game by an agent: a chest gave the
Kokiri Emerald, the Forest Medallion and the Zora Sapphire, each with its model, Link's hold-up pose and its named
window. A stone's pack names `AS/Dummy`, no file, and a medallion's `AS/DemoDummy`, which only cutscene props share, and
a medallion's pack has no sound link (read from the files); none of that showed in the chest's give. A gift raises an
item with the same pose (`Dm_SY_GetInEvent`, read), so a reward at a gift is inferred. No shop slot, Gold Skulltula or
loose spot has been seen to give one, so the fill places the rewards before those join it (`KeptInChestsAndCalls`). A
reward opens nothing in the remake, so it is no major item: no hint names one as an item to find, and none keeps its
area from being called foolish. The place after a boss, or Rauru's, keeps its area from being foolish whatever it holds.
