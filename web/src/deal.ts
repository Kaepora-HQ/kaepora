type Wall = { columns: number; rows: number; tiles: number[]; span: Record<number, string>; off: string; width: Record<number, string> }

// A tile is a square of that many cells a side. A wall's tiles must fill it exactly, and
// `tiles` must list them in reading order, because the server lays them out as listed.
export const walls: { wide: Wall; narrow: Wall } = {
  wide: {
    columns: 6,
    rows: 8,
    tiles: [3, 2, 1, 1, 1, 1, 1, 2, 2, 2, 2, 1, 3, 1, 1, 1, 1],
    span: { 3: '@min-[48rem]:col-span-3 @min-[48rem]:row-span-3', 2: '@min-[48rem]:col-span-2 @min-[48rem]:row-span-2', 1: '' },
    off: '@min-[48rem]:hidden',
    width: { 3: 'min(38rem, 50vw)', 2: 'min(25rem, 33vw)', 1: 'min(13rem, 17vw)' },
  },
  narrow: {
    columns: 2,
    rows: 8,
    tiles: [2, 1, 1, 1, 1, 2, 1, 1, 1, 1],
    span: { 2: '@max-[48rem]:col-span-2 @max-[48rem]:row-span-2', 1: '' },
    off: '@max-[48rem]:hidden',
    width: { 2: '100vw', 1: '50vw' },
  },
}

// The deal is one string, so the root element can hold it: the pictures' order, then each
// wall's tiling as tile sides in reading order, the order the grid places them in. deal,
// tileAt and parts go into the page's script as text, so they use nothing outside themselves.
export function deal(count: number, { wide, narrow }: typeof walls) {
  const order = Array.from({ length: count }, (_, place) => place)
  for (let last = count - 1; last > 0; last--) {
    const pick = Math.floor(Math.random() * (last + 1))
    ;[order[last], order[pick]] = [order[pick], order[last]]
  }
  const tiled = ({ columns, rows, tiles }: Wall) => {
    for (;;) {
      const left = [...tiles]
      const taken = new Set<number>()
      const tiling: number[] = []
      for (let cell = 0; left.length; cell++) {
        if (taken.has(cell)) continue
        const square = (side: number) => Array.from({ length: side * side }, (_, n) => cell + (n % side) + Math.floor(n / side) * columns)
        const fits = [...new Set(left)].filter(
          (side) => (cell % columns) + side <= columns && Math.floor(cell / columns) + side <= rows && square(side).every((other) => !taken.has(other)),
        )
        if (!fits.length) break
        const side = fits[Math.floor(Math.random() * fits.length)]
        left.splice(left.indexOf(side), 1)
        for (const other of square(side)) taken.add(other)
        tiling.push(side)
      }
      if (!left.length) return tiling
    }
  }
  return [order, tiled(wide), tiled(narrow)].join(';')
}

// A tile's classes and its picture's sizes hint, from its side on each wall. A tile past a wall's last has no side there.
export function tileAt(wideSide: number | undefined, narrowSide: number | undefined, { wide, narrow }: typeof walls) {
  return {
    className: `${wideSide ? wide.span[wideSide] : wide.off} ${narrowSide ? narrow.span[narrowSide] : narrow.off}`,
    sizes: `(min-width: 52.2rem) ${wideSide ? wide.width[wideSide] : '1px'}, ${narrowSide ? narrow.width[narrowSide] : '1px'}`,
  }
}

export const parts = (dealt: string) => dealt.split(';').map((part) => part.split(',').map(Number))
