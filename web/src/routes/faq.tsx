import { createFileRoute } from '@tanstack/react-router'
import { PageHero, SiteFooter, SiteHeader } from '@/components/site-chrome'
import { copy } from '@/copy'
import faq, { sections } from 'virtual:faq'

export const Route = createFileRoute('/faq')({
  head: () => ({ meta: [{ title: `${copy.faqPage.title} | Kaepora` }] }),
  component: Faq,
})

function Faq() {
  return (
    <>
      <SiteHeader />
      <main>
        <PageHero title={copy.faqPage.title} />
        <section className="@container bg-silhouette px-gutter pbs-6 pbe-section">
          <div className="mx-auto grid max-w-5xl gap-x-16 gap-y-10 @min-[50rem]:grid-cols-[minmax(12rem,1fr)_minmax(0,38rem)]">
            <nav
              aria-label={copy.faqPage.questions}
              className="@min-[50rem]:sticky @min-[50rem]:inset-bs-8 @min-[50rem]:self-start"
            >
              <ul className="flex flex-col gap-4">
                {sections.map(({ id, title }) => (
                  <li key={id}>
                    <a
                      href={`#${id}`}
                      className="text-mist hover:text-starlight active:text-moon motion-safe:transition-colors"
                    >
                      {title}
                    </a>
                  </li>
                ))}
              </ul>
            </nav>
            <div className="markdown faq max-w-[38rem]" dangerouslySetInnerHTML={{ __html: faq }} />
          </div>
        </section>
      </main>

      <SiteFooter words={copy.footer} />
    </>
  )
}
