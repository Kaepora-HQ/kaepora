#!/usr/bin/env bash
# Packages the kaepora window for Linux as one self-contained folder, shipped twice: as an AppImage, and as a tar.gz of
# the same files with a kaepora link at the top. linuxdeploy bundles every library the window needs except the base
# system ones (glibc, the C++ runtime, X11 and Wayland client libraries, the graphics driver), and its Qt plugin adds Qt
# and its plugins. Both run on systems with glibc 2.39 or later.
#
# Usage: tools/package-linux.sh <build folder> <name> <version>
# Runs on Ubuntu, whose packages supply the bundled libraries and their licences. Needs QT_ROOT_DIR, the Qt the build
# used. Writes <name>.AppImage and <name>.tar.gz to the current folder.
set -euo pipefail

build=$1
name=$2
version=$3
qt=${QT_ROOT_DIR:?the Qt the build used}
work=$(realpath "$(mktemp -d)")
trap 'rm -rf "$work"' EXIT
appdir=$work/AppDir

# Tagged releases, each checked by its sum: linuxdeploy, its Qt plugin, and the runtime the AppImage starts with.
fetch() {
    curl -fsSL -o "$work/$1" "$2"
    echo "$3  $work/$1" | sha256sum -c --quiet
    chmod +x "$work/$1"
}
fetch linuxdeploy-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage \
    c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d
fetch linuxdeploy-plugin-qt-x86_64.AppImage \
    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage \
    15106be885c1c48a021198e7e1e9a48ce9d02a86dd0a1848f00bdbf3c1c92724
fetch runtime-x86_64 https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64 \
    2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
linuxdeploy=$work/linuxdeploy-x86_64.AppImage

cmake --install "$build" --prefix "$appdir/usr" --strip

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE=$qt/bin/qmake
# Wayland's platform plugins with their shell and graphics integrations; the xcb plugin for X11 is always deployed. The
# offscreen plugin lets the check below start the window without a display.
export EXTRA_QT_MODULES=waylandcompositor
export EXTRA_PLATFORM_PLUGINS='libqwayland-egl.so;libqwayland-generic.so;libqoffscreen.so'
# Qt's TLS plugin looks for OpenSSL by name at the first HTTPS request, so no library lists it as a dependency. The
# package carries Ubuntu's, so every player runs the OpenSSL the check below proves. Without it, each system decides
# which OpenSSL major runs inside the window.
"$linuxdeploy" --appdir "$appdir" --plugin qt --library /usr/lib/x86_64-linux-gnu/libssl.so.3

# The Qt plugin's release wraps AppRun in a script with a hook that sets QT_QPA_PLATFORMTHEME=gtk2 on GNOME and XFCE.
# Qt 6 has no gtk2 theme, and the setting would replace a theme the player chose, for the window and for the emulator it
# starts. AppRun goes back to the plain link to the window.
rm -r "$appdir/apprun-hooks" "$appdir/AppRun"
mv "$appdir/AppRun.wrapped" "$appdir/AppRun"

# The AppImage specification asks for a PNG icon at .DirIcon; linuxdeploy puts the SVG at the top of the folder.
ln -s usr/share/icons/hicolor/256x256/apps/io.github.pixelhabits.kaepora.png "$appdir/.DirIcon"

python3 -I "$(dirname "${BASH_SOURCE[0]}")/third_party_notices.py" "$appdir" "$qt" "$work/runtime-x86_64"

# The notices cover exactly the libraries packed below.
libraries=$(ls "$appdir/usr/lib")
LDAI_OUTPUT=$work/$name.AppImage LINUXDEPLOY_OUTPUT_VERSION=$version LDAI_RUNTIME_FILE=$work/runtime-x86_64 \
    "$linuxdeploy" --appdir "$appdir" --output appimage
test "$(ls "$appdir/usr/lib")" = "$libraries"

# The AppImage holds the same files as the folder.
(cd "$work" && "$work/$name.AppImage" --appimage-extract > /dev/null)
root=$work/squashfs-root
diff -r --no-dereference "$root" "$appdir"

# Starts the AppImage's window on Qt's offscreen platform with nothing in the environment that points at a library or a
# plugin, so it finds only what the package holds and the base system, then checks where each library it loaded came
# from. Qt's HTTP/2 log names each response that arrives whole, whatever its status, so the line below shows the
# window's request to GitHub went through.
mkdir "$work/home"
finished='qt.network.http2: stream 1 closed'
env -i HOME="$work/home" QT_QPA_PLATFORM=offscreen QT_FORCE_STDERR_LOGGING=1 \
    QT_LOGGING_RULES=qt.network.http2.debug=true "$root/AppRun" 2> "$work/window.log" &
pid=$!
for _ in $(seq 150); do
    if grep -qxF "$finished" "$work/window.log"; then
        break
    fi
    sleep 0.2
done
if ! kill -0 "$pid" 2> /dev/null; then
    echo "The packaged window stopped" >&2
    wait "$pid"
    exit 1
fi
if ! grep -qxF "$finished" "$work/window.log"; then
    echo "The packaged window's request to GitHub did not finish:" >&2
    cat "$work/window.log" >&2
    exit 1
fi
loaded=$(awk '$6 ~ /\.so(\.|$)/ { print $6 }' "/proc/$pid/maps" | sort -u)
kill "$pid"
wait "$pid" || true
inside=$(grep "^$root/" <<< "$loaded")
outside=$(grep -v "^$root/" <<< "$loaded")
echo "The window loaded these libraries from the package:"
echo "$inside"
echo "and these from the system:"
echo "$outside"
grep -qxF "$root/usr/lib/libQt6Widgets.so.6" <<< "$inside"
grep -qxF "$root/usr/plugins/platforms/libqoffscreen.so" <<< "$inside"
grep -qxF "$root/usr/plugins/tls/libqopensslbackend.so" <<< "$inside"
grep -qxF "$root/usr/lib/libssl.so.3" <<< "$inside"
# Each library that the window or a library it loaded from the package asks for, and that the package holds, came from
# the package. The package also holds libraries that only the system's own libraries ask for, because linuxdeploy copies
# everything ldd lists; the system's copies serve those.
for soname in $(readelf -d "$root/usr/bin/kaepora" $inside | sed -n 's/.*(NEEDED).*\[\(.*\)\]$/\1/p' | sort -u); do
    if [ -f "$root/usr/lib/$soname" ] && ! grep -qxF "$root/usr/lib/$soname" <<< "$inside"; then
        echo "$soname came from outside the package, which holds its own copy" >&2
        exit 1
    fi
done

mv "$appdir" "$work/kaepora"
ln -s usr/bin/kaepora "$work/kaepora/kaepora"
tar -czf "$name.tar.gz" -C "$work" kaepora
mv "$work/$name.AppImage" .
ls -la "$name.AppImage" "$name.tar.gz"
