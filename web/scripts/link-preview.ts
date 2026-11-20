import { previewCard } from '../src/preview-card'

const port = 4799
const server = Bun.spawn(['bun', '.output/server/index.mjs'], {
  env: { ...process.env, PORT: String(port) },
  stdout: 'ignore',
})

try {
  const card = `http://localhost:${port}/link-preview`
  for (let tries = 0; !(await fetch(card).then((r) => r.ok, () => false)); tries++) {
    if (tries === 50) throw new Error(`the built site did not answer at ${card}`)
    await Bun.sleep(100)
  }

  await using view = new Bun.WebView({ ...previewCard, backend: { type: 'chrome', url: false } })
  await view.navigate('about:blank')
  // Headless Chrome counts its window frame in the height; pin the page area. Mobile
  // emulation draws overlay scrollbars, so the page's scrollbar gutter takes no width.
  await view.cdp('Emulation.setDeviceMetricsOverride', { ...previewCard, deviceScaleFactor: 1, mobile: true })
  await view.navigate(card)
  await view.evaluate('document.fonts.ready.then(() => true)')
  const png = await view.screenshot({ format: 'png', encoding: 'buffer' })
  await Bun.write('public/link-preview.png', png)
  console.log(`wrote public/link-preview.png (${png.length} bytes)`)
} finally {
  server.kill()
}
