type Moment = { title: string; text: string }

export type Copy = {
  hero: { headline: string; lead: string }
  download: { button: string; otherLink: string }
  middle: { heading: string; moments: readonly [Moment, Moment, Moment, Moment] }
  closing: { heading: string }
  downloadPage: {
    title: string
    status: string
    noReleases: string
  }
  footer: {
    heart: { before: string; label: string; after: string }
    thanks: { before: string; name: string; after: string }
    smallPrint: string
  }
  meta: { description: string }
  preview: { line: string }
}

export const copy: Copy = {
  hero: {
    headline: "You've never played this Ocarina of Time.",
    lead: "Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It shuffles what's inside every chest in Hyrule, so the Hookshot could be anywhere. You know this game by heart. Now you get to guess.",
  },
  download: {
    button: 'Download for Linux',
    otherLink: 'Windows and other downloads',
  },
  middle: {
    heading: "Every seed is a Hyrule you haven't met.",
    moments: [
      {
        title: 'No chest is safe to skip.',
        text: "You know which chests hold a few rupees. You stopped opening them long ago. Open them anyway. Even Mido's House is worth a look now.",
      },
      {
        title: 'Throw out the route in your head.',
        text: 'You could play the old route in your sleep. The thing you need is somewhere else now, so you go somewhere else, to places you used to rush past.',
      },
      {
        title: 'Get a friend lost too.',
        text: 'Give a seed to a friend and you both get the same game, chest for chest. Race it, or play apart and compare.',
      },
      {
        title: 'Stuck? The map knows.',
        text: "Every seed comes with a spoiler log, a list of where everything went. Nobody has to know you looked. This owl won't ask if you want to hear it again.",
      },
    ],
  },
  closing: {
    heading: 'Go find your Hookshot.',
  },
  downloadPage: {
    title: 'Download Kaepora',
    status: 'Kaepora is still in development.',
    noReleases: 'No releases yet.',
  },
  footer: {
    heart: { before: 'Made with', label: 'love', after: 'by fans of Ocarina of Time.' },
    thanks: { before: 'Special thanks to', name: 'Ship of Harkinian', after: '.' },
    smallPrint: 'For the Switch 2 remake, not the Nintendo 64 game. You need your own copy of the game and an emulator that loads mods. Kaepora includes none of the game and never changes your copy. Open source under the MIT license. A fan project, not affiliated with Nintendo.',
  },
  meta: {
    description: "Kaepora is a randomizer for the Switch 2 remake of Ocarina of Time. It shuffles what's inside every chest, so the game you know by heart is unknown again.",
  },
  preview: {
    line: 'Every chest in Hyrule has changed its mind.',
  },
}
