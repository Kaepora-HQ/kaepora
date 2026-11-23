# Remake tables

The generator builds these into the program and checks them when it starts, all but the quest tables, which no program
reads. The patcher builds in `stones.tsv` and `shop_goods.tsv` too. With the game file, `tests/remake_tables_test.py`
checks the tables against the game's own files.

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
that "Shop goods" can shuffle has a row too, with SoH's one sentence for its shop: items 1 and 3 to 8. So does each
gift, with SoH's sentence for it where that is true of the remake, each Gold Skulltula, each item of
`loose_items.tsv`, and each song place, with SoH's sentence for it. No crate stands by the graveyard's heart piece in
the remake, so its sentence is our own. No hint names a slot that sells the game's goods, as in SoH. A stone calls no
place foolish whose kept slot sells goods the seed cannot be finished without.

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
every mod changes (below). With shop slots shuffled no skulltula holds anything `KeptFromShops` keeps, these three
among them: the fill places those before skulltulas join it.

`loose_items.tsv` has a row for each item that lies loose in the remake and can be shuffled: the 14 heart pieces, the
8 heart containers the bosses leave, the loose keys of the Bottom of the Well and the Shadow Temple, the Fire Arrows
the sun over Lake Hylia gives, and Ruto's Letter at the bottom of the lake. Each is one placed pickup. Five loose items
are left out: the heart piece in Goron City's spinning pot, which gave two rupees and no other item when its pot named
one (seen in the game), and the four Gerudo Fortress keys, which need a key that counts for the fortress wherever it
is found.

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

A taken pickup stays taken by the key its spot has in the game data list. Ruto's Letter's spot has none: the lake's
logic module makes the letter until the letter's quest flag is set, and only the game's own letter sets it. The two
keys' spots have none either, and their rooms set a flag when the key is taken. So the patcher gives each planned
loose spot with no key one. Seen in the game: a copy at the letter's spot was taken once, with the spot's key set, and
the spot was empty afterwards; the game's own Piece of Heart and Heart Container there were each taken under water with
its own get, setting the spot's key. With the spot shuffled, Ruto's Letter lies in a chest (`KeptInChests`): King Zora
took a chest's letter and moved aside (seen), and whether he takes one that a gift, a shop, a Gold Skulltula or a copy
gave is unseen. The game names the letter a Bottle, so a hint does too.

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
- `sent_on`: for a line the game says only once, the `flow:event:next` that brings it on every talk: the plan sends
  the event on to the next whatever it asks. Mido says `Talk_26`, his line for a Link with a shield and no sword,
  until he has seen the shield (`KokiriForest_Npc_Kokiri_Mido_Child_Talk_02`). After that his question (`Event64`)
  picks `Talk_18` or `Talk_19`, lines that a Link with the sword and no shield hears too. So the question goes on, on
  either answer, to the game's own write of that flag (`Event54`), whose only next is `Talk_26` (`Event41`). The flag
  is then written at the first such talk, as in the game: his question for a Link who wears both reads it, and so does
  his balloon line, through the saved expression `KokiriForest_Npc_Kokiri_Blocker_Surprised_BoolExp`. A Link he met
  before without a shield no longer hears `Talk_16`, his surprise at the shield.

`shops.tsv` has a row for each of SoH's 64 shop locations, by shop and then by item number. Each row is one slot of a
remake shop. With "Shop goods" at "Four per shop shuffled", items 5 to 8 of each shop are shuffled, the four that SoH's
shopsanity takes at that count. Each shuffled slot adds a Red Rupee to the pool and takes one of SoH's cheap balanced
prices, 0 to 95 rupees. Every other slot sells goods from SoH's list in `kept_goods.tsv` (below), and at "Goods shuffled
among the shops" every slot does.

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
- A slot sells another shop's goods again and again: the Zora Shop sold a Deku Stick twice to a child who had never
  held one, raising his limit from 0 to 10.
- The Kokiri Shop sold the Goron Tunic at 600 rupees to a child and to an adult, once each.
- The Goron Shop's kept slot sold a child the Deku Shield at 40 rupees, and refused him a second one while he held
  it.
- A Kokiri Shop slot sells from the start once the patcher lifts its gate on meeting the Deku Tree, from the shopkeeper
  who stands until then too.
- A child can buy an adult's item: the Market Bazaar sold him the Silver Gauntlets.
- A slot's quantity sign shows the count of the bundle it sells, and a slot that sells one item or a rupee shows none:
  in the Bombchu Shop the 3 Deku Nuts read x3 and the Ice Arrows and the Blue Rupee had no sign. The shop sold the Ice
  Arrows from the slot whose sign was taken out.

Read from the game's data and not yet seen:

- A day passing leaves a sold slot sold out, since its count's reset type is 0 where the game's is 2.
- The Bombchu Shop's shopkeeper stands only once Link has the Goron Ruby.
- The Goron Shop and the Zora Shop refuse a child a tunic by its name: each shop's flow compares the chosen goods
  with the two tunics, then asks whether Link is a child. So the logic asks for adult Link wherever either shop sells
  a tunic. The potion shops ask for Blue Fire by name the same way.
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

`kept_goods.tsv` has a row for each of the game's goods in SoH's list of what a seed keeps on sale, which
`GetMinVanillaShopItems` holds: 64 goods, some of them more than once. The count picks which goods stay on sale: the
first 64 less one for each shuffled slot. The fill picks their slots, among those the count leaves, before it places
the shuffled items, as SoH's does, so a slot may sell another shop's goods. SoH's list is not the game's own 64: ten
kinds of the game's goods are on it nowhere (10 Deku Seeds, Five Deku Sticks, the Magic Jar, Half-Full Lon Lon Milk,
and six Bombchu packs but 10 and 20), and some goods stand on it more often than in the game. The generator fails
when it starts if one of SoH's goods has no row. The spoiler log names each kept slot's goods by SoH's name without the
price in brackets that SoH tells some apart by, with the price the slot asks.

The patcher writes each kept slot as it writes a shuffled one, but with the goods actor at offset 0 and no limit to its
count, so it sells again and again, and it leaves the game data list as it is. So a kept slot:

- asks the goods' base price. Three of the game's own slots ask less, which a kept slot does not: the Goron Shop's
  Five Bombs at 25 and the Zora Shop's fish at 30, by their slots' offsets, and the Market Bazaar's Hylian Shield, 20
  off once its gate opens.
- loses the slot's gates, as a shuffled slot does: the Kokiri Shop's wait for meeting the Deku Tree, and the sold-out
  gates of the Market Bazaar's item 2, which waits for the Goron Ruby, and of both potion shops' item 2, which wait for
  Blue Fire. The shop's own checks by kind and by name still refuse bombs before the Goron Ruby and a first Blue Fire
  in a potion shop, so such a shelf can show goods the shopkeeper does not sell yet.
- shows how many a pack holds only where the game's shelf has a sign. 73 of the 120 shelves have none.

- `item`: the SoH item, by SoH's English name for it.
- `goods`: the remake's goods actor that sells it.
- `price`: what the remake asks for those goods, their `BuyingPrice` in `PouchActorInfo`. A test checks that it is.
  SoH's three Red Potions, at 30, 40 and 50 rupees, are all the remake's one Red Potion at 30, and its two 5 Bombs are
  the remake's one at 35.

`gifts.tsv` has a row for each place in the game's event flows that gives the item of one of 47 SoH locations: the five
House of Skulltula rewards, the heart pieces, bottles and scales that minigames and characters give, the eight prizes
that make a quiver, bullet bag, bomb bag, stick bag or nut bag bigger, five places where the story moves (the scene
after the last carpenter in the Gerudo Fortress, Darunia's dance, his son's talk once an adult stops him rolling, King
Zora's thanks once he is thawed, and Zelda's scene in the Temple of Time), the two eggs, Biggoron's sword, four Business
Scrubs that sell once (the stick bag by the Lost Woods bridge, the nut bag in the Lost Woods grotto, and a Piece of
Heart in a Hyrule Field grotto and in the crater), and the gifts of the six Great Fairies. Each group ticked in "Gifts"
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
  names the same group. Released seeds' "Shuffled" names the four groups of its time, so it keeps the eggs, the Deku
  Scrubs and the Great Fairies.
- `place`: where the place stands, as an `area` of `places.tsv`, when that is not its location's SoH area, and empty
  otherwise. Hints name it there. SoH files the Great Fairy outside Ganon's Castle under Hyrule Castle; her mark stands
  in the scene the game names Ganondorf's Castle, where `skulltulas.tsv` places that scene's skulltula too.

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

Adult Anju gives the Pocket Egg in Kakariko, and Biggoron, on the summit, the Biggoron Sword for the Claim Check, the
game's Biggoron Receipt. "Biggoron Quest" shuffles both places as SoH shuffles them with its adult trade off: the
Claim Check and the sword are items of the pool and the Pocket Egg is not, as SoH's pool has them and its location list
gives Anju's place the Claim Check. Each place holds whatever the seed puts there. The chain's places leave the seed, so
only the pool's Claim Check counts for Biggoron's place, an adult with the receipt as SoH's rule asks. Only Anju's offer
gives a Pocket Egg (read), so a seed with the group ticked has none, and the chain does not start.

- Anju's offer stands while the trade quest is at Ready: her schedule's first entry and her talk ask it (read). Her
  offer asks nothing of the egg.
- Biggoron takes a shown Claim Check at every step of the quest (read). His hand-over asks whether the quest is
  complete, then takes a held Giant's Knife or broken knife, then asks whether 72 game hours have passed since the
  eye-drops scene wrote `GetSwordBigGoron_ElapsedTime`, gives, and writes the quest complete. With the quest complete
  Anju offers nothing, so a receipt shown before her talk would lose her place. A mod that shuffles his place writes no
  step: his hand-over goes on from its last line to the write of his talk flag in his talk after the quest, which now
  records a claim, `Kaepora_Once_14299450766367161284`, and his question at the hand-over whether the quest is complete
  (`Event49`) asks `Kaepora_Once_14299450766367161284_Exp`, an expression of the mod's own that reads the claim. His
  plain talk asks the same question (`Event238`) only past Step10, which no seed of the group reaches (read), so it
  keeps the game's question. An expression query reads no Bool (read in the program, seen in look DA1). So he gives
  once, in any order with Anju's talk (seen in both orders), as SoH's Biggoron gives once on a flag of its own while its
  trade never completes.
- His wait's question (`QueryGameDataCheckElapsedTime`) is false while the record is 0. Otherwise it subtracts the
  record from the world clock and passes once the difference reaches 72 × 60 (read in the program). In the game only the
  eye-drops scene writes the record, with the clock's value at that moment, so three days are the clock passing that
  moment by 4,320. The clock counts game minutes (inferred) and is saved as the UInt `0x3f8d9d86`, and a game counts on
  from the saved value (seen: 5000 became 5044). It is the world's clock, not the quest's, and it stood still while a
  child stayed in Kokiri Forest (measured from saves). The mod starts the record at 1, so a receipt found before any
  eye-drops scene gives once the clock reaches 4,321: three game days after a new game begins, SoH's forge time. Only a
  new game takes a start value; a save made before the mod keeps the record at 0, and his wait never ends there (seen).
  A new game under the mod took it: its first save held the record at 1 and the clock at 0 (seen).
- His hand-over takes no knife with the receipt, and the get flow's entry for his sword, which only his hand-over calls,
  no longer waits for the Biggoron Sword to be equipped, a wait that would never end with another item and no Master
  Sword equipped (read).
- Medigoron sells his knife only to a Link who holds no Biggoron Sword (`Npc_Goron_Medigoron_Adult` `Event19`), and the
  mod keeps that refusal. His place is not shuffled, and the logic counts the knife as held by a Link with the sword.
  Seen in a mod that sent his question on to the sale: a Link holding the sword paid 200 rupees for the Giant's Knife,
  which became his equipped weapon but did nothing for him and did not replace the Biggoron Sword.
- The Claim Check and the sword go only to a chest, a gift or a reward's place, as the eggs do. A chest's
  `Item_Sword_Biggoron` gives `Weapon_Sword_Biggoron`, an adult's two-handed sword, through the equipment its pouch row
  names, as a chest's `Item_Sword_Kokiri` gives the Kokiri Sword (read; seen: an adult equipped and swung it, and a
  child holds it but cannot use it, as in the game). No shop row names the sword: the remake has no
  `Shop_Sword_Biggoron`.

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
- The egg goes only to a chest, a gift or a reward's place. A chest gave one (seen).

The egg's caption still says who gave it ("Malon gave you this egg").

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
- The bridge's scrub sold a placed Piece of Heart for 40 rupees once, and no scrub stood on either side of the bridge
  afterwards, nor after a load. From the same save it sold a placed Zora's Sapphire, which showed in the menu, and the
  Water quest's step stayed at Ready. The crater's scrub offered a placed Boomerang for 100 rupees under the crater's
  own label, with nothing of a heart in the screen's preview, sold it to a child once and was gone after a load. Each
  sale took its price and set the item's record and the scrub's own flag.

So every way the 29, Malon, adult Anju and the Business Scrubs that sell once hand over their item has given another
item. Read from the game's files and not yet seen: a prize talk may still name the usual prize, and the map may keep a
heart on these places until each is won.

A Great Fairy gives her item once Link has paid her offering. On a return visit she asks whether he has got her own
item, and sells it again if not (read from her flow). `fountain_claims.tsv` has a row for each of the six: by `location`
and `flow` as in `gifts.tsv`, the event of her paid path that writes `GreatFairy_AlreadyAppeared` (`paid`), the event
that asks on a return (`question`), and a saved Bool of the mod's own (`claim`), named `Kaepora_Once_` and the lowest
placement hash of her Triforce mark. The plan points both events' `GameDataName` at the Bool and lists it, and the
patcher adds it to the game data list. So she gives the placed item once, whatever it is. With "Great Fairies" left out
of "Gifts" the plan names neither, and the list and the fountains stay the game's. Her paid path writes
`GreatFairy_AlreadyAppeared` after her gift, and no path reaches that write without her `Pay` call; the healing all six
share writes it again, at the summit flow's `Event140` (read from the flows). Nothing reads it: no game file but the six
flows names it, and the program holds neither its name nor its hash as a constant (measured).

Seen in the game, with cutscenes skipped: the castle's Great Fairy gave a placed Boomerang once, took nothing after that
first paid visit and healed Link on every return; she gave a Deku Shield to a Link who held one, and a fourth Piece of
Heart, which added its heart. The summit's gave a placed Boomerang to a Link with a meter and to a Link with none. The
crater's and the one outside Ganon's Castle each gave a placed Boomerang through her own entry, and her scene ended.
Each save held her claim true and her own item not got. The game took a list grown by one claim, and the list of a whole
seed's mod, with all six claims and the loose spots' keys.

The summit's Great Fairy gives through her own entry of `Dm_SY_GetInEvent`. For a Link who had no meter, which her flow
marks in `GreatFairy_HasNoMPOnDinDemo` before the gift, the entry reserves the meter's birth and plays it, asking that
mark before each (`Event555`, `Event554`). The birth ends only when its animation reaches its last frame (read from the
program). With an item that brings no meter there is none to be born, so the plan points both questions at her claim,
false until her paid path writes it, and the entry passes the scene. Seen in the game: given the Boomerang, she gave it
to a Link with no meter, no meter appeared, and nothing waited.

The fill keeps the six fairy items, Din's Fire, Farore's Wind, Nayru's Love, the gift of halved damage and the two magic
blessings, in chests, as it keeps Ruto's Letter (`KeptInChests`). Seen in the game, from chests: Farore's Wind brought
the meter, the two magic blessings set their records and doubled it, and the gift of halved damage set halved damage.
The castle's and the crater's blessings stand for SoH's Progressive Magic Meter, and either found first brings the meter
(measured from the program).

With "Cutscenes" at "Skipped" the skip ends only a fairy's appearance. Her gift comes after that window on both of her
flow's paths, and the summit's gift has no window at all (read from the flows).

The five story places write their quest's step whatever they give: the carpenters' scene moves the Spirit quest to
Step02, Darunia's dance the Fire quest to DodongoCavernOpne, his son's talk the Fire Temple's quest to MoveStatue, which
moves Darunia's statue and opens his door, King Zora's talk the Water quest to Step05, and Zelda's scene the AllSage
quest to Complete and the Ganon quest to Step01 (read from the flows). The fortress's guards, the Wasteland's gate
keeper and the archery range follow the Spirit quest, the Training Ground's keeper asks for 30 rupees and no card, and
the bridge to Ganon's Castle follows the Ganon quest, so the rules ask for the scenes, not the Gerudo's card or the
Light Arrows. Of the game's flows and logic modules, one reads the card: a module that takes two of the fortress's sneak
areas away. The program also reads it, inferred to be for the fortress's key counter on the screen, and a map picture's
table picks its light by it (read).

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

Darunia's scene binds a bracelet model to his hand whatever he gives (`Dm_Goron_0030` Events 0, 4, 11, 38 and 74), and
its give, `GetInEvent_Goron`, adds the item it is handed (read). He gives once, at the first Saria's Song after his
first meeting, at any step of the Fire quest (the Quests section below). His son gives through `GetInEvent_NotTurn`,
which does the same, and then moves the statue and opens the door whatever he gives (read). The son gives once, at his
talk at his step or past it, until the Fire Temple is finished (the Quests section below).

The Gerudo's card is a key item of the pouch, as the stones are, so it goes to a chest, a gift or a reward's place only.
A shop asks whether Link is a child before it sells either tunic (`DisplayShop.bfevfl`, read; both tunic shops refused a
child, seen), and a slot's rule asks for no age, so no shop sells the Zora Tunic or the Goron Tunic. The Light Arrows
may go wherever the Fire and Ice Arrows go. Seen in the game: a chest gave the pass; a chest, a Gold Skulltula and a
loose spot gave the Zora Tunic; a Gold Skulltula and a Kokiri Shop shelf gave the Light Arrows, the shelf to a child. A
seed that starts with either tunic leaves its copy at King Zora's or Darunia's son's place out of the pool, with a Red
Rupee in its stead. The Goron Tunic's other readers are a Gerudo's and Navi's lines and the shops' sale, which read its
ownership and whether Link wears it (read).

The Goron Bracelet may go wherever the gauntlets go, with its row in `shop_goods.tsv`. The pouch add writes its `IsGet`
and the lift level whatever gives it (read from the program), and its other readers are a Goron shopkeeper's lines, an
ownership query in the program and the bomb flower's fruit, which read the same record (read). Each step of strength
includes the bracelet, so a seed that starts with any step leaves Darunia's out of the pool, with a Red Rupee in its
stead, as it leaves each gauntlet's chest copy that step holds.

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

The quest tables say what the game's files hold about its quests: names and places, with no reading of which way a
scene goes. They are of the files of the game's update package, version 2.0.0, which is what the patcher reads; a file
only the base package holds is not in them. `tests/quests.py` writes them, and `tests/remake_tables_test.py` makes them
again and compares. After a change to that script or to the tests' readers, run
`python3 -I -B tests/quests.py <game.nup> remake`.

The tables do not hold every way a step changes. The game's program asks for a step itself at three places (read from
the program): the trade timer's reset to `SubEvent_GetSwordBigGoron` `Step09_01`, and two moves to the next step in the
routine that hatches an egg. The engine moves a quest on by itself when a `forced_by` step's flag or expression holds.
And it refuses a write onto a quest at `NotReady` or `Complete`.

A quest's step is the saved value `Step_<quest>`, and the game's quest table, `RSDB/Challenge`, gives its steps in the
order every comparison of steps reads. `quest_steps.tsv` has a row for each step of each of the 45 quests, in that
order, from `NotReady` to `Complete`.

- `quest` and `step`: the game's names.
- `depends_on`: on a quest's `NotReady` row, the flag or expression the quest table names as its `DependFlagName`.
- `forced_by`: the flag or expression the quest table names as the step's `ForceProgressGameData`.
- `participants`: the schedules of the characters who take part in the step, each the member
  `Component/AIScheduleParam/<name>.game__component__AIScheduleParam.bgyml` of `Pack/AIScheduleParam.pack.zs`.

`quest_writes.tsv` has a row for each place in the game's files that sets a quest's step, sorted by quest and step: each
event of an event flow that sets one, and each node of a logic graph (AINB) of a kind that does.

- `quest` and `step`: what the place sets. `quest` is empty where the place takes it from the event it carries out,
  and `step` is empty where the place sets the next step or takes it from the event. The scene loader's
  `SeqOneShotResetChallengeStepForTimerStop` names neither.
- `file`: the romfs path. A file inside an actor pack is `<pack>:<file>`. The game's `.110` copies of a flow have rows
  of their own.
- `at`: the event of an event flow, or the index of a node of a logic graph.
- `action`: the game's name for the event's action or the node, with where a node's quest comes from when the node
  does not fix it: `(blackboard <name>)` for a parameter of the actor's blackboard, whose value is in a file of its
  pack, or `(from <node>)` for the node that hands it on.
- `entries`: the flow's entry points that reach the event. The walk follows every next event and every case of a switch.
  It takes a fork's branches and its join as next events, so it shows what can be reached and not the order things play
  in. It follows a call of another entry of the same flow, and steps over a call of an entry that is already open on the
  way.
- `tests`: each switch of the flow that reads the same quest and stands on a way from an entry to the event, with the
  cases of it that lead on toward the event (`Event293:0`). For a node, each node that feeds it, directly or through
  others, and reads the same quest, with the outputs of it that are read (`6:IsEqual`).
- `untested_from`: each entry from which a way reaches the event past none of those switches. With no test, that is
  every entry that reaches it.

`tests` and `untested_from` say where the quest is asked on the way to a write, not what the answer is. They say nothing
of which case the game takes, or of what a case means. In a switch on a yes-or-no question, case 0 is the true one: in
963 of the 964 switches where the files show it, and wherever it has been seen in the game. A single switch's case is
still a reading until that switch is seen. They see only the write's own flow or graph. A test in a flow that calls this
one, in the actor that starts the event, or a played flag that lets a scene run once, is not among them, and in an
actor's AI, neither is the selector that picks the node.

`quest_reads.tsv` has a row for each place in the game's files that compares a quest's step, or names a quest beside its
steps: in an event flow, a logic graph, an actor's AI, a placement, a schedule or a table, sorted as `quest_writes.tsv`
is. A place compares a step through the saved value `Step_<quest>` itself, through a node that reads the step, or
through a saved expression of the game data list: 329 expressions compare a step themselves, and 391 reach such a
comparison through the expressions they name. Only BYML of version 7 is read; the game's other BYML files are effect
sets and node definitions. No cutscene timeline names a step write, a step's saved value or such an expression, and
`tests/quests.py` stops if one does. The step lists of the `FeverMaskParam` files are left out: they name no quest.

- `quest` and `step`: the quest, and the step it is compared with or named beside. `quest` is empty where the file
  takes it from elsewhere and no file of its pack gives it, and `step` is empty where the file does not fix it.
- `test`: `IsAfter` (strictly after), `IsEqual` or `IsBefore` (strictly before), with `not` before it when the
  expression negates that one comparison. `IsBefore` in an expression is read from the five that use it, each as the low
  end of a range. For a node that compares, the name of its output another node reads. Empty where a place names a
  step beside its quest, as a spawn point does, or reads a step and compares it with no fixed step.
- `file` and `at`: where, as in `quest_writes.tsv`. In a BYML file, `at` is the path to the string or number, with a
  placed actor by its hash.
- `via`: what the file names: `Step_<quest>`; a saved expression, then each expression it names on the way to the
  comparison, with `not` before one it negates; or the node that reads the step, with where its quest comes from as in
  `action`, and the node that compares what it reads. `(default)` marks a name an input holds only as its default:
  another node feeds that input, or it reads the actor's blackboard or an expression. `(hash)` marks an expression the
  file holds by its hash, under a name that hashes to it where another file or the quest's own expression names give
  one, and in hex where none does.

## Quests

A quest is one saved value, `Step_<quest>`, and the game moves it as Link acts. A write names the step it sets, and asks
nothing of the step the quest stands at unless a switch stands on the way to it, which `tests` in `quest_writes.tsv`
shows. The engine refuses only a write onto a quest at `NotReady` or `Complete`. In the game's own order of play the
writes come in order. A seed lets a player take another order, and a write can then set a quest back to a step it had
passed: what waited for the later step shuts again, and what Link handed over to pass it is gone.

Every mod changes the places below, whatever the seed's settings. The tables are of the game's files, so a row named
here shows the game's own write or question, and the change is what this section says of it. A change takes one of five
shapes:

- The write names another quest with the step `Complete`: a quest that is complete wherever the write can run, so the
  engine drops it.
- The switch before the write asks another flag or expression.
- The event before the write goes on to a later event, past the write.
- A placed actor's presence names another flag.
- An entry of an NPC's schedule holds on another flag and starts another talk.

The warp after Gohma, `Dm_SY_BancChange_EscapeDungeon` `Event4`, sets `Goto_HyruleCastle` to Step01 with no test. A Link
who woke Talon before Gohma stood at Step04 and was set back: Talon was gone from the crates, and Zelda only asked
whether something was the matter (seen in the game). `Event4` names `Get_StoneOfForest` and `Complete`, which `Event1`
of the same entry has just written. Seen in the game: the step is kept, and Zelda's scene plays and gives the letter.
From Gohma with the quest at Ready every scene on the road to Zelda plays too: Kaepora Gaebora's scene moves the quest
from Ready to Step02, so nothing needs Step01.

Kaepora Gaebora at the foot of Zora's River, `Get_StoneOfWater` `Event80`, sets `Get_StoneOfWater` to Step02 with no
test. He waits there once the Dodongo's Cavern Bomb Bag Chest is open. A Link who had shown King Zora Ruto's Letter
stood at Step07 and was set back, and King Zora sat in the passage to Zora's Fountain again (seen in the game). Only the
letter's show, `Event65`, writes Step07, and the letter is gone by then. `Event80` runs only after Impa's talk, which
comes only once the Fire quest is complete (read), so it names `Get_StoneOfFire` and `Complete`. Seen in the game in
both orders: the owl's scene plays and the Water quest keeps its step.

The diving game's first win, `Get_StoneOfWater` `Event45`, sets `Get_StoneOfWater` to Step05. The switches before it,
`Event293` and `Event471`, ask only that the quest be past Step03 and past Step03_Impa. After the letter's show a first
win set the quest back from Step07: King Zora could not be talked to, and after a load he sat in the passage again (seen
in the game). `Event293` asks `Sequence_IsAlternative_ZoraDomainAdult`, the expression by which the game shows an adult
the thawed domain once the Water Sage's quest is complete. It is false for every child, and only a child plays the
diving game (read), so the win writes no step. Seen in the game: a win after the show keeps Step07, gives its prize and
leaves the passage open. In the usual order the quest stays at Step04 through the win, until the lake's letter moves it
on. No reader tells Step04 from Step05 (read).

The first walk into Zora's Domain from the river, the entry `Goto_ZoraDomain` of `Dm_SY_BancChangeArrive`, plays the
domain's intro and then sets `Get_StoneOfWater` to Step03 or Step03_Impa, in `Event44` or `Event45`, with no test. The
tunnel from Lake Hylia has no such arrival. So a child with a scale can show the letter first and walk in from the river
after it, and the arrival then sets the quest back from Step07 with the letter gone (read). The intro, `Event48`, goes
on to `Event551`, the event that follows either write. The intro itself stays: its banner is the only writer of the
domain's visited record, `IsVisitLocation.ZoraDomain`, and with the intro left out the domain was on no map (seen in the
game). No write reaches Step03 or Step03_Impa in a mod. King Zora's first talk takes the quest on from Ready, and what
read Step03 waits for that talk or for the letter's show: among them `Logic/ZoraRiver_Common_08f0`, which keeps seven
invisible walls at the river's two boulders while the boulders are whole and the quest is below Step03. Seen in the
game: a first walk in at Ready plays the intro and its banner, keeps Ready and puts the domain on the map. After a show
through the tunnel the quest stands at Step07 before the walk in, after it and in Zora's Fountain.

The stone check of the Door of Time, the entry `DoorOfTime_CheckStoneNum` of the flow `DoorOfTime`, runs at each stone
dungeon's first completion, and `DoorOfTime` is `NotReady` until Link meets Zelda (`depends_on`). With both stones done
before Zelda, the engine refuses the check's write of Step03 in `Event37`, and the next event, `Event77`, raises Hyrule
Field's drawbridge all the same. Only Zelda's escape at Step03 lowers it, so the child cannot reach Zelda (the raised
bridge seen in the game, the rest read). `Event37` goes on to `Event4`, past `Event77`. And the last event of Impa's
send-off, `Event62`, goes on to `Event38`, the check's first event, so the send-off counts the stones done before Zelda.
Seen in the game with both stones done: the bridge is down, Zelda's escape plays at the drawbridge right after Impa's
scene, and the moat then holds the Ocarina of Time.

Impa teaches her song only while `DoorOfTime` stands at Step01, and stands in the courtyard only until the quest is past
it (`DoorOfTime_IsAfter_Step01_Exp` on her placement in `quest_reads.tsv`). With the Goron's Ruby got before Zelda,
Zelda's talk after her scene, `Npc_Hylia_Zelda_Child` `Event42`, and Saria's Song on the ocarina,
`Dm_SY_Ocarina_SariaSong` `Event172`, each set the quest to Srep2_1_1, and Impa was gone with what her lesson holds
(seen in the game). Every way to either write passes the true case of `Get_StoneOfFire_IsCompleted_Exp` (measured), so
both name `Get_StoneOfFire` and `Complete`. Seen in the game: Impa stays after the talk and after the song, and teaches.
One order still loses her lesson: a warp song out of the courtyard before it, and then a stone dungeon, whose check
writes Step2_1 or Step2_2 in `Event147` or `Event148`. A child holds a warp song before Zelda only with the songs
shuffled.

The Forest Temple's exit, `Dm_SY_BancChange_EscapeDungeon` `Event208`, sets `AllSage` to Step01_00 with no test, right
after `Event199` completes `SageOfForest`. Zelda's scene in the Temple of Time starts as Link walks in while `AllSage`
stands at Step02 exactly (`Logic/Market_Adult_Prefab_c668`), and only `AllSage` `Event20` writes Step02, once the Shadow
and the Spirit sages have both woken. With those two woken before the Forest Temple, the exit set the quest back and the
walk in played nothing. Only an arrival by the Prelude of Light still played the scene (seen in the game). `Event208`
names `SageOfForest` and `Complete`, the quest and step of `Event199`. Seen in the game: in that order the quest stays
at Step02 and the walk in plays Zelda's scene. In the usual order it stays at Step01 after the exit, and the adventure
log keeps Step01's line until the Prelude's lesson or a later sage moves the quest on (read).

Adult Malon, `Npc_Hylia_Malon_Adult` `Event177`, sets `Get_Epona` to GuideToLonLonRanchAdult after a lost bet race on
another horse, whenever the switch `Event179` finds the quest not past ReadyForGetEponaSong. A Link who met child Malon
at the ranch and had not learned her song was set back a step, and she then started no lesson (seen in the game).
`Event179` asks `Get_Epona_IsAfter_Ready_Exp`, so the write moves only a quest still at Ready. Seen in the game: after a
lost race the step stays and the child then learns the song, and an adult who learned it as a child still wins Epona and
rides out.

Darunia dances at each Saria's Song from his first meeting on, at every step of `Get_StoneOfFire` (`Event35` and
`Event185`), and the switch after the dance, `Event39`, starts his gift's scene, `Dm_Goron_0030`, while the quest is not
past SariaMelodyPlay. The scene gives in `Event127` and sets the quest to DodongoCavernOpne in `Event121`. The cavern's
mouth sets DungeonStart (`Logic/DeathMountainTrail_Child_Prefab_ffa7`) and King Dodongo's warp Complete, so a Link who
opened the cavern first lost the gift. Five writes that each run once set the quest back to SariaMelodyPlay or below:
his first meeting, Impa's send-off, Zelda's Letter shown at Kakariko's gate, the first walk up the trail and Saria's
lesson. After the dance each lets the next song give again (read). Seen in the game: the trail's first walk set the
quest back from DungeonStart. `Event39` asks `Kaepora_HavePlayedEvent_Dm_Goron_0030_Exp`, an expression of the mod's own
that reads the scene's played flag, `HavePlayedEvent.Dm_Goron_0030`, which the game sets as the scene plays (seen). So
he gives at the first song after his meeting, at any step, and at no later one. His first meeting plays as a child walks
into his room at any step (`Logic/GoronCity_Child_Prefab_5b76`), so a child who meets him after an adult has cleared the
cavern gets the gift as well. The scene's write sets the quest back to DodongoCavernOpne from DungeonStart, and is
refused on a completed quest (read). Seen in the game: with the cavern opened first, the first song gave, and a second
song and one after the trail's first walk gave nothing; with the quest complete and Darunia never met, his meeting
played, the first song gave, and the second gave nothing.

Darunia's son gives his tunic only as a participant of `SageOfFire`, at Ready, StopDaruniaJr and TalkDaruniaJr. His
talk at TalkDaruniaJr, `1396283892_1460050088_2425371882_C1_T`, gives it in `SageOfFire` `Event18`, and the door scene
after it sets `SageOfFire_Step02_DoorOpen` and the quest to MoveStatue. The crater's arrival from Goron City
(`Logic/DeathMountainCrater_Adult_Prefab_a0e2`), Sheik's lesson there (`_e63c`) and the Fire Temple's first room
(`Logic/FireTemple_Level_Room_00_Prefab_1ae5`) each set a later step from any earlier one, and past his step only his
own schedule, `Component/AIScheduleParam/Npc_Goron_DaruniaJr_Adult`, applies, with no talk that gives. Seen in the
game, with the quest at MeetSheik and no Goron Tunic: he stood at his post and gave nothing. Every mod changes that
schedule's entry [0], which holds while `SageOfFire_Step01_DaruniaJr_BombHit` is false and starts his plain talk: it
holds while `DoorOpen` is false instead, still reversed, and starts the gift talk. Only that talk's door scene sets
`DoorOpen`, so past his step he gives once, until the Fire Temple is finished. Seen in the game with the quest at
MeetSheik: he gave, and a second talk gave nothing. His gift writes MoveStatue back from the later step, and the quest
lands on EnterCrater: `Logic/GoronCity_Adult_Prefab_d9d1` writes EnterCrater whenever the quest is before it and
Darunia's statue stands in `Area_64c2`, and it forces the statue there while the quest is past MoveStatue (read; seen
at MeetSheik). The crater's arrival and the temple's first room set their steps again as Link passes them, but
EnterTemple, GetBossKey, MeetBoss and DefeatBoss do not come again (read). `BombHit` is not saved, so in the game's
order he takes entry [0] again from MoveStatue on. In a mod `DoorOpen` is set by then, so he takes his default entry,
one unit from his post, with the balloon "It's up to you! Gowo!" in place of "Dad... Where are you?" (read).

Once the Fire Temple is finished the son gives nothing, and he stands in front of Darunia's door (seen). At Complete
entry [1] holds too, on `SageOfFire_IsCompleted_Exp`, and with both holding the later entry won (seen). Entries [2] to
[5] hold on `SageOfFire_FromCrater`, `_FromCrater_FarJr`, `_FromDeathMt` and `_FromDeathMt2`: unsaved Bools, set only
while the quest is complete and his temple talk (`Npc_Goron_DaruniaJr_SageOfFireClearTalked`) is not done. The
arrivals from the trail and the crater set `FromDeathMt` and `FromCrater` (`SageOfFire_CityInFromDeathMt` and
`_CityInFromCrater`), and the areas of `Logic/GoronCity_Adult_Prefab_a09c` and `_64b3` set `FromDeathMt2` and
`FromCrater_FarJr` (read). So a Link who beats Volvagia before the son's talk loses what his place holds.

With "Biggoron Quest" ticked, a mod changes one more write. Biggoron's hand-over for the Claim Check,
`Npc_Goron_Biggoron_Adult` `Event89`, sets `SubEvent_GetSwordBigGoron` to Complete after his gift, and his question
`Event49` asks `SubEvent_GetSwordBigGoron_IsCompleted_Exp`, so he gives once. A receipt found elsewhere can come before
adult Anju's talk, and she offers her gift only while the quest stands at Ready (her schedule's
`SubEvent_GetSwordBigGoron_IsAfter_Ready_Exp` in `quest_reads.tsv`), so the write would lose her place (read). The event
before the write, `Event112`, goes on to `Event21`, the write of his talk flag in his talk after the quest, which now
records the claim `Kaepora_Once_14299450766367161284`, and `Event49` asks `Kaepora_Once_14299450766367161284_Exp`, an
expression of the mod's own that reads it. So his hand-over writes no step of the quest: he gives once in either order
with Anju's talk, and the quest never completes, as in SoH. Seen in the game in both orders: Biggoron first, he gave his
place's item, his claim went to 1 and the quest stayed at Ready, and Anju then gave hers and moved it to StartSubEvent;
Anju first, he then gave his, the claim went to 1 and the quest stayed at StartSubEvent. With both given, the receipt
shown again got only his line about carrying it, and Anju gave nothing.

King Zora's ice is the one presence among them, told with the story gifts above: `SageOfWater` at `Complete` on
`Actors/3341274640047764997/Presence/FlagName` of `Banc/ZoraDomain/ZoraDomain_Adult_Alternative_Static` in
`quest_reads.tsv`.

A mod also rests on a step in these places, where no write changes:

- The pedestal's module, `Logic/Market_Adult_Prefab_89da`, takes the sword's interaction away while `SageOfForest` is
  not complete. Every mod makes it ask `DoorOfTime_IsCompleted_Exp`, so every adult can put the Master Sword back.
- Sheik's schedule sends him to `AnchorAction1` of `Banc/Market/Market_Adult_Static` until `SageOfForest` is complete.
  Every mod moves that anchor from in front of the pedestal to behind it.
- With "Kokiri Forest" at "Closed Deku", Mido's schedule loses its last entry, the one on
  `Goto_HyruleCastle_IsCompleted_Exp`.
- With the songs shuffled, the Colossus's gates stand on `SageOfSpirit_IsAfter_Step02_Exp` (above), and
  `HavePlayedEvent.Dm_TimeTemple_0065` starts true: that scene at the Door of Time plays only while `AllSage` stands at
  Step01, and the adult's world map waits for it.
- With "Malon's Egg" ticked, the area at the top of the castle's vines stands only for a Link who got the Weird Egg. It
  hatches the Chicken while `Goto_HyruleCastle` is at Step03, the step Malon's talk writes.
- With "Ganon's trials" at "None", nothing writes `Defeat_Ganon` Step04, and the boss door's write of Step05 goes
  forward (read).
- With "Opening" at "Skipped", `HavePlayedEvent.Dm_Kokiri_0030` starts true. It is the flag that forces `Meet_DekuTree`
  to Ready, Navi waking Link.
