#!/usr/bin/env bash
# Puts a release's packages into one folder under the names the website's download buttons ask for, with SHA256SUMS and
# the latest.json that the window's update notice reads.
#
# Usage: tools/release-files.sh <folder> <AppImage> <tar.gz> <zip>
# Runs at the top of the repository.
set -euo pipefail

folder=$1
releases=https://github.com/PixelHabits/kaepora/releases

version=$(jq -r '."."' .release-please-manifest.json)
# The window reads 0.2.0-rc.1 as 0.2.0, and ignores a version that starts with v.
if ! [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Version $version is not three numbers, so it cannot be the latest release" >&2
    exit 1
fi
# The programs read their version from the project() line. One that lags would offer players the release they run.
if ! grep -qF "project(kaepora VERSION $version " CMakeLists.txt; then
    echo "The project() line in CMakeLists.txt does not say $version" >&2
    exit 1
fi

mkdir "$folder"
cp "$2" "$folder/kaepora-linux-x86_64.AppImage"
cp "$3" "$folder/kaepora-linux-x86_64.tar.gz"
cp "$4" "$folder/kaepora-windows-x86_64.zip"
(cd "$folder" && sha256sum kaepora-* > SHA256SUMS)
jq -n --arg version "$version" --arg url "$releases/tag/v$version" '{ version: $version, url: $url }' \
    > "$folder/latest.json"

ls -l "$folder"
cat "$folder/SHA256SUMS" "$folder/latest.json"
