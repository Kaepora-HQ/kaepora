#!/usr/bin/env bash
# Makes the Windows installer from a package folder that tools/package-windows.sh laid out, with dist/kaepora.iss and
# the Inno Setup compiler that tools/install-inno-setup.ps1 installs.
#
# Usage: tools/package-windows-installer.sh <package folder> <installer>
# Runs in Git Bash.
set -euo pipefail

installer=$(realpath -m "$2")
iscc "--define=Package=$(cygpath -aw "$1")" "--output-dir=$(cygpath -w "$(dirname "$installer")")" \
    "--output-filename=$(basename "$installer" .exe)" "$(dirname "${BASH_SOURCE[0]}")/../dist/kaepora.iss"
ls -la "$installer"
