import type { System } from '@/release'

type Moment = { title: string; text: string }
type Shot = { picture: string; alt: string }

export type Copy = {
  hero: { headline: string; lead: string }
  download: { forSystem: Record<System, string>; anySystem: string; allLink: string }
  middle: { heading: string; moments: readonly [Moment, Moment, Moment, Moment] }
  // The wall shows its pictures in this order until the page deals its own.
  wall: { viewer: string; close: string; previous: string; next: string; shots: readonly Shot[] }
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
    shots: [
      {
        picture: 'chest-hookshot-kokiri-sword-chest',
        alt: 'Young Link holds up the Hookshot over the open Kokiri Sword chest. The game says: "You were given the Hookshot!"',
      },
      {
        picture: 'shop-longshot-kakariko-bazaar',
        alt: 'The Kakariko Bazaar\'s shelves, with the Longshot picked out among hearts, bombs and shields. The panel reads: Longshot, 15 rupees.',
      },
      {
        picture: 'shop-zora-shop',
        alt: 'The Zora shop\'s shelves, with the Boomerang picked out. The panel reads: Boomerang, 5 rupees.',
      },
      {
        picture: 'shop-golden-gauntlets-kakariko-potion-shop',
        alt: 'The Kakariko potion shop\'s shelves, with a pair of gauntlets picked out among the potions. The panel reads: Golden Gauntlets, 20 rupees.',
      },
      {
        picture: 'start-ganons-tower-no-trials',
        alt: 'Link stands in the dark hall of Ganon\'s Castle, facing the open way into the tower.',
      },
      {
        picture: 'chest-adults-wallet-gerudo-valley',
        alt: 'Link holds up a wallet among red rocks. The game says: "You got an adult\'s wallet!"',
      },
      {
        picture: 'hint-gossip-stone-kokiri-forest',
        alt: 'Young Link stands at a gossip stone in Kokiri Forest. The stone says: "They say that the Bottom of the Well hides the Megaton Hammer."',
      },
      {
        picture: 'start-rainbow-bridge-standing',
        alt: 'Link stands at the foot of a shining rainbow bridge that leads to Ganon\'s Castle.',
      },
      {
        picture: 'chest-silver-scale-midos-house',
        alt: 'Young Link holds up a silver scale in Mido\'s House. The game says: "You got a silver scale!"',
      },
      {
        picture: 'song-windmill-teaches-eponas-song',
        alt: 'The man in the windmill cranks his music box beside Link. A sheet of music reads: "This song has been stuck in my head ever since!"',
      },
      {
        picture: 'song-minuet-lesson-castle-courtyard',
        alt: 'Impa stands before Young Link in the castle courtyard. A sheet of music reads: "Play the Minuet of the Forest."',
      },
      {
        picture: 'hint-cursed-child-hover-boots',
        alt: 'A cursed boy, half spider, hangs before Link in the House of Skulltula and says: "Please save me by destroying 10 Gold Skulltulas, and I will give you the Hover Boots!"',
      },
      {
        picture: 'hint-mido-kokiri-sword-in-kakariko',
        alt: 'Mido stands in Young Link\'s way and says: "Hrmph! You\'ll never find the Kokiri Sword I hid in Kakariko Village! Without a sword and a shield, I won\'t let you through!"',
      },
      {
        picture: 'hint-happy-mask-shop-sign',
        alt: 'The sign in the Happy Mask Shop reads: "Show them the Skull Mask, and they\'ll give you the Fire Arrows."',
      },
      {
        picture: 'gift-fairy-bow-kakariko-roof',
        alt: 'Link holds up the Fairy Bow on a rooftop in Kakariko Village, with a man behind him. The game says: "You found the Fairy Bow!"',
      },
      {
        picture: 'gift-hover-boots-house-of-skulltula',
        alt: 'Link holds up the Hover Boots in the dark House of Skulltula. The game says: "You found the Hover Boots!"',
      },
      {
        picture: 'chest-lon-lon-milk-hyrule-field',
        alt: 'Link holds up a bottle of milk beside an open chest in a cave. The game says: "You got some Lon Lon milk!"',
      },
      {
        picture: 'loose-megaton-hammer-gohma',
        alt: 'Young Link holds up the Megaton Hammer beside the blue light in Gohma\'s lair. The game says: "You found the Megaton Hammer!"',
      },
      {
        picture: 'loose-boomerang-lon-lon-ranch',
        alt: 'Young Link stands over a Boomerang that lies in a ring of stones in a tower at Lon Lon Ranch.',
      },
      {
        picture: 'gift-boomerang-lab-dive-child',
        alt: 'Young Link holds up the Boomerang beside the tank in the lakeside laboratory. The game says: "You found the Boomerang!"',
      },
    ],
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
