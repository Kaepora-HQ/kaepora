#!/usr/bin/env bash
# Packages the kaepora window for Windows as one zip and an installer of the same folder: the window at the top, beside
# the Qt and Visual C++ runtime libraries it loads and Qt's plugin folders, with THIRD_PARTY_NOTICES.txt and the licences
# under licenses/. The install rules lay the folder out, and Qt's windeployqt adds Qt.
#
# Usage: tools/package-windows.sh <build folder> <zip> <installer>
# Runs in Git Bash, in the Visual Studio environment the build used. Needs QT_ROOT_DIR, the Qt the build used.
set -euo pipefail

build=$1
zip=$(realpath -m "$2")
qt=${QT_ROOT_DIR:?the Qt the build used}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
folder=$work/kaepora

cmake --install "$build" --prefix "$folder"
# The window needs the Windows platform plugin, a style, and the TLS plugins for its HTTPS request. windeployqt leaves
# out the OpenSSL one, so the request uses Windows' own TLS through Schannel. The plugin types skipped here would bring
# Qt Svg, which the window does not use.
"$qt/bin/windeployqt.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler \
    --no-system-dxc-compiler --skip-plugin-types generic,iconengines,imageformats,networkinformation \
    "$folder/kaepora.exe"
python -I "$(dirname "${BASH_SOURCE[0]}")/third_party_notices.py" --windows "$folder" "$qt"

(cd "$folder" && find . -type f | sort && cmake -E tar cf "$zip" --format=zip -- *)
ls -la "$zip"
"$(dirname "${BASH_SOURCE[0]}")/package-windows-installer.sh" "$folder" "$3"
