export const repository = 'https://github.com/Kaepora-HQ/kaepora'
export const releases = `${repository}/releases`

const latest = `${releases}/latest/download`

export const downloads = {
  linuxAppImage: `${latest}/kaepora-linux-x86_64.AppImage`,
  linuxTarball: `${latest}/kaepora-linux-x86_64.tar.gz`,
  windowsInstaller: `${latest}/kaepora-windows-x86_64-setup.exe`,
  windowsZip: `${latest}/kaepora-windows-x86_64.zip`,
  macosZip: `${latest}/kaepora-macos-arm64.zip`,
  checksums: `${latest}/SHA256SUMS`,
}

export const systems = ['windows', 'macos', 'linux'] as const
export type System = (typeof systems)[number]

export const mainDownloads: Record<System, string> = {
  windows: downloads.windowsInstaller,
  macos: downloads.macosZip,
  linux: downloads.linuxAppImage,
}

const lichiiLatest = 'https://github.com/Kaepora-HQ/Lichii/releases/latest/download'

export const lichiiDownloads = {
  linuxAppImage: `${lichiiLatest}/lichii-linux-x86_64-pgo.AppImage`,
  linuxTarball: `${lichiiLatest}/lichii-linux-x86_64-pgo.tar.gz`,
  linuxZen4: `${lichiiLatest}/lichii-linux-x86_64-zen4-pgo.AppImage`,
  linuxZen5: `${lichiiLatest}/lichii-linux-x86_64-zen5-pgo.AppImage`,
  windowsZip: `${lichiiLatest}/lichii-windows-x86_64-pgo.zip`,
  macosZip: `${lichiiLatest}/lichii-macos-arm64.zip`,
}

export const lichiiUpstreamReleases = 'https://github.com/zortos293/Lichii/releases'
