import type { ReactNode } from 'react'
import { PageHero, SiteFooter, SiteHeader } from '@/components/site-chrome'
import { copy } from '@/copy'

export function ArticlePage({
  title,
  contents,
  html,
  sections,
  children,
}: {
  title: string
  contents: string
  html: string
  sections: readonly { id: string; title: string }[]
  children?: ReactNode
}) {
  return (
    <>
      <SiteHeader />
      <main>
        <PageHero title={title} />
        <section className="@container bg-silhouette px-gutter pbs-6 pbe-section">
          <div className="mx-auto grid max-w-page gap-x-16 gap-y-10 @min-[50rem]:grid-cols-[minmax(12rem,22rem)_minmax(0,38rem)]">
            <nav aria-label={contents} className="@min-[50rem]:sticky @min-[50rem]:inset-bs-8 @min-[50rem]:self-start">
              <ul className="flex flex-col gap-4">
                {sections.map(({ id, title }) => (
                  <li key={id}>
                    <a href={`#${id}`} className="text-mist hover:text-starlight active:text-moon motion-safe:transition-colors">
                      {title}
                    </a>
                  </li>
                ))}
              </ul>
            </nav>
            <div className="flex max-w-[38rem] flex-col gap-10">
              {children}
              <div className="markdown article" dangerouslySetInnerHTML={{ __html: html }} />
            </div>
          </div>
        </section>
      </main>

      <SiteFooter words={copy.footer} />
    </>
  )
}
