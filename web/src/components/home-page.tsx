import { FoldedMap, Lanterns, MoonAndOwl, OpenChest, Signpost, Treeline } from '@/components/night'
import { DownloadCall, SiteFooter, SiteHeader } from '@/components/site-chrome'
import type { Copy } from '@/copy'

const drawings = [OpenChest, Signpost, Lanterns, FoldedMap] as const

export function HomePage({ copy }: { copy: Copy }) {
  return (
    <>
      <SiteHeader />
      <main>
        <div className="@container/sky overflow-clip bg-linear-to-b from-midnight via-midnight to-dusk">
          {/* The left edge is the page container's, and the drawing may run to the right edge. */}
          <section className="grid items-center gap-x-12 gap-y-6 ps-edge pe-gutter @min-[60rem]/sky:grid-cols-[minmax(0,1.1fr)_minmax(0,1fr)] @min-[60rem]/sky:pbs-8">
            <div className="relative mx-auto -mbe-6 w-full max-w-[34rem] @min-[60rem]/sky:order-last @min-[60rem]/sky:me-0 @min-[60rem]/sky:mbe-0 @min-[60rem]/sky:max-w-[44rem]">
              {/* The stars stop short of the words beside and below the moon. */}
              <div aria-hidden="true" className="starfield absolute -inset-bs-[30%] -inset-e-[45%] inset-be-[5%] -inset-s-[8%]" />
              <MoonAndOwl className="relative w-full" />
            </div>
            <div className="@container flex flex-col items-center gap-6 text-center @min-[60rem]/sky:items-start @min-[60rem]/sky:text-start">
              <h1 className="storybook text-display font-normal">{copy.hero.headline}</h1>
              <p className="max-w-[36rem] text-lead">{copy.hero.lead}</p>
              <DownloadCall words={copy.download} className="justify-center @min-[60rem]/sky:justify-start" />
            </div>
          </section>
          <Treeline className="mbs-16 block h-[clamp(7rem,4rem+10vw,13rem)] w-full" />
        </div>

        <section aria-labelledby="adventure" className="bg-silhouette px-gutter py-section">
          <div className="mx-auto max-w-page">
            <h2 id="adventure" className="storybook text-title font-normal">
              {copy.middle.heading}
            </h2>
            <ul className="mbs-14 grid grid-cols-[repeat(auto-fit,minmax(min(100%,26rem),1fr))] gap-x-16 gap-y-12">
              {copy.middle.moments.map(({ title, text }, index) => {
                const Drawing = drawings[index]
                return (
                  <li key={title} className="flex flex-wrap items-start gap-x-6 gap-y-3">
                    <Drawing className="w-20 flex-none" />
                    <div className="flex min-w-[17rem] flex-1 flex-col gap-2">
                      <h3 className="storybook text-heading font-normal">{title}</h3>
                      <p className="max-w-[34rem] text-mist">{text}</p>
                    </div>
                  </li>
                )
              })}
            </ul>
          </div>
        </section>

        <section
          aria-labelledby="ready"
          className="flex flex-col items-center gap-8 bg-silhouette px-gutter pbe-section text-center"
        >
          <h2 id="ready" className="storybook text-title font-normal">
            {copy.closing.heading}
          </h2>
          <DownloadCall words={copy.download} className="justify-center" />
        </section>
      </main>

      <SiteFooter words={copy.footer} />
    </>
  )
}
