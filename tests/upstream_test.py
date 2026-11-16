"""End-to-end test of tools/upstream.py on a throwaway upstream, its clone, and a downstream repo.

Usage: python3 -I tests/upstream_test.py
"""

import json
import os
import pathlib
import subprocess
import sys
import tempfile

TOOL = pathlib.Path(__file__).resolve().parent.parent / "tools" / "upstream.py"
ENV = os.environ | {
    "GIT_CONFIG_GLOBAL": os.devnull,
    "GIT_CONFIG_NOSYSTEM": "1",
    "GIT_AUTHOR_NAME": "test",
    "GIT_AUTHOR_EMAIL": "test@example.com",
    "GIT_COMMITTER_NAME": "test",
    "GIT_COMMITTER_EMAIL": "test@example.com",
}
LINES = [f"line {i}\n" for i in range(1, 11)]


def git(repo, *args):
    return subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True, text=True, env=ENV).stdout


def commit(repo, files, message):
    for path, text in files.items():
        (repo / path).parent.mkdir(parents=True, exist_ok=True)
        (repo / path).write_text(text)
    git(repo, "add", "-A")
    git(repo, "commit", "-qm", message)
    return git(repo, "rev-parse", "HEAD").strip()


def tool(downstream, clone, *args):
    return subprocess.run(
        [sys.executable, "-I", TOOL, "--clone", clone, *args],
        cwd=downstream, check=True, capture_output=True, text=True, env=ENV,
    ).stdout


def main():
    with tempfile.TemporaryDirectory() as tmp:
        upstream, clone, downstream = (pathlib.Path(tmp) / name for name in ("upstream", "clone", "downstream"))
        git(tmp, "init", "-qb", "main", upstream)
        base = commit(upstream, {"src/carried.txt": "".join(LINES), "src/other.txt": "other\n"}, "initial")
        git(tmp, "clone", "-q", upstream, clone)

        git(tmp, "init", "-qb", "main", downstream)
        upstream_json = {"Fake": {"url": "fake", "branch": "main", "imported": base, "reviewed": base, "path": "shipwright"}}
        # Close enough to the upstream edit that the plain patch does not apply and git needs a three-way merge.
        edited = LINES[:3] + ["line 4 edited downstream\n"] + LINES[4:]
        commit(downstream, {"shipwright/UPSTREAM.json": json.dumps(upstream_json), "shipwright/src/carried.txt": "".join(edited)}, "import")

        carried_change = commit(upstream, {"src/carried.txt": "".join(LINES[:1] + ["line 2 edited upstream\n"] + LINES[2:])}, "edit carried file")
        commit(upstream, {"src/other.txt": "other edited\n"}, "edit uncarried file")
        commit(upstream, {"src/new.txt": "new\n"}, "add file next to carried file")
        git(upstream, "switch", "-qc", "side")
        side = LINES[:1] + ["line 2 edited upstream\n"] + LINES[2:8] + ["line 9 edited on a side branch\n"] + LINES[9:]
        commit(upstream, {"src/carried.txt": "".join(side)}, "side branch commit")
        git(upstream, "switch", "-q", "main")
        git(upstream, "merge", "-q", "--no-ff", "-m", "merge side branch", "side")
        head = git(upstream, "rev-parse", "HEAD").strip()

        status = tool(downstream, clone, "status")
        assert "edit carried file" in status, status
        assert "edit uncarried file" not in status, status
        assert "src/new.txt" in status, status
        # The merge is listed in place of the commit it brings, so applying each listed commit applies each change once.
        assert "merge side branch" in status, status
        assert "side branch commit" not in status, status

        tool(downstream, clone, "apply", carried_change)
        tool(downstream, clone, "apply", head)
        merged = (downstream / "shipwright/src/carried.txt").read_text()
        expected = (LINES[:1] + ["line 2 edited upstream\n", LINES[2], "line 4 edited downstream\n"] + LINES[4:8]
                    + ["line 9 edited on a side branch\n"] + LINES[9:])
        assert merged == "".join(expected), merged

        tool(downstream, clone, "reviewed", head)
        assert json.loads((downstream / "shipwright/UPSTREAM.json").read_text())["Fake"]["reviewed"] == head
    print("upstream tool test passed")


if __name__ == "__main__":
    main()
