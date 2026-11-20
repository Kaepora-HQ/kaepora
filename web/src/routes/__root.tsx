import fraunces from '@fontsource-variable/fraunces/files/fraunces-latin-full-normal.woff2?url'
import figtree from '@fontsource-variable/figtree/files/figtree-latin-wght-normal.woff2?url'
import { HeadContent, Link, Scripts, createRootRoute } from '@tanstack/react-router'
import type { ReactNode } from 'react'
import { copy } from '@/copy'
import { previewCard } from '@/preview-card'
import { markSystem } from '@/system'
import css from '../styles.css?url'

// Link previews need an absolute address for the image.
const origin = 'https://kaepora.org'

export const Route = createRootRoute({
  head: () => ({
    meta: [
      { charSet: 'utf-8' },
      { name: 'viewport', content: 'width=device-width, initial-scale=1' },
      { name: 'color-scheme', content: 'dark' },
      { title: 'Kaepora' },
      { name: 'description', content: copy.meta.description },
      { property: 'og:type', content: 'website' },
      { property: 'og:title', content: 'Kaepora' },
      { property: 'og:description', content: copy.meta.description },
      { property: 'og:image', content: `${origin}/link-preview.png` },
      { property: 'og:image:width', content: String(previewCard.width) },
      { property: 'og:image:height', content: String(previewCard.height) },
      { property: 'og:image:alt', content: `Kaepora. ${copy.preview.line}. An owl on a branch across the moon.` },
      { name: 'twitter:card', content: 'summary_large_image' },
    ],
    links: [
      { rel: 'preload', href: fraunces, as: 'font', type: 'font/woff2', crossOrigin: 'anonymous' },
      { rel: 'preload', href: figtree, as: 'font', type: 'font/woff2', crossOrigin: 'anonymous' },
      { rel: 'stylesheet', href: css },
      { rel: 'icon', type: 'image/svg+xml', href: '/favicon.svg' },
    ],
    scripts: [{ children: markSystem }],
  }),
  shellComponent: Document,
  notFoundComponent: NotFound,
})

function Document({ children }: { children: ReactNode }) {
  return (
    <html lang="en" suppressHydrationWarning>
      <head>
        <HeadContent />
      </head>
      <body>
        {children}
        <Scripts />
      </body>
    </html>
  )
}

function NotFound() {
  return (
    <main className="grid min-h-svh place-content-center gap-6 px-gutter text-center">
      <h1 className="storybook text-title font-normal">Nothing is where you left it</h1>
      <p className="text-mist">There is no page at this address.</p>
      <Link to="/" className="moon-button mx-auto px-7 py-3">
        Go to the Kaepora home page
      </Link>
    </main>
  )
}
