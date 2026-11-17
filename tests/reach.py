import json
import subprocess

PROGRAM = None


def reachable(location, *placed, settings=None):
    """Whether kaepora-reach lets Link collect what is at location, with each (location, item) pair in placed, under
    the settings given in the spoiler log's words."""
    args = [json.dumps(settings or {}), location, *(name for pair in placed for name in pair)]
    result = subprocess.run([PROGRAM, *args], capture_output=True, text=True)
    assert result.returncode == 0 and result.stdout in ("yes\n", "no\n"), result.stderr
    return result.stdout == "yes\n"
