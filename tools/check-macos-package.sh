#!/usr/bin/env bash
# Unzips the macOS package into a new folder and starts its window there, with nothing in the environment but a new home.
# Checks that the app's signature holds, that its request to GitHub completes, that it loaded Qt, its platform plugin and
# the Secure Transport plugin from the package, and that every other library it loaded is macOS's own. Qt's HTTP/2 log
# names each response that arrives whole, whatever its status.
#
# Usage: tools/check-macos-package.sh <zip>
set -euo pipefail

zip=$1
# The window keeps its settings in the preferences of the user who runs it, whatever its home is, under the name Qt
# makes from its organisation and application names. The check runs only where they do not exist, and removes them
# after. defaults delete empties them and leaves their file, so the file goes too.
preferences=com.kaepora.kaepora
if defaults read "$preferences" > /dev/null 2>&1; then
    echo "This Mac holds Kaepora's settings, which the check would change" >&2
    exit 1
fi
folder=$(mktemp -d)
cleanup() {
    if [ -n "${pid:-}" ]; then
        kill "$pid" 2> /dev/null || true
        wait "$pid" 2> /dev/null || true
    fi
    defaults delete "$preferences" 2> /dev/null || true
    rm -f "$HOME/Library/Preferences/$preferences.plist"
    rm -rf "$folder"
}
trap cleanup EXIT

ditto -x -k "$zip" "$folder"
app=$folder/Kaepora.app
codesign --verify --deep --strict "$app"
if [ -e "$app/Contents/PlugIns/tls/libqopensslbackend.dylib" ]; then
    echo "The package holds the OpenSSL plugin, which Qt would prefer to Secure Transport" >&2
    exit 1
fi

# macOS finds the user's folders from CFFIXED_USER_HOME, not HOME.
home=$folder/home
mkdir "$home"
log=$folder/window.log
finished='qt.network.http2: stream 1 closed'
env -i HOME="$home" CFFIXED_USER_HOME="$home" QT_FORCE_STDERR_LOGGING=1 QT_LOGGING_RULES=qt.network.http2.debug=true \
    "$app/Contents/MacOS/Kaepora" 2> "$log" &
pid=$!
for _ in $(seq 150); do
    if grep -qxF "$finished" "$log"; then
        break
    fi
    sleep 0.2
done
if ! kill -0 "$pid" 2> /dev/null; then
    echo "The packaged window stopped:" >&2
    cat "$log" >&2
    exit 1
fi
if ! grep -qxF "$finished" "$log"; then
    echo "The packaged window's request to GitHub did not finish:" >&2
    cat "$log" >&2
    exit 1
fi
loaded=$(vmmap -w "$pid" | sed -n 's|^__TEXT .* SM=[A-Z]* *||p' | sort -u)

inside=$(grep "^$app/" <<< "$loaded" || true)
outside=$(grep -v "^$app/" <<< "$loaded" || true)
echo "The window loaded these libraries from the package:"
echo "$inside"
echo "and these from the system:"
echo "$outside"
for file in MacOS/Kaepora Frameworks/QtCore.framework/Versions/A/QtCore Frameworks/QtWidgets.framework/Versions/A/QtWidgets \
    Frameworks/QtNetwork.framework/Versions/A/QtNetwork PlugIns/platforms/libqcocoa.dylib \
    PlugIns/tls/libqsecuretransportbackend.dylib; do
    if ! grep -qxF "$app/Contents/$file" <<< "$inside"; then
        echo "The window did not load $file from the package" >&2
        exit 1
    fi
done
if grep -vE '^/(System|usr/lib)/' <<< "$outside"; then
    echo "These came from outside the package and macOS" >&2
    exit 1
fi
