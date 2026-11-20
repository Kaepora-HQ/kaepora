import { ScriptOnce } from '@tanstack/react-router'
import { createIsomorphicFn } from '@tanstack/react-start'
import { Activity, type KeyboardEvent, ViewTransition, startTransition, useLayoutEffect, useRef, useState } from 'react'
import { captureWidths } from '@/capture-widths'
import type { Copy } from '@/copy'
import { deal, parts, tileAt, walls } from '@/deal'

// Every picture in src/captures is on the wall. scripts/captures.ts writes each one at every width.
const files = import.meta.glob<string>('../captures/*.webp', { eager: true, import: 'default' })
const pictures = [...new Set(Object.keys(files).map((path) => path.replace(/^.*\/|-\d+\.webp$/g, '')))]

function file(picture: string, width: (typeof captureWidths)[number]) {
  const url = files[`../captures/${picture}-${width}.webp`]
  if (!url) throw new Error(`the picture "${picture}" has no ${width} px file`)
  return url
}

const everyWidth = (picture: string) => captureWidths.map((width) => `${file(picture, width)} ${width}w`).join(', ')

// A page load deals once and keeps the deal on the root element. Two scripts in the server's
// HTML do it before hydration. A browser can draw the list before the script after it has
// run, so the first, before the list, marks the page as dealing, and the wall stays unseen
// and loads no picture until the second has given each tile its size and its picture.
const startDealing = `document.documentElement.dataset.dealing = ''`
const dealPictures = `try {
  const deal = ${deal}
  const tileAt = ${tileAt}
  const parts = ${parts}
  const walls = ${JSON.stringify(walls)}
  const tiles = [...document.currentScript.previousElementSibling.children]
  const images = tiles.map((tile) => tile.querySelector('img'))
  const shots = images.map((image) => [image.getAttribute('srcset'), image.getAttribute('src'), image.alt])
  const [order, wide, narrow] = parts((document.documentElement.dataset.deal ??= deal(tiles.length, walls)))
  order.forEach((shot, place) => {
    const { className, sizes } = tileAt(wide[place], narrow[place], walls)
    const [srcset, src, alt] = shots[shot]
    tiles[place].className = className
    Object.assign(images[place], { sizes, srcset, src, alt })
  })
} finally {
  delete document.documentElement.dataset.dealing
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
        <ScriptOnce>{startDealing}</ScriptOnce>
        <ul className="grid auto-rows-[calc((100cqi-0.5rem)/2*9/16)] grid-cols-2 gap-2 @min-[48rem]:auto-rows-[calc((100cqi-3.75rem)/6*9/16)] @min-[48rem]:grid-cols-6 @min-[48rem]:gap-3 [[data-dealing]_&]:invisible">
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
                        className="size-full object-cover [[data-dealing]_&]:hidden"
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
