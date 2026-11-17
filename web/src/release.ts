export const repository = 'https://github.com/PixelHabits/kaepora'
export const releases = `${repository}/releases`

const latest = `${releases}/latest/download`

export const downloads = {
  linuxAppImage: `${latest}/kaepora-linux-x86_64.AppImage`,
  linuxTarball: `${latest}/kaepora-linux-x86_64.tar.gz`,
  windowsZip: `${latest}/kaepora-windows-x86_64.zip`,
  checksums: `${latest}/SHA256SUMS`,
}
