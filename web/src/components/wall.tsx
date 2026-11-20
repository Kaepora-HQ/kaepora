import { ScriptOnce } from '@tanstack/react-router'
import { createIsomorphicFn } from '@tanstack/react-start'
import { Activity, type KeyboardEvent, ViewTransition, startTransition, useLayoutEffect, useRef, useState } from 'react'
import { captureWidths } from '@/capture-widths'
import type { Copy } from '@/copy'

// Every picture in src/captures is on the wall. scripts/captures.ts writes each one at every width.
const files = import.meta.glob<string>('../captures/*.webp', { eager: true, import: 'default' })
const pictures = [...new Set(Object.keys(files).map((path) => path.replace(/^.*\/|-\d+\.webp$/g, '')))]

function file(picture: string, width: (typeof captureWidths)[number]) {
  const url = files[`../captures/${picture}-${width}.webp`]
  if (!url) throw new Error(`the picture "${picture}" has no ${width} px file`)
  return url
}

const everyWidth = (picture: string) => captureWidths.map((width) => `${file(picture, width)} ${width}w`).join(', ')

type Wall = { columns: number; rows: number; tiles: number[]; span: Record<number, string>; off: string; width: Record<number, string> }

// The two walls: a wide one of six columns and a narrow one of two, each eight rows deep. A
// tile is a square of so many cells a side, and a wall's tiles fill it exactly. `tiles` is
// also the order the server lays them out in. A picture dealt past a wall's last tile sits out.
const walls: { wide: Wall; narrow: Wall } = {
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

// What one page load deals, as one string the root element can hold: the pictures' order,
// then a tiling of each wall, each a list of tile sides in reading order. A tiling fills the
// wall's first empty cell with a tile of a side picked at random from those left that fit
// there, and starts over when none fits. This function and tileAt go into the page's script
// as text, so they use nothing outside themselves.
function deal(pictures: number, { wide, narrow }: typeof walls) {
  const order = Array.from({ length: pictures }, (_, place) => place)
  for (let last = pictures - 1; last > 0; last--) {
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
function tileAt(wideSide: number | undefined, narrowSide: number | undefined, { wide, narrow }: typeof walls) {
  return {
    className: `${wideSide ? wide.span[wideSide] : wide.off} ${narrowSide ? narrow.span[narrowSide] : narrow.off}`,
    sizes: `(min-width: 52.2rem) ${wideSide ? wide.width[wideSide] : '1px'}, ${narrowSide ? narrow.width[narrowSide] : '1px'}`,
  }
}

const parts = (dealt: string) => dealt.split(';').map((part) => part.split(',').map(Number))

// A page load deals once and keeps the deal on the root element. This runs in the server's
// HTML, right after the list: it deals, then gives each tile its size and its picture
// before hydration.
const dealPictures = `{
  const deal = ${deal}
  const tileAt = ${tileAt}
  const parts = ${parts}
  const walls = ${JSON.stringify(walls)}
  const tiles = [...document.currentScript.previousElementSibling.children]
  const pictures = tiles.map((tile) => tile.querySelector('img'))
  const shots = pictures.map((picture) => [picture.getAttribute('srcset'), picture.getAttribute('src'), picture.alt])
  const [order, wide, narrow] = parts((document.documentElement.dataset.deal ??= deal(tiles.length, walls)))
  order.forEach((shot, place) => {
    const { className, sizes } = tileAt(wide[place], narrow[place], walls)
    const [srcset, src, alt] = shots[shot]
    tiles[place].className = className
    Object.assign(pictures[place], { sizes, srcset, src, alt })
  })
}`

// A tile and the viewer showing the same picture share a name, so React moves one into the
// other. default="none" keeps every other tile out of the transition: left to its default,
// React lifts each clipped element near a change into a layer of its own above the page.
const named = (picture: string) => `shot-${picture}`

// The server renders the pictures in the order of their names, on each wall's listed tiles. In
// the browser the script above has dealt by the time React hydrates the page, so the first
// render there uses that deal. A visit that arrives by a link has no server HTML, and deals here.
const readDeal = createIsomorphicFn().client(() => (document.documentElement.dataset.deal ??= deal(pictures.length, walls)))
const listed = [pictures.map((_, place) => place), walls.wide.tiles, walls.narrow.tiles]

const control = 'press grid size-11 place-items-center rounded-full bg-silhouette/70 text-starlight hover:bg-silhouette'

export function Wall({ words }: { words: Copy['wall'] }) {
  const viewer = useRef<HTMLDialogElement>(null)
  const [spot, setSpot] = useState<{ picture: string; open: boolean }>()

  const fromPage = readDeal()
  const [order, wideTiling, narrowTiling] = fromPage ? parts(fromPage) : listed
  const dealt = order.map((picture) => pictures[picture])
  const tile = (place: number) => tileAt(wideTiling[place], narrowTiling[place], walls)
  const alt = (picture: string) => words.alt[picture] ?? picture.replaceAll('-', ' ')
  const open = spot?.open === true

  // A layout effect runs inside React's view transition, so the new state is pictured with the dialog open.
  useLayoutEffect(() => {
    if (open) viewer.current?.showModal()
    else viewer.current?.close()
  }, [open])

  function view(picture: string) {
    startTransition(() => setSpot({ picture, open: true }))
  }

  function close() {
    startTransition(() => setSpot((now) => (now?.open ? { ...now, open: false } : now)))
  }

  function step(by: -1 | 1) {
    startTransition(() => setSpot((now) => now && { picture: dealt[(dealt.indexOf(now.picture) + by + dealt.length) % dealt.length], open: true }))
  }

  function turn(event: KeyboardEvent) {
    if (event.key === 'ArrowLeft') step(-1)
    if (event.key === 'ArrowRight') step(1)
  }

  return (
    <>
      <div className="@container">
        <ul className="grid grid-flow-dense auto-rows-[calc((100cqi-0.5rem)/2*9/16)] grid-cols-2 gap-2 @min-[48rem]:auto-rows-[calc((100cqi-3.75rem)/6*9/16)] @min-[48rem]:grid-cols-6 @min-[48rem]:gap-3">
          {dealt.map((picture, place) => {
            const { className, sizes } = tile(place)
            return (
              <li key={place} className={className}>
                {/* Hiding the tile while the viewer shows its picture is what pairs its boundary with the viewer's. */}
                <Activity mode={open && picture === spot?.picture ? 'hidden' : 'visible'}>
                  <ViewTransition name={named(picture)} default="none" share="shot">
                    <button
                      type="button"
                      onClick={() => view(picture)}
                      className="press block size-full cursor-zoom-in overflow-clip rounded-lg border border-mist/15 hover:border-moon"
                    >
                      <img
                        src={file(picture, 960)}
                        srcSet={everyWidth(picture)}
                        sizes={sizes}
                        width={960}
                        height={540}
                        alt={alt(picture)}
                        loading="lazy"
                        decoding="async"
                        className="size-full object-cover"
                      />
                    </button>
                  </ViewTransition>
                </Activity>
              </li>
            )
          })}
        </ul>
        <ScriptOnce>{dealPictures}</ScriptOnce>
      </div>

      <dialog
        ref={viewer}
        aria-label={words.viewer}
        onCancel={(event) => {
          event.preventDefault()
          close()
        }}
        onClick={(event) => {
          if (event.target === event.currentTarget) close()
        }}
        onClose={close}
        onKeyDown={turn}
        className="h-dvh max-h-none w-screen max-w-none bg-transparent backdrop:bg-silhouette/90 open:grid open:place-items-center"
      >
        {spot?.open ? (
          <div className="relative">
            {/* Each picture has a boundary of its own: one that changed its name to the next picture's
                would share that name with the picture's tile for a moment, which React does not allow.
                So on a step the picture shown goes back to its tile as the next comes out of its own. */}
            <ViewTransition key={spot.picture} name={named(spot.picture)} default="none" share="shot">
              {/* The box has its size before any file arrives. The file its tile shows, which the browser
                  already holds, fills it until the full one comes. A lazy picture does not hold the
                  transition back, so the viewer opens at the click. */}
              <div className="grid aspect-video w-[min(100vw-2*var(--spacing-gutter),(100dvh-2*var(--spacing-gutter))*16/9)] overflow-clip rounded-xl border border-mist/15 *:size-full *:object-cover *:[grid-area:1/1]">
                <img src={file(spot.picture, 960)} srcSet={everyWidth(spot.picture)} sizes={tile(dealt.indexOf(spot.picture)).sizes} alt="" loading="lazy" />
                <img src={file(spot.picture, 1920)} srcSet={everyWidth(spot.picture)} sizes="100vw" alt={alt(spot.picture)} loading="lazy" />
              </div>
            </ViewTransition>
            <button type="button" aria-label={words.close} onClick={close} className={`${control} absolute inset-bs-3 inset-e-3`}>
              <svg viewBox="0 0 20 20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" className="size-5">
                <path d="m5 5 10 10M15 5 5 15" />
              </svg>
            </button>
            <button type="button" aria-label={words.previous} onClick={() => step(-1)} className={`${control} absolute inset-s-3 inset-bs-[calc(50%-1.375rem)]`}>
              <Chevron className="size-5 -scale-x-100" />
            </button>
            <button type="button" aria-label={words.next} onClick={() => step(1)} className={`${control} absolute inset-e-3 inset-bs-[calc(50%-1.375rem)]`}>
              <Chevron className="size-5" />
            </button>
          </div>
        ) : null}
      </dialog>
    </>
  )
}

function Chevron({ className }: { className: string }) {
  return (
    <svg viewBox="0 0 20 20" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" className={className}>
      <path d="m7.5 4 6 6-6 6" />
    </svg>
  )
}
