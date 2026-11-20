import { createFileRoute } from '@tanstack/react-router'
import { ArticlePage } from '@/components/article-page'
import { copy } from '@/copy'
import counts from 'virtual:counts'
import randomized, { sections } from 'virtual:randomized'

export const Route = createFileRoute('/randomized')({
  head: () => ({ meta: [{ title: `${copy.randomized.title} | Kaepora` }] }),
  component: Randomized,
})

function Randomized() {
  const words = copy.randomized
  const rows = [
    [words.chests, `${counts.chests.shuffled} ${words.of} ${counts.chests.total}`],
    [words.stones, String(counts.stones)],
    ...words.fixedRows,
  ]
  return (
    <ArticlePage title={words.title} contents={words.contents} html={randomized} sections={sections}>
      <div className="markdown">
        <table>
          <caption className="sr-only">{words.counts}</caption>
          <tbody>
            {rows.map(([what, howMany]) => (
              <tr key={what}>
                <th scope="row">{what}</th>
                <td>{howMany}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </ArticlePage>
  )
}
