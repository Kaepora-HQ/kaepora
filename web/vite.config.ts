import { join, resolve } from 'node:path'
import tailwindcss from '@tailwindcss/vite'
import { tanstackStart } from '@tanstack/react-start/plugin/vite'
import viteReact from '@vitejs/plugin-react'
import { nitro } from 'nitro/vite'
import { type Plugin, defineConfig } from 'vite'

// A required file that is missing fails the build, because reading it throws.
function repositoryMarkdown({ file, module, optional = false }: { file: string; module: string; optional?: boolean }): Plugin {
  const id = `\0${module}`
  let path = ''
  return {
    name: `kaepora:${module}`,
    configResolved: (config) => {
      path = resolve(config.root, '..', file)
    },
    // Vite's dev server watches a file outside its root only once it exists.
    configureServer: (server) => {
      server.watcher.add(path)
    },
    resolveId: (source) => (source === module ? id : undefined),
    async load(loading) {
      if (loading !== id) return
      const markdown = Bun.file(path)
      if (optional && !(await markdown.exists())) return 'export default null'
      this.addWatchFile(path)
      const text = (await markdown.text()).replace(/^# .*\s*/, '')
      const options = { headings: { ids: true } }
      const sections: { id: string; title: string; summary: string }[] = []
      Bun.markdown.render(
        text,
        {
          heading: (title, { level, id }) => {
            if (level !== 2) return
            if (!id) throw new Error(`${file}: the heading "${title}" has no id`)
            sections.push({ id, title, summary: '' })
          },
          paragraph: (words) => {
            const section = sections.at(-1)
            if (section && !section.summary) section.summary = words
          },
        },
        options,
      )
      const html = Bun.markdown.html(text, options)
      return `export default ${JSON.stringify(html)}\nexport const sections = ${JSON.stringify(sections)}`
    },
    hotUpdate({ type, file: changed }) {
      if (type !== 'create' || changed !== path) return
      const loaded = this.environment.moduleGraph.getModuleById(id)
      return loaded ? [loaded] : undefined
    },
  }
}

// Counted from the generator's own tables at build time, so the page follows them.
function remakeCounts(): Plugin {
  const module = 'virtual:counts'
  const id = `\0${module}`
  let remake = ''
  async function table(file: string, columns: readonly string[]) {
    const [header, ...lines] = (await Bun.file(join(remake, file)).text()).trimEnd().split('\n')
    const names = header.split('\t')
    for (const column of columns) {
      if (!names.includes(column)) throw new Error(`remake/${file} has no "${column}" column`)
    }
    return lines.map((line) => Object.fromEntries(line.split('\t').map((cell, column) => [names[column], cell])))
  }
  return {
    name: 'kaepora:counts',
    configResolved: (config) => {
      remake = resolve(config.root, '..', 'remake')
    },
    resolveId: (source) => (source === module ? id : undefined),
    async load(loading) {
      if (loading !== id) return
      // A row with no hash is a place the remake has no chest in. Four places have two rows, because
      // the files keep their chest twice, and a chest with no location name is a place of its own.
      const chests = (await table('chests.tsv', ['location', 'hash', 'shuffled'])).filter((chest) => chest.hash)
      const places = (rows: typeof chests) => new Set(rows.map((chest) => chest.location || chest.hash)).size
      const stones = await table('stones.tsv', ['label'])
      const counts = {
        chests: { shuffled: places(chests.filter((chest) => chest.shuffled === 'yes')), total: places(chests) },
        stones: stones.length,
      }
      return `export default ${JSON.stringify(counts)}`
    },
  }
}

// Vercel answers this before its files and functions. Nitro's type for a route leaves out `has`, which Vercel reads.
const apiHostShowsOnlyTheApi = {
  src: '^/(?!v[0-9]+(?:/|$))(.*)$',
  has: [{ type: 'host', value: 'api.kaepora.org' }],
  status: 307,
  headers: { Location: 'https://kaepora.org/$1' },
}

export default defineConfig({
  resolve: { tsconfigPaths: true },
  build: { license: true },
  plugins: [
    repositoryMarkdown({ file: 'CHANGELOG.md', module: 'virtual:changelog', optional: true }),
    repositoryMarkdown({ file: 'FAQ.md', module: 'virtual:faq' }),
    repositoryMarkdown({ file: 'RANDOMIZED.md', module: 'virtual:randomized' }),
    remakeCounts(),
    tailwindcss(),
    tanstackStart({ prerender: { enabled: true } }),
    nitro({ vercel: { config: { version: 3, routes: [apiHostShowsOnlyTheApi] } } }),
    viteReact({ compiler: true }),
  ],
})
