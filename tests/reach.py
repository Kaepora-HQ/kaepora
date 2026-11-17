import subprocess

PROGRAM = None


def reachable(location, *placed):
    """Whether kaepora-reach lets Link collect what is at location, with each (location, item) pair in placed."""
    result = subprocess.run([PROGRAM, location, *(name for pair in placed for name in pair)],
                            capture_output=True, text=True)
    assert result.returncode == 0 and result.stdout in ("yes\n", "no\n"), result.stderr
    return result.stdout == "yes\n"
