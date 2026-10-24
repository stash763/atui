#!/usr/bin/env python3
"""Does a long session cost anything it does not give back?

The project's constraints are about small machines -- a 1.5MB binary was too large for
some targets, and the targets are machines with little RAM -- and yet until this script
nothing had ever looked at what atui costs over an afternoon of turns.  One turn is what
every other test does, and a leak that takes twenty turns to matter passes all of them.

So: start the real TUI on a pty, have a conversation of N turns with the real endpoint,
and after every turn ask the operating system two different questions.

    ATUI_BASE_URL=http://host:port/v1 ATUI_MODEL=name \\
        python3 tools/tui_soak.py [--turns 10] [--binary ./build/atui]

The first question is resident size (`ps -o rss`), which is the honest measure on a
small machine and the only one available on every platform.  The second is macOS's
`leaks`, which reports blocks with no pointers left to them.  A run is failing if
`leaks` reports any bytes, or if the resident size keeps climbing after warm-up.

Two shapes are distinguished on purpose.  Allocator arenas settling is a curve that
flattens; a leak is a line that does not.  The first three turns are warm-up and the
threshold is measured from turn 3 to the end, so a binary that grows 40KB and then
stops -- which is what a real run does -- is not reported as a leak.

Exit 0 clean, 1 a leak or steady growth, 2 the program could not be started or died.

And since a measurement of nothing is worth nothing, each turn is checked for having
happened: the token has to appear on the screen (a message typed but never submitted is
not a turn), and the app must never answer "(busy, try again)" (a turn that ends without
its thread being taken back refuses every prompt after it).  Both of these were added
after the fact.  The script's first baseline was taken over ten "turns" of which the
script could not have told whether any was answered, and the second-turn bug it now
catches was found by a person at a keyboard instead.  Whether the *answer* is right is
tui_smoke.py's job: it asks arithmetic whose result is not in the question, twice.

Baseline, measured 2026-10-22 on macOS against the llama.cpp endpoint, ten turns of
"remember the token MARIGOLD-n", each answered as far as the script of the day could
tell:

    turn   RSS kB     leaked
    1        3632          0
    2        3648          0
    3        3680          0
    ...      3680          0     (flat through turn 10)
    RSS after turn 2: 3648 kB, after turn 10: 3680 kB, growth 32 kB over 8 turns

48KB total, all of it in the first three turns, and zero leaked bytes at every sample.
The flat tail is the result; the number to worry about is a slope, not an offset.

Notes on the harness, all learned the same way as in tui_smoke.py: the pty is 80x25
because a pty is born 0x0 and a terminal library told the screen is zero wide draws
nothing; the message is typed a character at a time because a pty that is still
negotiating keyboard modes eats bursts; and the answer to "did the turn finish" is a
pause rather than a probe, because the endpoint is the only thing that knows how long
an answer takes.  `leaks` needs the process to be attachable, which a locally built
binary is.

The macOS-only part is the leak report; everywhere else the resident-size question is
still asked and the leak column says n/a.
"""
import argparse
import fcntl
import os
import pty
import re
import select
import subprocess
import struct
import sys
import termios
import time


def open_pty():
    master, slave = pty.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
    return master, slave


ANSI = re.compile(rb'\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()][B0]|\x1b[=>]')


def strip(buf):
    return ANSI.sub(b'', buf).decode('latin-1', 'replace')


# The app's own words for "I will not take this message".  Seeing them is not a
# slow turn or a hiccup: it means a previous turn is still held open, and every
# turn after it is refused the same way.  A soak that only measures memory sits
# through forty refused prompts and reports a flat, healthy line.
BUSY = '(busy, try again)'


def drain(master, seconds):
    """Read the pty for a while, returning (alive, bytes read).

    Reading is not optional: the buffer fills, the program blocks writing its
    screen, and a turn that was fine looks hung.
    """
    end = time.time() + seconds
    data = b''
    while time.time() < end:
        ready, _, _ = select.select([master], [], [], 0.1)
        if ready:
            try:
                chunk = os.read(master, 65536)
            except OSError:
                return False, data
            if not chunk:
                return False, data
            data += chunk
    return True, data


def type_line(master, text):
    for ch in text:
        os.write(master, ch.encode())
        time.sleep(0.01)
    # Enter on its own, after a gap.  Sent in the same burst as the text it can be
    # swallowed by whatever the terminal library is still negotiating, and the
    # message then sits in the prompt line unsubmitted -- which a memory-only soak
    # cannot tell from an answer.
    time.sleep(0.3)
    os.write(master, b'\r')


def resident_kb(pid):
    out = subprocess.run(['ps', '-o', 'rss=', '-p', str(pid)],
                         capture_output=True, text=True).stdout.strip()
    return int(out) if out else -1


def leaked_bytes(pid):
    """macOS `leaks`, as (blocks, bytes); None where the tool does not exist."""
    if not os.path.exists('/usr/bin/leaks'):
        return None
    out = subprocess.run(['leaks', '-q', str(pid)],
                         capture_output=True, text=True).stdout
    match = re.search(r'(\d+) leaks? for (\d+) total leaked bytes', out)
    return (int(match.group(1)), int(match.group(2))) if match else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--turns', type=int, default=10)
    ap.add_argument('--binary', default='./build/atui')
    ap.add_argument('--answer-wait', type=float, default=11.0,
                    help='how long to allow the endpoint per turn')
    ap.add_argument('--growth-limit', type=int, default=256,
                    help='kB the process may still be gaining after warm-up')
    args = ap.parse_args()

    if 'ATUI_BASE_URL' not in os.environ:
        sys.stderr.write('ATUI_BASE_URL (and ATUI_MODEL) are required: this soak does '
                         'real turns against a real endpoint\n')
        return 2

    master, slave = open_pty()
    process = subprocess.Popen([args.binary], stdin=slave, stdout=slave,
                               stderr=slave, preexec_fn=os.setsid)
    os.close(slave)
    screen = drain(master, 2.0)[1]

    print('%-6s %8s %8s' % ('turn', 'RSS kB', 'leaked'))
    rows = []
    for i in range(1, args.turns + 1):
        if process.poll() is not None:
            sys.stderr.write('atui died in turn %d (status %d)\n'
                             % (i, process.returncode))
            return 2
        token = 'MARIGOLD-%02d' % i
        type_line(master, 'Remember the token %s and reply with just ok.' % token)
        alive, chunk = drain(master, args.answer_wait)
        screen += chunk
        text = strip(screen)

        # Two things this checks besides memory, both of which it checked not at
        # all while it was only measuring RSS: that the message got in (an Enter
        # lost in a burst means the "turn" was silence), and that the app did not
        # refuse it as busy (a finished turn whose thread was never taken back).
        if token not in text:
            sys.stderr.write('turn %d: the prompt never reached the transcript; the '
                             'message was typed and not submitted\n' % i)
            return 1
        if BUSY in text:
            sys.stderr.write('turn %d: %s -- a finished turn is holding the app '
                             'open; every later prompt is refused\n' % (i, BUSY))
            return 1
        if not alive:
            sys.stderr.write('atui closed the screen in turn %d\n' % i)
            return 2

        leak = leaked_bytes(process.pid)
        rows.append((i, resident_kb(process.pid), -1 if leak is None else leak[1]))
        column = 'n/a' if leak is None else str(rows[-1][2])
        print('%-6d %8d %8s' % (rows[-1][0], rows[-1][1], column), flush=True)

    # Leave the way a person would.  A soak that ends in SIGKILL would not notice an
    # exit path that hangs, and this one has already been found waiting once.
    os.write(master, b'\x1b')
    os.write(master, b'x')
    drain(master, 3.0)
    forced = process.poll() is None
    if forced:
        # The child can leave between `poll()` reporting nothing and the signal
        # arriving -- poll() only reports what it has reaped.  A process that closed
        # the screen and then exited on its own is the good outcome, not a crash.
        try:
            os.killpg(os.getpgid(process.pid), 9)
        except ProcessLookupError:
            forced = False
    status = process.wait()
    os.close(master)

    # Warm-up is three turns: an allocator taking arena is not a leak, and calling it
    # one is how a check stops being believed.
    warm = rows[2][1] if len(rows) > 2 else rows[0][1]
    growth = rows[-1][1] - warm
    leaks = [r for r in rows if r[2] > 0]

    print('RSS after turn %d: %d kB, after turn %d: %d kB, growth %d kB'
          % (3 if len(rows) > 2 else 1, warm, rows[-1][0], rows[-1][1], growth))
    print('exit: %s status %s' % ('still running, killed' if forced else 'ESC x', status))

    if leaks:
        print('FAIL: unreachable memory: %s' % ', '.join('turn %d: %d B' % (r[0], r[2])
                                                          for r in leaks))
        return 1
    if growth > args.growth_limit:
        print('FAIL: still gaining %d kB after warm-up (limit %d kB)'
              % (growth, args.growth_limit))
        return 1
    if forced or status != 0:
        print('FAIL: did not leave by itself')
        return 1
    print('SOAK OK: %d turns, %d kB growth, no unreachable memory'
          % (args.turns, growth))
    return 0


if __name__ == '__main__':
    sys.exit(main())
