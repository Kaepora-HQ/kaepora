import { mainDownloads, type System } from '@/release'

// Safari on an iPad in desktop mode sends a Mac user agent; only its touch points tell it
// apart. Android in desktop mode sends a desktop Linux user agent but keeps the platform
// "Linux armv81", and ChromeOS states the platform "Linux x86_64" with its own agent.
function systemOf(agent: string, platform: string, touchPoints: number): System | undefined {
  if (/Windows NT 10\.0/.test(agent) && /Win64|WOW64/.test(agent)) return 'windows'
  if (/Macintosh/.test(agent) && touchPoints < 2) return 'macos'
  if (/Linux x86_64/.test(agent) && platform === 'Linux x86_64') return 'linux'
  return undefined
}

export const markSystem = `{
  const system = (${systemOf})(navigator.userAgent, navigator.platform, navigator.maxTouchPoints)
  if (system) document.documentElement.setAttribute('data-system', system)
}`

export const homeOffers: Record<System, string> = {
  windows: mainDownloads.windows,
  macos: '/download#macos',
  linux: mainDownloads.linux,
}
