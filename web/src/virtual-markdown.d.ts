declare module 'virtual:changelog' {
  const html: string | null
  export default html
}

declare module 'virtual:faq' {
  const html: string
  export default html
  /** The level-2 headings, with the ids the HTML gives them and each one's first paragraph as plain text. */
  export const sections: { id: string; title: string; summary: string }[]
}

declare module 'virtual:randomized' {
  const html: string
  export default html
  /** The level-2 headings, with the ids the HTML gives them and each one's first paragraph as plain text. */
  export const sections: { id: string; title: string; summary: string }[]
}

declare module 'virtual:counts' {
  /** Counted from `remake/chests.tsv` and `remake/stones.tsv`: the game's chests by place, how many of them a seed can shuffle, and the gossip stones that can give a hint. */
  const counts: { chests: { shuffled: number; total: number }; stones: number }
  export default counts
}
