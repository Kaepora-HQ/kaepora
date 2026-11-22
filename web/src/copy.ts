import type { System } from '@/release'

type Moment = { title: string; text: string }

export type Copy = {
  hero: { headline: string; lead: string }
  download: { forSystem: Record<System, string>; anySystem: string; allLink: string }
  middle: { heading: string; moments: readonly [Moment, Moment, Moment, Moment] }
  // The wall shows every picture in src/captures. One with no alt text here is named by its file name.
  wall: { viewer: string; close: string; previous: string; next: string; alt: Readonly<Partial<Record<string, string>>> }
  closing: { heading: string }
  downloadPage: {
    title: string
    status: string
    noReleases: string
    macosFirstOpen: {
      heading: string
      why: string
      steps: readonly string[]
      after: string
      terminal: string
      command: string
    }
  }
  faqPage: { title: string; questions: string }
  randomized: {
    title: string
    contents: string
    all: string
    counts: string
    chests: string
    of: string
    stones: string
    fixedRows: readonly (readonly [string, string])[]
  }
  footer: {
    heart: { before: string; label: string; after: string }
    thanks: { before: string; name: string; after: string }
    smallPrint: string
    disclosure: string
  }
  meta: { description: string }
  preview: { line: string }
}

export const copy: Copy = {
  hero: {
    headline: "You've never played this Ocarina of Time",
    lead: "Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It shuffles what's inside every chest in Hyrule, so the Hookshot could be anywhere. You know this game by heart. Now you get to guess.",
  },
  download: {
    forSystem: {
      windows: 'Download for Windows',
      macos: 'Download for macOS',
      linux: 'Download for Linux',
    },
    anySystem: 'Download',
    allLink: 'All downloads',
  },
  middle: {
    heading: "Every seed is a Hyrule you haven't met",
    moments: [
      {
        title: 'No chest is safe to skip',
        text: "You know which chests hold a few rupees. You stopped opening them long ago. Open them anyway. Even Mido's House is worth a look now.",
      },
      {
        title: 'Throw out the route in your head',
        text: 'You could play the old route in your sleep. The thing you need is somewhere else now, so you go somewhere else, to places you used to rush past.',
      },
      {
        title: 'Get a friend lost too',
        text: 'Give a seed to a friend and you both get the same game, chest for chest. Race it, or play apart and compare.',
      },
      {
        title: 'Stuck? The map knows',
        text: "Every seed comes with a spoiler log, a list of where everything went. Nobody has to know you looked. This owl won't ask if you want to hear it again.",
      },
    ],
  },
  wall: {
    viewer: 'A picture from the game',
    close: 'Close',
    previous: 'Previous picture',
    next: 'Next picture',
    alt: {
      'chest-adults-wallet-gerudo-valley': 'Link holds up a wallet among red rocks. The game says: "You got an adult\'s wallet!"',
      'chest-hookshot-kokiri-sword-chest': 'Young Link holds up the Hookshot over the open Kokiri Sword chest. The game says: "You were given the Hookshot!"',
      'chest-lon-lon-milk-hyrule-field': 'Link holds up a bottle of milk beside an open chest in a cave. The game says: "You got some Lon Lon milk!"',
      'chest-silver-scale-midos-house': 'Young Link holds up a silver scale in Mido\'s House. The game says: "You got a silver scale!"',
      'gift-boomerang-frogs-zoras-river': 'Young Link holds up the Boomerang on a grassy bank of Zora\'s River. The game says: "You found the Boomerang!"',
      'gift-boomerang-lab-dive-adult': 'Link holds up the Boomerang beside the tank in the lakeside laboratory. The game says: "You found the Boomerang!"',
      'gift-boomerang-lab-dive-child': 'Young Link holds up the Boomerang beside the tank in the lakeside laboratory. The game says: "You found the Boomerang!"',
      'gift-fairy-bow-kakariko-roof': 'Link holds up the Fairy Bow on a rooftop in Kakariko Village, with a man behind him. The game says: "You found the Fairy Bow!"',
      'gift-hover-boots-house-of-skulltula': 'Link holds up the Hover Boots in the dark House of Skulltula. The game says: "You found the Hover Boots!"',
      'gift-iron-boots-poe-buyer': 'Link holds up a pair of iron boots in a dim stone room. The game says: "You found the Iron Boots!"',
      'hint-cursed-child-hover-boots': 'A cursed boy, half spider, hangs before Young Link in the House of Skulltula and says: "Please save me by destroying 10 Gold Skulltulas, and I will give you the Hover Boots!"',
      'hint-gossip-stone-kokiri-forest': 'Young Link stands at a gossip stone in Kokiri Forest. The stone says: "They say that the Bottom of the Well hides the Megaton Hammer."',
      'hint-happy-mask-shop-sign': 'The sign in the Happy Mask Shop reads: "Show them the Skull Mask, and they\'ll give you the Fire Arrows."',
      'hint-mido-kokiri-sword-in-kakariko': 'Mido stands in Young Link\'s way and says: "Hrmph! You\'ll never find the Kokiri Sword I hid in Kakariko Village! Without a sword and a shield, I won\'t let you through!"',
      'loose-boomerang-lon-lon-ranch': 'Young Link stands over a Boomerang that lies in a ring of stones in a tower at Lon Lon Ranch.',
      'loose-megaton-hammer-gohma': 'Young Link holds up the Megaton Hammer beside the blue light in Gohma\'s lair. The game says: "You found the Megaton Hammer!"',
      'shop-business-scrub-seed-goron-shop': 'The Goron shop\'s shelves, with a sprouting seed picked out among bombs, a rupee and a shield. The panel reads: Business Scrub Seed, 5 rupees.',
      'shop-golden-gauntlets-kakariko-potion-shop': 'The Kakariko potion shop\'s shelves, with a pair of gauntlets picked out among the potions. The panel reads: Golden Gauntlets, 5 rupees.',
      'shop-ice-arrow-bought-bombchu-shop': 'Young Link holds up an arrow tipped in ice in the Bombchu Shop. The game says: "You got the ice arrow!"',
      'shop-ice-arrow-price-bombchu-shop': 'The Bombchu Shop\'s keeper shows an arrow tipped in ice and says: "Lookin\' at the ice arrow? That\'ll cost ya 5 rupees." The choices are "I\'ll buy!" and "Never mind."',
      'shop-ice-arrows-bombchu-shop': 'The Bombchu Shop\'s shelves, with an arrow tipped in ice picked out among the Bombchus. The panel reads: Ice Arrow, 5 rupees.',
      'shop-kokiri-sword-kokiri-shop': 'The Kokiri Shop\'s shelves, with a small sword picked out beside sticks, nuts, arrows and a shield. The panel reads: Kokiri Sword, 5 rupees.',
      'shop-kokiri-sword-price-zora-shop': 'The Zora shop\'s keeper shows a small sword and says: "The kokiri sword... That\'ll cost 54 rupees! Have we got a deal?" The choices are "I\'ll buy!" and "Never mind."',
      'shop-longshot-kakariko-bazaar': 'The Kakariko Bazaar\'s shelves, with the Longshot picked out beside a heart, bombs, boots and a shield. The panel reads: Longshot, 5 rupees.',
      'shop-megaton-hammer-market-bazaar': 'The Market Bazaar\'s shelves, with a hammer picked out beside a heart, bombs, a rupee and two shields. The panel reads: Megaton Hammer, 5 rupees.',
      'shop-mirror-shield-market-potion-shop': 'The Market potion shop\'s shelves, with a shield picked out among the potions. The panel reads: Mirror Shield, 5 rupees.',
      'shop-zora-shop': 'The Zora shop\'s shelves, with the Boomerang picked out. The panel reads: Boomerang, 5 rupees.',
      'skulltula-bomb-bag-deku-tree': 'Young Link holds up a golden skull token inside the Great Deku Tree. The game says: "You found a bomb bag!"',
      'song-impa-plays-the-minuet': 'Impa, close up, with a hand to her chin. A sheet of music reads: "Play the Minuet of the Forest."',
      'song-link-plays-song-of-time': 'Young Link plays the ocarina in a rush of blue light. A sheet of music reads: "You played the Song of Time!"',
      'song-link-plays-the-minuet-moat': 'Young Link plays the ocarina in a white mist. A sheet of music reads: "You played the Minuet of the Forest!"',
      'song-malon-teaches-sarias-song': 'Malon stands before Young Link in a grassy paddock. A sheet of music reads: "Try playing Saria\'s Song."',
      'song-minuet-lesson-castle-courtyard': 'Impa stands before Young Link in the castle courtyard. A sheet of music reads: "Play the Minuet of the Forest."',
      'song-royal-tomb-teaches-song-of-time': 'A stone carved with writing and a line of music, lit green. A sheet of music reads: "The melody that opens the Door of Time."',
      'song-windmill-teaches-eponas-song': 'The man in the windmill cranks his music box beside Link. A sheet of music reads: "This song has been stuck in my head ever since!"',
      'song-zelda-teaches-the-minuet-at-the-moat': 'Princess Zelda plays the ocarina in a white mist. A sheet of music reads: "Play the Minuet of the Forest."',
      'song-zeldas-lullaby-impa-no-ocarina': 'Young Link holds up his empty hands as if playing an ocarina in the castle courtyard, with Navi beside him. The game says: "You learned Zelda\'s Lullaby!"',
      'start-ganons-tower-no-trials': 'Link stands in the dark hall of Ganon\'s Castle, facing the open way into the tower.',
      'start-rainbow-bridge-standing': 'Link stands at the foot of a shining rainbow bridge that leads to Ganon\'s Castle.',
    },
  },
  closing: {
    heading: 'Go find your Hookshot',
  },
  downloadPage: {
    title: 'Download Kaepora',
    status: 'Kaepora is still in development.',
    noReleases: 'No releases yet.',
    macosFirstOpen: {
      heading: 'Opening it on a Mac the first time',
      why: 'Apple has not checked this version, so macOS blocks it the first time you open it.',
      steps: [
        'Unzip the download and open Kaepora. macOS says it cannot be opened. Click Done.',
        'Open System Settings and go to Privacy & Security.',
        'Under Security, click Open Anyway, then enter your password.',
      ],
      after: 'After that it opens like any other app.',
      terminal: 'If you prefer Terminal:',
      command: 'xattr -dr com.apple.quarantine Kaepora.app',
    },
  },
  randomized: {
    title: 'What is randomized',
    contents: 'On this page',
    all: 'Read all of it',
    counts: 'The most a seed can shuffle',
    chests: 'Chests a seed can shuffle',
    of: 'of',
    stones: 'Gossip stones that can give a hint',
    fixedRows: [
      ['Shop places a seed can shuffle', '55 of 64'],
      ['Gifts from characters', '44'],
      ['Songs', '12 of 12'],
      ['Gold Skulltula tokens', '100 of 100'],
      ['Loose items', '26'],
      ['Dungeon rewards', '9 of 9'],
    ],
  },
  faqPage: {
    title: 'FAQ',
    questions: 'Questions',
  },
  footer: {
    heart: { before: 'Made with', label: 'love', after: 'by fans of Ocarina of Time' },
    thanks: { before: 'Special thanks to', name: 'Ship of Harkinian', after: '.' },
    smallPrint: 'For the Switch 2 remake, not the Nintendo 64 game. You need your own copy of the game and an emulator that loads mods. Kaepora includes none of the game and never changes your copy. Open source under the MIT license.',
    disclosure: 'An unofficial fan project, not affiliated with or endorsed by Nintendo. The Legend of Zelda and Ocarina of Time are trademarks of Nintendo.',
  },
  meta: {
    description: "Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It shuffles what's inside every chest, so the game you know by heart is unknown again.",
  },
  preview: {
    line: 'Every chest in Hyrule has changed its mind',
  },
}
