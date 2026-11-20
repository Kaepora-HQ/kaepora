import { ScriptOnce } from '@tanstack/react-router'
import { createIsomorphicFn } from '@tanstack/react-start'
import { Activity, type KeyboardEvent, ViewTransition, startTransition, useLayoutEffect, useRef, useState } from 'react'
import type { Copy } from '@/copy'

type Shot = Copy['wall']['shots'][number]

const files = import.meta.glob<string>('../captures/*.avif', { eager: true, import: 'default' })

function file(picture: string, width: 480 | 960 | 1920) {
  const url = files[`../captures/${picture}-${width}.avif`]
  if (!url) throw new Error(`the picture "${picture}" has no ${width} px file`)
  return url
}

const everyWidth = (picture: string) => `${file(picture, 480)} 480w, ${file(picture, 960)} 960w, ${file(picture, 1920)} 1920w`

// One row per place on the wall, in dealing order: its size on a wide wall of six columns,
// then on a narrow wall of two. A picture dealt past the last place sits out.
const onWall = [
  ['large', 'large'], ['medium', 'small'], ['small', 'small'], ['small', 'small'], ['small', 'small'], ['small', 'large'],
  ['small', 'small'], ['medium', 'small'], ['medium', 'small'], ['medium', 'small'], ['medium', 'none'], ['small', 'none'],
  ['large', 'none'], ['small', 'none'], ['small', 'none'], ['small', 'none'], ['small', 'none'],
] as const
const offWall = ['none', 'none'] as const

const wide = {
  large: { span: '@min-[48rem]:col-span-3 @min-[48rem]:row-span-3', width: 'min(38rem, 50vw)' },
  medium: { span: '@min-[48rem]:col-span-2 @min-[48rem]:row-span-2', width: 'min(25rem, 33vw)' },
  small: { span: '', width: 'min(13rem, 17vw)' },
  none: { span: '@min-[48rem]:hidden', width: '1px' },
}
const narrow = {
  large: { span: '@max-[48rem]:col-span-2 @max-[48rem]:row-span-2', width: '100vw' },
  small: { span: '', width: '50vw' },
  none: { span: '@max-[48rem]:hidden', width: '1px' },
}

const sizeAt = (place: number) => (place < onWall.length ? onWall[place] : offWall)
const sizesAt = (place: number) => `(min-width: 52.2rem) ${wide[sizeAt(place)[0]].width}, ${narrow[sizeAt(place)[1]].width}`

// The page's head deals a new order on every load (deal.ts). This runs in the server's
// HTML, right after the list, and puts each dealt picture in its place before hydration.
const dealPictures = `{
  const pictures = [...document.currentScript.previousElementSibling.querySelectorAll('img')]
  const shots = pictures.map((picture) => [picture.getAttribute('srcset'), picture.getAttribute('src'), picture.alt])
  document.documentElement.dataset.deal.split(',').forEach((shot, place) => {
    const [srcset, src, alt] = shots[shot]
    Object.assign(pictures[place], { srcset, src, alt })
  })
}`

// A tile and the viewer showing the same picture share a name, so React moves one into the
// other. default="none" keeps every other tile out of the transition: left to its default,
// React lifts each clipped element near a change into a layer of its own above the page.
const named = (shot: Shot) => `shot-${shot.picture}`

// The server renders the listed order. In the browser the script above has dealt the pictures
// in the page by the time React hydrates it, so the first render there uses the dealt order.
const readDeal = createIsomorphicFn().client(() => document.documentElement.dataset.deal)

const control = 'press grid size-11 place-items-center rounded-full bg-silhouette/70 text-starlight hover:bg-silhouette'

export function Wall({ words }: { words: Copy['wall'] }) {
  const viewer = useRef<HTMLDialogElement>(null)
  const [spot, setSpot] = useState<{ shot: Shot; open: boolean }>()

  const dealt = readDeal()?.split(',').map((shot) => words.shots[Number(shot)]) ?? words.shots
  const open = spot?.open === true

  // A layout effect runs inside React's view transition, so the new state is pictured with the dialog open.
  useLayoutEffect(() => {
    if (open) viewer.current?.showModal()
    else viewer.current?.close()
  }, [open])

  function view(shot: Shot) {
    startTransition(() => setSpot({ shot, open: true }))
  }

  function close() {
    startTransition(() => setSpot((now) => (now?.open ? { ...now, open: false } : now)))
  }

  function step(by: -1 | 1) {
    startTransition(() => setSpot((now) => now && { shot: dealt[(dealt.indexOf(now.shot) + by + dealt.length) % dealt.length], open: true }))
  }

  function turn(event: KeyboardEvent) {
    if (event.key === 'ArrowLeft') step(-1)
    if (event.key === 'ArrowRight') step(1)
  }

  return (
    <>
      <div className="@container">
        <ul className="grid grid-flow-dense auto-rows-[calc((100cqi-0.5rem)/2*9/16)] grid-cols-2 gap-2 @min-[48rem]:auto-rows-[calc((100cqi-3.75rem)/6*9/16)] @min-[48rem]:grid-cols-6 @min-[48rem]:gap-3">
          {dealt.map((shot, place) => {
            const [onWide, onNarrow] = sizeAt(place)
            return (
              <li key={place} className={`${wide[onWide].span} ${narrow[onNarrow].span}`}>
                {/* Hiding the tile while the viewer shows its picture is what pairs its boundary with the viewer's. */}
                <Activity mode={open && shot === spot?.shot ? 'hidden' : 'visible'}>
                  <ViewTransition name={named(shot)} default="none" share="shot">
                    <button
                      type="button"
                      onClick={() => view(shot)}
                      className="press block size-full cursor-zoom-in overflow-clip rounded-lg border border-mist/15 hover:border-moon"
                    >
                      <img
                        src={file(shot.picture, 960)}
                        srcSet={everyWidth(shot.picture)}
                        sizes={sizesAt(place)}
                        width={960}
                        height={540}
                        alt={shot.alt}
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
            <ViewTransition key={spot.shot.picture} name={named(spot.shot)} default="none" share="shot">
              {/* The box has its size before any file arrives. The file its tile shows, which the browser
                  already holds, fills it until the full one comes. A lazy picture does not hold the
                  transition back, so the viewer opens at the click. */}
              <div className="grid aspect-video w-[min(100vw-2*var(--spacing-gutter),(100dvh-2*var(--spacing-gutter))*16/9)] overflow-clip rounded-xl border border-mist/15 *:size-full *:[grid-area:1/1]">
                <img src={file(spot.shot.picture, 960)} srcSet={everyWidth(spot.shot.picture)} sizes={sizesAt(dealt.indexOf(spot.shot))} alt="" loading="lazy" />
                <img src={file(spot.shot.picture, 1920)} srcSet={everyWidth(spot.shot.picture)} sizes="100vw" alt={spot.shot.alt} loading="lazy" />
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
