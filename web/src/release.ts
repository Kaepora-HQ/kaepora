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
