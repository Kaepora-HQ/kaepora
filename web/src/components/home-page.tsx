import { FoldedMap, Lanterns, MoonAndOwl, OpenChest, Signpost, Treeline } from '@/components/night'
import { DownloadCall, SiteFooter, SiteHeader } from '@/components/site-chrome'
import type { Copy } from '@/copy'

const drawings = [OpenChest, Signpost, Lanterns, FoldedMap] as const

export function HomePage({ copy }: { copy: Copy }) {
  return (
    <>
      <SiteHeader />
      <main>
        <div className="bg-linear-to-b from-midnight via-midnight to-dusk">
          <section className="flex flex-col items-center gap-6 px-gutter text-center">
            <MoonAndOwl className="-mbe-6 w-full max-w-[34rem]" />
            <h1 className="storybook max-w-[15ch] text-display font-normal tracking-tight">{copy.hero.headline}</h1>
            <p className="max-w-[34rem] text-lead text-mist">{copy.hero.lead}</p>
            <DownloadCall words={copy.download} />
          </section>
          <Treeline className="mbs-16 block h-[clamp(7rem,4rem+10vw,13rem)] w-full" />
        </div>

        <section aria-labelledby="adventure" className="bg-silhouette px-gutter py-section">
          <div className="mx-auto max-w-2xl">
            <h2 id="adventure" className="storybook text-center text-title font-normal">
              {copy.middle.heading}
            </h2>
            <ul className="mbs-14 flex flex-col">
              {copy.middle.moments.map(({ title, text }, index) => {
                const Drawing = drawings[index]
                return (
                  <li
                    key={title}
                    className="[&+&]:before:mx-auto [&+&]:before:my-10 [&+&]:before:block [&+&]:before:size-2 [&+&]:before:rotate-45 [&+&]:before:bg-moon/70 [&+&]:before:content-['']"
                  >
                    <div className="flex flex-wrap items-start gap-x-8 gap-y-3">
                      <Drawing className="w-24 flex-none" />
                      <div className="flex min-w-[15rem] flex-1 flex-col gap-2">
                        <h3 className="storybook text-heading font-medium">{title}</h3>
                        <p className="text-mist">{text}</p>
                      </div>
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
          <DownloadCall words={copy.download} />
        </section>
      </main>

      <SiteFooter words={copy.footer} />
    </>
  )
}
