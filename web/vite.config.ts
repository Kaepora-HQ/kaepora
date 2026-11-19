import { resolve } from 'node:path'
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
      const sections: { id: string; title: string }[] = []
      Bun.markdown.render(
        text,
        {
          heading: (title, { level, id }) => {
            if (level !== 2) return
            if (!id) throw new Error(`${file}: the heading "${title}" has no id`)
            sections.push({ id, title })
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

export default defineConfig({
  resolve: { tsconfigPaths: true },
  build: { license: true },
  plugins: [
    repositoryMarkdown({ file: 'CHANGELOG.md', module: 'virtual:changelog', optional: true }),
    repositoryMarkdown({ file: 'FAQ.md', module: 'virtual:faq' }),
    tailwindcss(),
    tanstackStart({ prerender: { enabled: true } }),
    nitro(),
    viteReact({ compiler: true }),
  ],
})
