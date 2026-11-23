#!/usr/bin/env bash
# Packages the kaepora window for macOS as the app Kaepora.app in one zip. The install rules lay the app out, Qt's
# macdeployqt adds Qt's frameworks and plugins, and THIRD_PARTY_NOTICES.txt and the licences go in its Resources folder.
# macdeployqt runs twice, once without signing to put Qt in place for the notices to list, and once on the finished app
# with nothing left to deploy, where it signs ad hoc as its last step, since a signature holds only while nothing in the
# app changes. A Mac with Apple silicon runs no code without a signature.
#
# Usage: tools/package-macos.sh <build folder> <zip>
# Needs QT_ROOT_DIR, the Qt the build used, without its OpenSSL plugin. Qt prefers that plugin, which would use whatever
# OpenSSL the player's Mac has. Without it, the window's HTTPS request uses macOS's own TLS through Secure Transport.
set -euo pipefail

build=$1
zip=$2
qt=${QT_ROOT_DIR:?the Qt the build used}
if [ -e "$qt/plugins/tls/libqopensslbackend.dylib" ]; then
    echo "$qt holds Qt's OpenSSL plugin, which macdeployqt would put in the app" >&2
    exit 1
fi
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
app=$work/Kaepora.app

cmake --install "$build" --prefix "$work"
"$qt/bin/macdeployqt" "$app" -no-codesign
python3 -I "$(dirname "${BASH_SOURCE[0]}")/third_party_notices.py" --macos "$app" "$qt"
# A second plugin deployment would add plugins for the frameworks the first one brought in, which the notices miss.
deployed=$(cd "$app/Contents" && find Frameworks PlugIns -type f ! -path '*/_CodeSignature/*' | sort)
"$qt/bin/macdeployqt" "$app" -no-plugins
test "$(cd "$app/Contents" && find Frameworks PlugIns -type f ! -path '*/_CodeSignature/*' | sort)" = "$deployed"
codesign --verify --deep --strict "$app"

(cd "$app" && find . -type f | sort)
# ditto keeps the symbolic links inside Qt's frameworks.
ditto -c -k --keepParent "$app" "$zip"
ls -la "$zip"
