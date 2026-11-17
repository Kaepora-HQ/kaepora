import { resolve } from 'node:path'
import tailwindcss from '@tailwindcss/vite'
import { tanstackStart } from '@tanstack/react-start/plugin/vite'
import viteReact from '@vitejs/plugin-react'
import { nitro } from 'nitro/vite'
import { type Plugin, defineConfig } from 'vite'

function changelog(): Plugin {
  const id = '\0virtual:changelog'
  let file = ''
  return {
    name: 'kaepora:changelog',
    configResolved: (config) => {
      file = resolve(config.root, '../CHANGELOG.md')
    },
    resolveId: (source) => (source === 'virtual:changelog' ? id : undefined),
    async load(loading) {
      if (loading !== id) return
      this.addWatchFile(file)
      const markdown = Bun.file(file)
      if (!(await markdown.exists())) return 'export default null'
      const html = Bun.markdown.html((await markdown.text()).replace(/^# Changelog\s*/, ''))
      return `export default ${JSON.stringify(html)}`
    },
  }
}

export default defineConfig({
  resolve: { tsconfigPaths: true },
  build: { license: true },
  plugins: [
    changelog(),
    tailwindcss(),
    tanstackStart({ prerender: { enabled: true } }),
    nitro(),
    viteReact({ compiler: true }),
  ],
})
