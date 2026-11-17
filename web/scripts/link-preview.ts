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
  await view.navigate(card)
  // Headless Chrome counts its window frame in the height; pin the page area.
  await view.cdp('Emulation.setDeviceMetricsOverride', { ...previewCard, deviceScaleFactor: 1, mobile: false })
  await view.cdp('Emulation.setScrollbarsHidden', { hidden: true })
  await view.evaluate('document.fonts.ready.then(() => true)')
  const png = await view.screenshot({ format: 'png', encoding: 'buffer' })
  await Bun.write('public/link-preview.png', png)
  console.log(`wrote public/link-preview.png (${png.length} bytes)`)
} finally {
  server.kill()
}
