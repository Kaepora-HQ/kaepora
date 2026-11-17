"""Write THIRD_PARTY_NOTICES at the top of a Linux package folder, with the licence texts it names, from what the
folder holds.

Usage: python3 -I tools/third_party_notices.py <package folder> <Qt> <AppImage runtime>

Runs on Ubuntu, after linuxdeploy has filled the folder. Each Qt file in the folder is looked up in Qt's SBOMs, which
also name the code from other projects built into it; each other library is looked up with dpkg; the runtime's parts
come from the notice the runtime carries. A library or a runtime part that none of these accounts for stops the script.
"""

import hashlib
import json
import pathlib
import platform
import re
import shutil
import subprocess
import sys
import urllib.request

# The SPDX License List 3.29.0, for the texts of the licences that Qt's SBOMs and the runtime's notice name.
SPDX_TEXT = "https://raw.githubusercontent.com/spdx/license-list-data/31ba1a50e5397e00a304dbadc76531740e89ee48/text/{}.txt"
ICU_LICENSE = "https://raw.githubusercontent.com/unicode-org/icu/release-{}/icu4c/LICENSE"
# The sum of ICU's licence for each ICU version that Qt's binaries carry.
ICU_LICENSE_SHA256 = {"73.2": "f3005e195ff74d8812cc1f182a1c446fab678d70a10e3dada497585befee5416"}
# Built into the window from its headers, so no library in the folder names it.
HEADER_PACKAGES = ["nlohmann-json3-dev"]
# The licence of each part that the runtime's notice names, by the licence link the notice gives for it.
RUNTIME_LICENSES = {
    "https://github.com/AppImage/type2-runtime/blob/main/LICENSE": "MIT",
    "https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT": "MIT",
    "https://github.com/libfuse/libfuse/blob/master/LGPL2.txt": "LGPL-2.1-only",
    "https://github.com/vasi/squashfuse/blob/master/LICENSE": "BSD-2-Clause",
    "https://github.com/facebook/zstd/blob/dev/LICENSE": "BSD-3-Clause",
    "https://zlib.net/zlib_license.html": "Zlib",
}
QT_LICENSE = "LGPL-3.0-only"
QT_COMMERCIAL = "LicenseRef-Qt-Commercial"


def stem(path):
    """A library or plugin path without its .so suffix and version, as both the folder and Qt's SBOMs name it."""
    return re.sub(r"\.so(\.[\d.]+)?$", "", path.removeprefix("./"))


def license_ids(expression):
    return set(re.findall(r"[\w.+-]+", expression)) - {"AND", "OR", "WITH"}


def run(*command):
    return subprocess.run(command, check=True, stdout=subprocess.PIPE, text=True).stdout


def fetch(url):
    with urllib.request.urlopen(url) as response:
        return response.read()


def qt(folder, qt_root, texts):
    usr = folder / "usr"
    shipped = {stem(str(path.relative_to(usr))) for path in [*usr.glob("lib/libQt6*"), *usr.glob("plugins/**/*.so")]}
    sboms = folder / "usr/share/licenses/qt"
    sboms.mkdir(parents=True)
    found, modules, parts, not_lgpl, extracted = set(), [], {}, [], {}
    for path in sorted((qt_root / "sbom").glob("*.spdx.json")):
        sbom = json.loads(path.read_text())
        files = {f["SPDXID"]: f for f in sbom["files"] if stem(f["fileName"]) in shipped}
        if not files:
            continue
        packages = {p["SPDXID"]: p for p in sbom["packages"]}
        relationships = sbom["relationships"]
        described = next(r["relatedSpdxElement"] for r in relationships if r["relationshipType"] == "DESCRIBES")
        modules.append(packages[described])
        shutil.copy(path.with_suffix(""), sboms)
        for file in files.values():
            found.add(stem(file["fileName"]))
            if QT_LICENSE not in license_ids(file["licenseConcluded"]):
                not_lgpl.append((stem(file["fileName"]), file["licenseConcluded"]))
        # The packages that hold the files, and every package they depend on.
        todo = [r["spdxElementId"] for r in relationships
                if r["relationshipType"] == "CONTAINS" and r["relatedSpdxElement"] in files]
        seen = set(todo)
        while todo:
            current = todo.pop()
            for r in relationships:
                if (r["relationshipType"] == "DEPENDS_ON" and r["spdxElementId"] == current
                        and r["relatedSpdxElement"] in packages and r["relatedSpdxElement"] not in seen):
                    seen.add(r["relatedSpdxElement"])
                    todo.append(r["relatedSpdxElement"])
        for spdxid in seen:
            package = packages[spdxid]
            if "_Attribution_" in package["name"] or package["name"].startswith("Bundled"):
                title = re.sub(r"^.*_Attribution_|^Bundled", "", package["name"])
                parts[(title.lower(), package.get("versionInfo"), package["licenseConcluded"])] = title, package
        extracted |= {e["licenseId"]: e["extractedText"] for e in sbom["hasExtractedLicensingInfos"]}
    if shipped - found:
        sys.exit(f"Qt's SBOMs do not list {sorted(shipped - found)}")

    version = next(ref["referenceLocator"].split(":")[5] for ref in modules[0]["externalRefs"]
                   if ref["referenceLocator"].startswith("cpe:2.3:a:qt:qt:"))
    lines = [
        f"Qt {version}",
        "",
        f"Kaepora uses Qt under the GNU Lesser General Public License version 3 ({QT_LICENSE}), which adds to",
        "the GNU General Public License version 3 (GPL-3.0-only). Qt is linked as shared libraries: its libraries are",
        f"in usr/lib and its plugins in usr/plugins, and you can replace them with your own build of Qt {version}.",
        "Qt's SBOMs, in usr/share/licenses/qt, list each Qt file here with its licence.",
        "",
        "Source:",
    ]
    texts.update({QT_LICENSE, "GPL-3.0-only"})
    for module in modules:
        archive = f"https://download.qt.io/archive/qt/{version.rsplit('.', 1)[0]}/{version}/submodules/" \
                  f"{module['name']}-everywhere-src-{version}.tar.xz"
        lines += [f"  {module['name']}: {archive}", f"    {module['downloadLocation']}"]
    for path, expression in sorted(not_lgpl):
        chosen = sorted(license_ids(expression) - {QT_COMMERCIAL})
        texts.update(chosen)
        lines += ["", f"Qt's SBOM does not offer this file under the LGPL, so it is used under {' or '.join(chosen)}:",
                  f"  usr/{path}.so: {expression}"]
    lines += ["", "Code from other projects built into these Qt files, as Qt's SBOMs list it:"]
    for _, (title, package) in sorted(parts.items()):
        version_info = package.get("versionInfo", "unknown")
        named = title if version_info == "unknown" else f"{title} {version_info}"
        lines.append(f"  {named}: {package['licenseConcluded']}")
        if package["copyrightText"] != "NOASSERTION":
            lines += [f"    {line.strip()}" for line in package["copyrightText"].splitlines() if line.strip()]
        if package["downloadLocation"] != "NOASSERTION":
            lines.append(f"    Source: {package['downloadLocation']}")
        texts.update(license_ids(package["licenseConcluded"]))
    return lines, {ref: extracted[ref] for ref in texts if ref.startswith("LicenseRef-")}


def icu(folder, qt_root):
    libraries = sorted(f"usr/lib/{path.name}" for path in (folder / "usr/lib").glob("libicu*")
                       if (qt_root / "lib" / path.name).exists())
    sbom = json.loads(next((qt_root / "sbom").glob("qtbase-*.spdx.json")).read_text())
    version = next(p["versionInfo"] for p in sbom["packages"] if p["name"] == "ICU")
    license = folder / "usr/share/licenses/icu/LICENSE"
    license.parent.mkdir(parents=True)
    text = fetch(ICU_LICENSE.format(version.replace(".", "-")))
    if hashlib.sha256(text).hexdigest() != ICU_LICENSE_SHA256[version]:
        sys.exit(f"ICU {version}'s licence does not match its sum")
    license.write_bytes(text)
    return [
        f"ICU {version}",
        "",
        f"Qt's libraries use ICU: {', '.join(libraries)}.",
        "Its licence is in usr/share/licenses/icu/LICENSE.",
        f"Source: https://github.com/unicode-org/icu/releases/tag/release-{version.replace('.', '-')}",
    ]


def ubuntu(folder, qt_root):
    usr = folder / "usr"
    contents = {package: ["built into usr/bin/kaepora"] for package in HEADER_PACKAGES}
    for library in sorted((usr / "lib").glob("*.so*")):
        if (qt_root / "lib" / library.name).exists():
            if not library.name.startswith(("libQt6", "libicu")):
                sys.exit(f"{library.name} comes from Qt's binaries and is neither Qt nor ICU")
            continue
        owners = {line.split(":")[0] for line in run("dpkg", "-S", f"*/{library.name}").splitlines()
                  if not line.startswith("diversion")}
        if len(owners) != 1:
            sys.exit(f"{library.name} belongs to {sorted(owners)}, expected one package")
        contents.setdefault(owners.pop(), []).append(f"usr/lib/{library.name}")

    release = platform.freedesktop_os_release()
    lines = [
        f"{release['NAME']} {release['VERSION_ID']} packages",
        "",
        "These come from Ubuntu's packages. Each package's copyright file states its copyright and licences. The texts",
        "those files refer to in /usr/share/common-licenses are in usr/share/common-licenses.",
    ]
    common = set()
    for package, files in sorted(contents.items()):
        version, source, source_version = run(
            "dpkg-query", "-W", "-f=${Version}\t${source:Package}\t${source:Version}", package).split("\t")
        # linuxdeploy copies the copyright file of each library's package; a package only built in is copied here.
        copyright = usr / "share/doc" / package / "copyright"
        if not copyright.exists():
            copyright.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(f"/usr/share/doc/{package}/copyright", copyright)
        text = copyright.read_text(errors="replace")
        names = sorted(set(re.findall(r"^License: *(\S.*)$", text, re.MULTILINE)))
        common |= {name.rstrip(".") for name in re.findall(r"/usr/share/common-licenses/([\w.+-]+)", text)}
        lines += [
            "",
            f"{package} {version}: {', '.join(files)}",
            f"  Licences: {'; '.join(names) or 'as the copyright file states'} (usr/share/doc/{package}/copyright)",
            f"  Source: https://launchpad.net/ubuntu/+source/{source}/{source_version}",
        ]
    (usr / "share/common-licenses").mkdir()
    for name in sorted(common):
        shutil.copy(f"/usr/share/common-licenses/{name}", usr / "share/common-licenses" / name)
    return lines


def runtime(path, texts):
    data = path.read_bytes()
    start = data.index(b"This executable contains code from")
    notice = data[start:data.index(b"Please see", start)].decode()
    parts = re.findall(r"\* (.+), licensed under the terms of\s+(\S+)", notice)
    unknown = [url for _, url in parts if url not in RUNTIME_LICENSES]
    if not parts or unknown:
        sys.exit(f"The runtime's notice names licences without an SPDX identifier here: {unknown}")
    commit = re.search(rb"https://github\.com/AppImage/type2-runtime/commit/\w+", data).group().decode()
    texts.update(RUNTIME_LICENSES[url] for _, url in parts)
    return [
        "AppImage runtime",
        "",
        "The AppImage file starts with the AppImage runtime, which holds code from:",
        *(f"  {name}: {RUNTIME_LICENSES[url]}" for name, url in parts),
        f"Source: {commit}",
    ]


def main():
    folder, qt_root, runtime_path = map(pathlib.Path, sys.argv[1:4])
    texts = set()
    qt_lines, extracted = qt(folder, qt_root, texts)
    sections = [qt_lines, icu(folder, qt_root), ubuntu(folder, qt_root), runtime(runtime_path, texts)]

    spdx = folder / "usr/share/licenses/spdx"
    spdx.mkdir()
    for license in sorted(texts):
        text = extracted[license].encode() if license in extracted else fetch(SPDX_TEXT.format(license))
        (spdx / f"{license}.txt").write_bytes(text)

    header = [
        "Third-party notices for Kaepora",
        "",
        "Kaepora is under the MIT License, in usr/share/licenses/kaepora/LICENSE. This package also holds the software",
        "below, each under its own licence. Paths are relative to the folder that holds this file. A licence named by",
        "its SPDX identifier, such as MIT, has its text in usr/share/licenses/spdx/<identifier>.txt.",
    ]
    text = "\n\n\n".join("\n".join(section) for section in [header, *sections]) + "\n"
    (folder / "THIRD_PARTY_NOTICES").write_text(text)


if __name__ == "__main__":
    main()
