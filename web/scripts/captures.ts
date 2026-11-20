// Makes src/captures hold the pictures of a folder of screenshots, each at the wall's
// widths: writes every picture in the folder, and removes the ones no longer in it.
// Usage: bun run captures <folder of screenshots>
import { rm } from 'node:fs/promises'
import { basename, extname, join } from 'node:path'
import { captureWidths } from '../src/capture-widths'
import { copy } from '../src/copy'

const [folder] = Bun.argv.slice(2)
if (!folder) throw new Error('usage: bun run captures <folder of screenshots>')

const captures = join(import.meta.dir, '../src/captures')
const widest = Math.max(...captureWidths)
const pictures: string[] = []
const tooSmall: string[] = []

for (const entry of (await Array.fromAsync(new Bun.Glob('*.{png,jpg,jpeg,webp}').scan(folder))).sort()) {
  const source = Bun.file(join(folder, entry))
  const { width } = await source.image().metadata()
  if (width < widest) {
    tooSmall.push(`${entry} (${width} px wide)`)
    continue
  }
  const picture = basename(entry, extname(entry))
  pictures.push(picture)
  // The wall and the viewer crop a picture to 16:9, so one of another shape keeps its own.
  await Promise.all(
    captureWidths.map((to) =>
      source
        .image()
        .resize(to, (to * 9) / 16, { fit: 'inside' })
        .webp({ quality: 75 })
        .write(join(captures, `${picture}-${to}.webp`)),
    ),
  )
}

const removed = new Set<string>()
for (const file of await Array.fromAsync(new Bun.Glob('*.webp').scan(captures))) {
  const picture = file.replace(/-\d+\.webp$/, '')
  if (pictures.includes(picture)) continue
  await rm(join(captures, file))
  removed.add(picture)
}

const list = (names: Iterable<string>) => [...names].join(', ')
console.log(`${pictures.length} pictures on the wall`)
if (removed.size) console.log(`removed: ${list(removed)}`)
if (tooSmall.length) console.log(`left out, under ${widest} px wide: ${list(tooSmall)}`)
const noAlt = pictures.filter((picture) => !copy.wall.alt[picture])
if (noAlt.length) console.log(`no alt text in src/copy.ts yet, so the name is read out: ${list(noAlt)}`)
const gone = Object.keys(copy.wall.alt).filter((picture) => !pictures.includes(picture))
if (gone.length) console.log(`alt text in src/copy.ts for a picture that is gone: ${list(gone)}`)
