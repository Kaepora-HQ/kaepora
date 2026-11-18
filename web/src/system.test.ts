import { expect, test } from 'bun:test'
import { copy } from '@/copy'
import { downloads, systems } from '@/release'
import { homeOffers, markSystem } from '@/system'

const chrome = (os: string, tail = 'Safari/537.36') =>
  `Mozilla/5.0 (${os}) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 ${tail}`
const firefox = (os: string) => `Mozilla/5.0 (${os}; rv:145.0) Gecko/20100101 Firefox/145.0`
const safariMac = 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.6 Safari/605.1.15'

const windows = { button: 'Download for Windows', href: downloads.windowsZip, allDownloads: true }
const macos = { button: 'Download for macOS', href: '/download#macos', allDownloads: true }
const linux = { button: 'Download for Linux', href: downloads.linuxAppImage, allDownloads: true }
const plain = { button: 'Download', href: '/download', allDownloads: false }

// Each platform is what that browser's navigator.platform returns: Chromium's
// navigator_base.cc, Gecko's Navigator.cpp, WebKit's NavigatorBase.cpp and, for iOS
// pages in desktop mode, WebKit's WebPageProxyIOS.mm.
const visitors: [string, string, string, number, typeof plain][] = [
  ['Chrome on Windows 10 or 11', chrome('Windows NT 10.0; Win64; x64'), 'Win32', 0, windows],
  ['Edge on Windows', chrome('Windows NT 10.0; Win64; x64', 'Safari/537.36 Edg/142.0.0.0'), 'Win32', 0, windows],
  ['Firefox on Windows, or on Windows 11 on ARM', firefox('Windows NT 10.0; Win64; x64'), 'Win32', 0, windows],
  ['Chrome on a Windows touch laptop', chrome('Windows NT 10.0; Win64; x64'), 'Win32', 10, windows],
  ['Edge on an Xbox', chrome('Windows NT 10.0; Win64; x64; Xbox; Xbox One', 'Safari/537.36 Edg/142.0.0.0'), 'Win32', 0, windows],
  ['Firefox 115 on Windows 7', 'Mozilla/5.0 (Windows NT 6.1; Win64; x64; rv:109.0) Gecko/20100101 Firefox/115.0', 'Win32', 0, plain],
  ['Firefox on 32-bit Windows 10, or on Windows 10 on ARM', 'Mozilla/5.0 (Windows NT 10.0; rv:145.0) Gecko/20100101 Firefox/145.0', 'Win32', 0, plain],
  ['Firefox 139 on 32-bit Windows 10', 'Mozilla/5.0 (Windows NT 10.0; rv:139.0) Gecko/20100101 Firefox/139.0', 'Win32', 0, plain],
  ['Safari on a Mac', safariMac, 'MacIntel', 0, macos],
  ['Safari 16.4 on an Apple silicon Mac with Ventura', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/16.4 Safari/605.1.15', 'MacIntel', 0, macos],
  ['Chrome on a Mac', chrome('Macintosh; Intel Mac OS X 10_15_7'), 'MacIntel', 0, macos],
  ['Firefox on a Mac', firefox('Macintosh; Intel Mac OS X 10.15'), 'MacIntel', 0, macos],
  ['Chrome on Linux', chrome('X11; Linux x86_64'), 'Linux x86_64', 0, linux],
  ['Firefox on Linux', firefox('X11; Linux x86_64'), 'Linux x86_64', 0, linux],
  ["Ubuntu's Firefox", firefox('X11; Ubuntu; Linux x86_64'), 'Linux x86_64', 0, linux],
  ['Chrome on a Linux touch laptop', chrome('X11; Linux x86_64'), 'Linux x86_64', 10, linux],
  ['Firefox 140 on ARM Linux, which states x86-64', 'Mozilla/5.0 (X11; Linux x86_64; rv:140.0) Gecko/20100101 Firefox/140.0', 'Linux x86_64', 0, linux],
  ['An older Firefox on ARM Linux', firefox('X11; Linux aarch64'), 'Linux aarch64', 0, plain],
  ['An older Firefox on 32-bit Linux', firefox('X11; Linux i686'), 'Linux i686', 0, plain],
  ['Chrome on ChromeOS', chrome('X11; CrOS x86_64 14541.0.0'), 'Linux x86_64', 0, plain],
  ['Chrome on a ChromeOS touch screen', chrome('X11; CrOS x86_64 14541.0.0'), 'Linux x86_64', 10, plain],
  ['Chrome on an Android phone', chrome('Linux; Android 10; K', 'Mobile Safari/537.36'), 'Linux armv81', 5, plain],
  ['Chrome on an Android tablet, mobile site', chrome('Linux; Android 10; K'), 'Linux armv81', 5, plain],
  ['Chrome on an Android tablet in desktop mode', chrome('X11; Linux x86_64'), 'Linux armv81', 5, plain],
  ["Chrome 119 on a premium Android tablet, from Chrome's desktop-mode post", 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/119.0.0.0 Safari/537.36', 'Linux armv81', 5, plain],
  ['Firefox on an Android phone', 'Mozilla/5.0 (Android 15; Mobile; rv:145.0) Gecko/145.0 Firefox/145.0', 'Linux armv81', 5, plain],
  ['Firefox on Android with Desktop site on', firefox('X11; Linux x86_64'), 'Linux armv81', 5, plain],
  ['The Meta Quest browser', 'Mozilla/5.0 (X11; Linux x86_64; Quest 3) AppleWebKit/537.36 (KHTML, like Gecko) OculusBrowser/40.0.0.0 Chrome/142.0.0.0 VR Safari/537.36', 'Linux armv81', 5, plain],
  ['Safari on an iPhone', 'Mozilla/5.0 (iPhone; CPU iPhone OS 18_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.6 Mobile/15E148 Safari/604.1', 'iPhone', 5, plain],
  ['Safari on an iPhone with Request Desktop Website', safariMac, 'MacIntel', 5, plain],
  ['Safari on an iPad, mobile site', 'Mozilla/5.0 (iPad; CPU OS 18_6 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.6 Mobile/15E148 Safari/604.1', 'iPad', 5, plain],
  ['Safari on an iPad in its default desktop mode', safariMac, 'MacIntel', 5, plain],
  ['Chrome on an iPad', 'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) CriOS/142.0.7444.77 Version/18.6 Safari/605.1.15', 'MacIntel', 5, plain],
  ['Googlebot, desktop', 'Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; Googlebot/2.1; +http://www.google.com/bot.html) Chrome/142.0.0.0 Safari/537.36', 'Linux x86_64', 0, plain],
  ['Googlebot, smartphone', 'Mozilla/5.0 (Linux; Android 6.0.1; Nexus 5X Build/MMB29P) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/142.0.0.0 Mobile Safari/537.36 (compatible; Googlebot/2.1; +http://www.google.com/bot.html)', 'Linux x86_64', 0, plain],
  ['bingbot', 'Mozilla/5.0 AppleWebKit/537.36 (KHTML, like Gecko; compatible; bingbot/2.0; +http://www.bing.com/bingbot.htm) Chrome/116.0.1938.76 Safari/537.36', 'Win32', 0, plain],
]

function visit(userAgent: string, platform: string, maxTouchPoints: number) {
  const marks = new Map<string, string>()
  const document = { documentElement: { setAttribute: (name: string, value: string) => marks.set(name, value) } }
  new Function('navigator', 'document', markSystem)({ userAgent, platform, maxTouchPoints }, document)
  const system = systems.find((known) => known === marks.get('data-system'))
  return system
    ? { button: copy.download.forSystem[system], href: homeOffers[system], allDownloads: true }
    : { button: copy.download.anySystem, href: '/download', allDownloads: false }
}

test.each(visitors)('%s', (_, userAgent, platform, maxTouchPoints, expected) => {
  expect(visit(userAgent, platform, maxTouchPoints)).toEqual(expected)
})
