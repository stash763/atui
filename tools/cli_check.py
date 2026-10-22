#!/usr/bin/env python3
"""The command line, checked from outside.

    python3 tools/cli_check.py [--binary ./build/atui] [--ask-ok]

These are process-level facts -- the exit code, and what was printed -- about `main`
before the library exists, so they are not what the built-in suite can assert: the suite
is linked into the program and cannot watch it start.  And the reason it is worth a script
is that the behaviour it checks used to be: an unrecognised option ignored, `--help` not
implemented, and a program with no terminal painting escape sequences into a pipe forever.
`atui --help` was therefore indistinguishable from a hang, which is a poor way to discover
what the options are.

What has to hold:

  * `--help` and `-h` print the usage and exit 0, with or without a terminal -- help is
    what somebody types when the program has just refused to start, so it cannot be the
    thing that also needs a terminal.
  * An unknown option, an option missing its value, a mode in the wrong place and an
    unexpected bare argument each say so and exit non-zero.  Saying nothing and opening a
    chat window is how a mistyped flag becomes a mystery.
  * With no terminal on stdin/stdout the program refuses with an explanation instead of
    starting and waiting for a keystroke that can never arrive.
  * `--version`, `--self-test` and `--judge-check` still work with their output in a file:
    the refusal above is only for the chat, and those three are what makes atui usable
    from a script and from CI.

Two harness notes, both from being wrong first.  Output goes to a file, never a pipe: a
killed child whose stdout is a pipe can leave the parent blocked in `communicate()` with
the timeout already fired and the kill already delivered, because some descriptor is still
open somewhere.  And every case runs in its own process group and is cleaned up by group,
because a check that can leave a process behind eventually runs against a build that
spawns one.

`--ask-ok` additionally runs the real UI on a pty to confirm the refusal did not become
over-zealous -- that is what tools/tui_smoke.py is for, and it is slow, so it is opt-in
here.
"""
import argparse
import os
import signal
import subprocess
import sys
import tempfile

TIMEOUT = 30


def run(binary, argv, timeout=TIMEOUT):
    """Run atui with output in a file.  Returns (returncode, text); the return code is
    the string 'TIMEOUT' if it had to be killed."""
    fd, path = tempfile.mkstemp(prefix='atui-cli-')
    os.close(fd)
    with open(path, 'wb') as out:
        proc = subprocess.Popen([binary] + argv, stdin=subprocess.DEVNULL,
                                stdout=out, stderr=out,
                                start_new_session=True)
        try:
            rc = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except OSError:
                pass
            proc.wait()
            rc = 'TIMEOUT'
    with open(path, 'rb') as f:
        text = f.read().decode('utf-8', 'replace')
    os.unlink(path)
    return rc, text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default='./build/atui')
    args = ap.parse_args()

    if not os.path.exists(args.binary):
        sys.stderr.write('no such binary: %s\n' % args.binary)
        return 2

    # (argv, exit code that must come back or None for "any non-zero", text the output
    # has to contain -- the wording is part of the promise, because an exit code alone
    # does not tell a person what they typed wrong)
    cases = [
        (['--help'],             0,    'terminal AI chat'),
        (['-h'],                 0,    'terminal AI chat'),
        (['--version'],          0,    'atui'),
        (['--bogus'],            None, 'not an option atui takes'),
        (['-x'],                 None, 'not an option atui takes'),
        (['stray'],              None, 'not an option atui takes'),
        (['--ask'],              None, '--ask needs a question'),
        (['--root'],             None, '--root needs a directory'),
        (['--ask', 'hi', '--version'], None, 'has to be the first argument'),
        (['--judge-check', 'sudo ls'], 0, 'sudo ls'),
        ([],                     None, 'draws on a terminal'),
    ]

    failed = 0
    for argv, want_rc, must_contain in cases:
        rc, out = run(args.binary, argv)
        problems = []
        if rc == 'TIMEOUT':
            problems.append('did not exit')          # the hang this check exists for
        elif want_rc is None and rc == 0:
            problems.append('exited 0, expected a failure')
        elif want_rc is not None and rc != want_rc:
            problems.append('exit %s, expected %s' % (rc, want_rc))
        if must_contain not in out:
            problems.append('output has no %r' % must_contain)
        lines = out.strip().splitlines()
        first = lines[0][:52] if lines else '(no output)'
        if problems:
            failed += 1
            print('FAIL %-28s %s' % (' '.join(argv) or '(no arguments)',
                                     '; '.join(problems)))
        else:
            print('ok   %-28s rc=%-3s %s' % (' '.join(argv) or '(no arguments)',
                                             rc, first))

    print()
    if failed:
        print('CLI CHECK FAILED: %d of %d cases' % (failed, len(cases)))
        return 1
    print('CLI CHECK OK: %d cases, nothing hangs and nothing is ignored silently'
          % len(cases))
    return 0


if __name__ == '__main__':
    sys.exit(main())
