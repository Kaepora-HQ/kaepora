import { expect, test } from 'bun:test'
import { deal, parts, walls } from './deal'

// Lays a tiling out as the grid does, each tile at the first cell where it fits, and
// counts the wall's cells left empty and the cells taken outside the wall.
function misses({ columns, rows }: { columns: number; rows: number }, tiling: number[]) {
  const taken = new Set<number>()
  for (const side of tiling) {
    const square = (cell: number) => Array.from({ length: side * side }, (_, n) => cell + (n % side) + Math.floor(n / side) * columns)
    let cell = 0
    while ((cell % columns) + side > columns || square(cell).some((other) => taken.has(other))) cell++
    for (const other of square(cell)) taken.add(other)
  }
  const wall = columns * rows
  return [...taken].filter((cell) => cell >= wall).length + wall - [...taken].filter((cell) => cell < wall).length
}

const deals = Array.from({ length: 2000 }, () => parts(deal(34, walls)))

test('a deal lists every picture once', () => {
  for (const [order] of deals) expect(order.toSorted((a, b) => a - b)).toEqual(Array.from({ length: 34 }, (_, picture) => picture))
})

test('a dealt tiling fills the wide wall and the narrow one with no gap', () => {
  for (const [, wide, narrow] of deals) {
    expect(wide.toSorted()).toEqual(walls.wide.tiles.toSorted())
    expect(narrow.toSorted()).toEqual(walls.narrow.tiles.toSorted())
    expect(misses(walls.wide, wide)).toBe(0)
    expect(misses(walls.narrow, narrow)).toBe(0)
  }
})

test("the tiling the server lays out fills its wall", () => {
  expect(misses(walls.wide, walls.wide.tiles)).toBe(0)
  expect(misses(walls.narrow, walls.narrow.tiles)).toBe(0)
})

test('loads deal different walls', () => {
  expect(new Set(deals.map(([, wide]) => wide.join())).size).toBeGreaterThan(100)
})
