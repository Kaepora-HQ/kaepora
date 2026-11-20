import { Link } from '@tanstack/react-router'
import type { ReactNode } from 'react'
import { Treeline } from '@/components/night'
import { OwlMark } from '@/components/owl-mark'
import type { Copy } from '@/copy'
import { repository, systems } from '@/release'
import { homeOffers } from '@/system'

const shipOfHarkinian = 'https://www.shipofharkinian.com/'
// This invite gives the Beta Tester role.
const discord = 'https://discord.gg/HyeadS8bzc'

const iconLink = 'press tap-target text-mist hover:text-starlight'
const menuLink = 'quiet-link px-4 py-3'

export function SiteHeader() {
  return (
    <header className="@container mx-auto box-content flex max-w-page flex-wrap items-center gap-x-3 gap-y-2 px-gutter py-5">
      <Link to="/" className="storybook inline-flex items-center gap-2.5 text-[clamp(1.375rem,1.1rem+1.4vw,1.625rem)] leading-none font-semibold">
        <OwlMark className="size-[1.25em] flex-none text-mist" />
        Kaepora
      </Link>
      <nav aria-label="Site" className="ms-auto flex items-center gap-2 text-small font-bold [--control-size:calc(1lh+--spacing(4))]">
        <Link to="/faq" className="quiet-link text-mist @max-[26rem]:hidden">
          FAQ
        </Link>
        <Link to="/download" className="press inline-flex h-(--control-size) items-center rounded-full bg-starlight/10 px-3.5 hover:bg-starlight/15">
          Download
        </Link>
        <a href={discord} aria-label="Kaepora on Discord" className={`${iconLink} @max-[26rem]:hidden`}>
          <svg viewBox="0 0 24 24" aria-hidden="true" className="block size-(--control-size)">
            <path fill="currentColor" d="M20.317 4.3698a19.7913 19.7913 0 00-4.8851-1.5152.0741.0741 0 00-.0785.0371c-.211.3753-.4447.8648-.6083 1.2495-1.8447-.2762-3.68-.2762-5.4868 0-.1636-.3933-.4058-.8742-.6177-1.2495a.077.077 0 00-.0785-.037 19.7363 19.7363 0 00-4.8852 1.515.0699.0699 0 00-.0321.0277C.5334 9.0458-.319 13.5799.0992 18.0578a.0824.0824 0 00.0312.0561c2.0528 1.5076 4.0413 2.4228 5.9929 3.0294a.0777.0777 0 00.0842-.0276c.4616-.6304.8731-1.2952 1.226-1.9942a.076.076 0 00-.0416-.1057c-.6528-.2476-1.2743-.5495-1.8722-.8923a.077.077 0 01-.0076-.1277c.1258-.0943.2517-.1923.3718-.2914a.0743.0743 0 01.0776-.0105c3.9278 1.7933 8.18 1.7933 12.0614 0a.0739.0739 0 01.0785.0095c.1202.099.246.1981.3728.2924a.077.077 0 01-.0066.1276 12.2986 12.2986 0 01-1.873.8914.0766.0766 0 00-.0407.1067c.3604.698.7719 1.3628 1.225 1.9932a.076.076 0 00.0842.0286c1.961-.6067 3.9495-1.5219 6.0023-3.0294a.077.077 0 00.0313-.0552c.5004-5.177-.8382-9.6739-3.5485-13.6604a.061.061 0 00-.0312-.0286zM8.02 15.3312c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9555-2.4189 2.157-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.9555 2.4189-2.1569 2.4189zm7.9748 0c-1.1825 0-2.1569-1.0857-2.1569-2.419 0-1.3332.9554-2.4189 2.1569-2.4189 1.2108 0 2.1757 1.0952 2.1568 2.419 0 1.3332-.946 2.4189-2.1568 2.4189Z" />
          </svg>
        </a>
        <a href={repository} aria-label="Kaepora on GitHub" className={`${iconLink} ms-1 @max-[26rem]:hidden`}>
          <GitHubMark className="block size-[calc(var(--control-size)-0.25rem)]" />
        </a>
        {/* The button and its menu leave together, so an open menu cannot stay behind on a header that grew wide. */}
        <div className="contents @min-[26rem]:hidden">
          <button type="button" popoverTarget="site-menu" aria-label="Menu" className={`${iconLink} cursor-pointer`}>
            <svg viewBox="0 0 24 24" aria-hidden="true" className="block size-(--control-size)">
              <path fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" d="M4 7h16M4 12h16M4 17h16" />
            </svg>
          </button>
          {/* A link in the menu can lead to a page that keeps this header, so a click closes the menu. */}
          <div
            id="site-menu"
            popover="auto"
            onClick={(event) => event.currentTarget.hidePopover()}
            className="under-trigger min-w-40 flex-col gap-0.5 rounded-2xl border border-mist/15 bg-dusk p-2 text-mist open:flex"
          >
            <Link to="/faq" className={menuLink}>
              FAQ
            </Link>
            <a href={discord} className={menuLink}>
              Discord
            </a>
            <a href={repository} className={menuLink}>
              GitHub
            </a>
          </div>
        </div>
      </nav>
    </header>
  )
}

export function PageHero({ title, children }: { title: string; children?: ReactNode }) {
  return (
    <section className="relative overflow-clip bg-linear-to-b from-midnight to-dusk">
      <div aria-hidden="true" className="starfield absolute inset-bs-0 inset-e-0 h-full w-3/5" />
      <div className="relative mx-auto box-content flex max-w-page flex-col gap-3 px-gutter pbs-12">
        <h1 className="storybook text-title font-normal">{title}</h1>
        {children}
      </div>
      <Treeline className="relative mbs-10 block h-[clamp(5rem,3rem+7vw,9rem)] w-full" />
    </section>
  )
}

export function SiteFooter({ words }: { words: Copy['footer'] }) {
  return (
    <footer className="border-t border-mist/10 bg-silhouette px-gutter pbs-12 pbe-12">
      <div className="mx-auto flex max-w-2xl flex-col items-center gap-5 text-center">
        <p className="storybook text-heading text-balance">
          {words.heart.before}{' '}
          <svg role="img" aria-label={words.heart.label} viewBox="0 0 24 22" className="inline h-[1.05cap] w-auto fill-moon">
            <path d="M12 21.5S1.5 15.2 1.5 7.6C1.5 4 4.2 1.5 7.3 1.5c2 0 3.6 1 4.7 2.7 1.1-1.7 2.7-2.7 4.7-2.7 3.1 0 5.8 2.5 5.8 6.1 0 7.6-10.5 13.9-10.5 13.9Z" />
          </svg>{' '}
          {words.heart.after}
        </p>
        <p className="text-balance text-mist">
          {words.thanks.before}{' '}
          <a
            href={shipOfHarkinian}
            className="whitespace-nowrap text-starlight underline decoration-moon/50 underline-offset-4 hover:decoration-moon"
          >
            {words.thanks.name}
          </a>
          {words.thanks.after}
        </p>
        <p className="text-small text-mist">{words.smallPrint}</p>
        <p className="text-small text-mist">{words.disclosure}</p>
        <nav aria-label="Footer" className="flex flex-wrap justify-center gap-2 text-small font-bold text-mist">
          <Link to="/download" className="quiet-link">
            Downloads and changes
          </Link>
          <Link to="/randomized" className="quiet-link">
            What is randomized
          </Link>
          <Link to="/faq" className="quiet-link">
            FAQ
          </Link>
          <a href={repository} className="quiet-link">
            Source code
          </a>
        </nav>
      </div>
    </footer>
  )
}

export function DownloadCall({ words, className }: { words: Copy['download']; className: string }) {
  return (
    <div className={`flex flex-wrap items-center gap-x-6 gap-y-4 ${className}`}>
      {systems.map((system) => (
        <a key={system} href={homeOffers[system]} data-offer={system} className="moon-button px-7 py-3">
          {words.forSystem[system]}
        </a>
      ))}
      <Link to="/download" data-offer="any" className="moon-button px-7 py-3">
        {words.anySystem}
      </Link>
      <Link to="/download" data-offer="all" className="underline decoration-mist/40 underline-offset-4 hover:decoration-starlight">
        {words.allLink}
      </Link>
    </div>
  )
}

function GitHubMark({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 24 24" aria-hidden="true" className={className}>
      <path
        fill="currentColor"
        d="M10.226 17.284c-2.965-.36-5.054-2.493-5.054-5.256 0-1.123.404-2.336 1.078-3.144-.292-.741-.247-2.314.09-2.965.898-.112 2.111.36 2.83 1.01.853-.269 1.752-.404 2.853-.404 1.1 0 1.999.135 2.807.382.696-.629 1.932-1.1 2.83-.988.315.606.36 2.179.067 2.942.72.854 1.101 2 1.101 3.167 0 2.763-2.089 4.852-5.098 5.234.763.494 1.28 1.572 1.28 2.807v2.336c0 .674.561 1.056 1.235.786 4.066-1.55 7.255-5.615 7.255-10.646C23.5 6.188 18.334 1 11.978 1 5.62 1 .5 6.188.5 12.545c0 4.986 3.167 9.12 7.435 10.669.606.225 1.19-.18 1.19-.786V20.63a2.9 2.9 0 0 1-1.078.224c-1.483 0-2.359-.808-2.987-2.313-.247-.607-.517-.966-1.034-1.033-.27-.023-.359-.135-.359-.27 0-.27.45-.471.898-.471.652 0 1.213.404 1.797 1.235.45.651.921.943 1.483.943.561 0 .92-.202 1.437-.719.382-.381.674-.718.944-.943"
      />
    </svg>
  )
}
