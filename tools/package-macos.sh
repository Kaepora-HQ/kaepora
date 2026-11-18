#!/usr/bin/env bash
# Packages the kaepora window for macOS as the app Kaepora.app in one zip. The install rules lay the app out, Qt's
# macdeployqt adds Qt's frameworks and plugins, and THIRD_PARTY_NOTICES.txt and the licences go in its Resources folder.
# The app is signed ad hoc, as a Mac with Apple silicon runs no code without a signature, and macdeployqt's changes
# break the signature the linker gave it.
#
# Usage: tools/package-macos.sh <build folder> <zip>
# Needs QT_ROOT_DIR, the Qt the build used.
set -euo pipefail

build=$1
zip=$2
qt=${QT_ROOT_DIR:?the Qt the build used}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app=$work/Kaepora.app

cmake --install "$build" --prefix "$work"
"$qt/bin/macdeployqt" "$app"
# Qt prefers its OpenSSL plugin, which would use whatever OpenSSL the player's Mac has. Without it, the window's HTTPS
# request uses macOS's own TLS through Secure Transport.
rm -f "$app/Contents/PlugIns/tls/libqopensslbackend.dylib"
python3 -I "$(dirname "${BASH_SOURCE[0]}")/third_party_notices.py" --macos "$app" "$qt"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"

(cd "$app" && find . -type f | sort)
# ditto keeps the symbolic links inside Qt's frameworks.
ditto -c -k --keepParent "$app" "$zip"
ls -la "$zip"
