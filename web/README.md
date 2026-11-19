# Kaepora website

The site for Kaepora: a homepage, a download page with the changelog, and an FAQ. It is a TanStack Start app with React and Tailwind CSS, and it runs on Vercel's Bun runtime.

## Run it locally

```sh
bun install
bun run dev
```

`bun run typecheck` checks the types. `bun run test` runs the tests. `bun run build` writes the production build to `.output/`.

`.github/workflows/web.yml` runs those three checks with Bun 1.4.4 on each pull request and each push to `main` that changes `web/`.

- The theme (colours, fonts, sizes) is in `src/styles.css`. The drawings are in `src/components/night.tsx`.
- The download addresses are in `src/release.ts`.
- The download page reads `CHANGELOG.md` from the root of the repository at build time and renders it with Bun's built-in Markdown. Until that file exists, the page says there are no releases yet.
- The FAQ page reads `FAQ.md` the same way. That file must exist, or the build fails.
- `bun run link-preview` builds the site and photographs the `/link-preview` card into `public/link-preview.png`, the image link previews show. Run it after changing the card or `copy.preview.line`.

## Deploy

The Vercel project is `kaepora` in the team `pixelhabits-projects`, connected to this repository with `web` as its root directory. Every push deploys a preview, and a push to `main` deploys production. The build reaches outside `web` for `CHANGELOG.md` and `FAQ.md`, which the project allows.

- `vercel.json` sets `bunVersion` to `1.4.x`, so the server runs on Bun 1.4.
- The Nitro plugin in `vite.config.ts` builds the server. On Vercel it builds for Vercel and takes the Bun version from `vercel.json`.
