"""Follow the upstream repos that shipwright/ was imported from.

Usage: python3 -I tools/upstream.py [--clone PATH] <command>
  status           fetch each upstream, list its commits after `reviewed` that touch files we carry,
                   and list the files it added in directories we carry files from
  diff [COMMIT]    show the upstream changes after `reviewed`, or one commit, limited to files we carry
  apply COMMIT     merge one upstream commit into the files we carry; conflicts stay in the working tree
  reviewed COMMIT  record COMMIT as the newest upstream commit whose changes have been looked at

shipwright/UPSTREAM.json has one entry per upstream repo. Our files under an entry's path are that repo's files
at the same path below it. --clone is a checkout of the upstream that shipwright/ mirrors, with its submodules,
so each repo's clone sits at the same path below it as the repo's files do below shipwright/.
"""

import argparse
import json
import pathlib
import posixpath
import subprocess
import sys

MIRROR = "shipwright"
UPSTREAM = f"{MIRROR}/UPSTREAM.json"


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], check=True, stdout=subprocess.PIPE, text=True).stdout


class Upstream:
    """One upstream repo: its UPSTREAM.json entry, its clone, and the upstream paths of the files we carry."""

    def __init__(self, root, clone, name, entry, entries):
        self.name = name
        self.entry = entry
        self.repo = clone / pathlib.Path(entry["path"]).relative_to(MIRROR)
        self.tip = f"origin/{entry['branch']}"
        prefix = entry["path"] + "/"
        nested = tuple(e["path"] + "/" for e in entries.values() if e["path"].startswith(prefix))
        self.files = [
            path.removeprefix(prefix)
            for path in git(root, "ls-files", "--", prefix).splitlines()
            if path != UPSTREAM and not path.startswith(nested)
        ]

    def resolve(self, commit):
        result = subprocess.run(["git", "-C", self.repo, "rev-parse", "--verify", "--quiet", f"{commit}^{{commit}}"],
                                stdout=subprocess.PIPE, text=True)
        return result.stdout.strip() if result.returncode == 0 else None


def owner(upstreams, commit):
    found = [(up, sha) for up in upstreams if (sha := up.resolve(commit))]
    if len(found) != 1:
        sys.exit(f"{commit} is in {len(found)} upstream clones, expected exactly one")
    return found[0]


def status(upstreams):
    for up in upstreams:
        git(up.repo, "fetch", "--quiet", "origin")
        reviewed = up.entry["reviewed"]
        print(f"{up.name}: {up.tip} after {reviewed[:9]}")
        # diff and apply take a commit against its first parent, so a merge stands for the side branch it brings.
        commits = git(up.repo, "log", "--first-parent", "--format=%h %as %s", f"{reviewed}..{up.tip}",
                      "--", *up.files).splitlines()
        print("".join(f"  {line}\n" for line in commits) or "  no commits touch files we carry\n", end="")
        directories = {posixpath.dirname(path) for path in up.files}
        added = git(up.repo, "diff", "--name-only", "--no-renames", "--diff-filter=A", reviewed, up.tip).splitlines()
        added = [path for path in added if posixpath.dirname(path) in directories and path not in up.files]
        if added:
            print("  files added next to files we carry:")
            print("".join(f"    {path}\n" for path in added), end="")


def diff(upstreams, commit):
    if commit is None:
        for up in upstreams:
            subprocess.run(["git", "-C", up.repo, "diff", up.entry["reviewed"], up.tip, "--", *up.files], check=True)
    else:
        up, sha = owner(upstreams, commit)
        subprocess.run(["git", "-C", up.repo, "show", "--diff-merges=first-parent", sha, "--", *up.files], check=True)


def apply(root, upstreams, commit):
    up, sha = owner(upstreams, commit)
    path = up.entry["path"]
    patch = subprocess.run(
        ["git", "-C", up.repo, "diff", "--full-index", "--binary", "--no-renames",
         f"--src-prefix=a/{path}/", f"--dst-prefix=b/{path}/", f"{sha}^", sha, "--", *up.files],
        check=True, stdout=subprocess.PIPE).stdout
    if not patch:
        print(f"{commit} touches no file we carry")
        return 0
    # The three-way merge reads each file's upstream blobs, and a conflict keeps them in our index, so they must be
    # in our object store. An all-zero id stands for the missing side of an added or deleted file.
    blobs = {blob for line in patch.splitlines() if line.startswith(b"index ")
             for blob in line.split()[1].split(b"..") if blob.strip(b"0")}
    pack = subprocess.run(["git", "-C", up.repo, "pack-objects", "--stdout", "-q"], input=b"\n".join(blobs) + b"\n",
                          check=True, stdout=subprocess.PIPE).stdout
    subprocess.run(["git", "-C", root, "unpack-objects", "-q"], input=pack, check=True)
    return subprocess.run(["git", "-C", root, "apply", "--3way"], input=patch).returncode


def reviewed(root, upstreams, commit):
    up, sha = owner(upstreams, commit)
    entries = json.loads((root / UPSTREAM).read_text())
    entries[up.name]["reviewed"] = sha
    (root / UPSTREAM).write_text(json.dumps(entries, indent=2) + "\n")
    print(f"{up.name}: reviewed {sha}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--clone", type=pathlib.Path, default=pathlib.Path.home() / "Projects" / "Shipwright")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    commands.add_parser("diff").add_argument("commit", nargs="?")
    commands.add_parser("apply").add_argument("commit")
    commands.add_parser("reviewed").add_argument("commit")
    args = parser.parse_args()

    root = pathlib.Path(git(".", "rev-parse", "--show-toplevel").strip())
    entries = json.loads((root / UPSTREAM).read_text())
    upstreams = [Upstream(root, args.clone, name, entry, entries) for name, entry in entries.items()]
    match args.command:
        case "status":
            return status(upstreams)
        case "diff":
            return diff(upstreams, args.commit)
        case "apply":
            return apply(root, upstreams, args.commit)
        case "reviewed":
            return reviewed(root, upstreams, args.commit)


if __name__ == "__main__":
    sys.exit(main())
