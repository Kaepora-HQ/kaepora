import { createFileRoute } from '@tanstack/react-router'
import { ArticlePage } from '@/components/article-page'
import { copy } from '@/copy'
import faq, { sections } from 'virtual:faq'

export const Route = createFileRoute('/faq')({
  head: () => ({ meta: [{ title: `${copy.faqPage.title} | Kaepora` }] }),
  component: () => <ArticlePage title={copy.faqPage.title} contents={copy.faqPage.questions} html={faq} sections={sections} />,
})
