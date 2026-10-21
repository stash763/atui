#!/usr/bin/env python3
"""End-to-end smoke test of the TUI chat path, through a real pty.

The self-test covers the transport, the framing and the turn as objects; nothing in
it draws a screen, and the part that was rewritten -- the timer pumping a ChatTurn
into the transcript -- is only exercised when the program is actually running in a
terminal.  So this starts atui on a pty, types a message, waits for the answer to
appear on the screen, and reports what the screen said.

    ATUI_BASE_URL=http://host:port/v1 ATUI_MODEL=name \\
        python3 tools/tui_smoke.py ['Reply with exactly: PTY-OK']

Exit 0 when the answer shows up, 1 when it does not (the screen dump goes to stdout
either way), 2 when the program could not be started or died.  A prompt with no
network in it (--network not compiled) still answers, from the stub, and the stub's
line does not contain the marker, so this test also says whether the build can chat.

Deliberately no terminal emulator: it looks for the marker in the raw bytes after
stripping escape sequences, which is enough to distinguish "the model answered and
the transcript drew it" from "nothing happened", and is not enough to check layout
(that is what the dialog sizes and the 80x25 notes in TESTING.md are for).

Three things this harness had to learn the hard way, each of which reads as "atui is
broken" when it is not:

  * a pty is created 0x0, and a terminal library told the screen is zero wide draws
    nothing -- the first run read 248 bytes of query noise in 70 seconds.  It is set
    to 80x25 below.
  * the message must be typed a character at a time.  Written in one burst it arrived
    with the spaces missing and the Enter never reached the app, which is what a
    terminal library that is still negotiating (tvision asks about modifyOtherKeys
    and the kitty keyboard on startup, and a bare pty answers nothing) does with
    bytes that turn up in the middle.
  * the marker cannot be a string the message contains, because the message is drawn
    on the transcript the moment it is sent: "reply with exactly: PTY-OK" is answered
    by its own echo.  Hence the arithmetic.

Not covered: the approval dialog.  It needs the model to ask for a tool the policy
will not clear on its own, which is not something to make a smoke test depend on;
that path is covered at the state-machine level by tests/chat_turn_test.cpp.
"""
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time

ANSI = re.compile(rb'\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()][B0]|\x1b[=>]|\r')


def strip(buf):
    return ANSI.sub(b'', buf).decode('latin-1', 'replace')


def main():
    # The marker cannot be a string the message itself contains, because the message
    # is drawn on screen the moment it is sent: an instruction like "reply with
    # exactly: PTY-OK" would be answered by its own echo.  So the check is something
    # the program has to work out: 8042 is nowhere in the question.
    marker = '8042'
    message = sys.argv[1] if len(sys.argv) > 1 else \
        'What is 4021 plus 4021? Reply with just the number.'
    if len(sys.argv) > 2:
        marker = sys.argv[2]
    binary = os.environ.get('ATUI', './build/atui')
    budget = float(os.environ.get('ATUI_SMOKE_SECONDS', '60'))
    # How long before the message is typed: the app has to have drawn its prompt.
    type_after = float(os.environ.get('ATUI_SMOKE_TYPE_AFTER', '2.0'))

    if not os.path.exists(binary):
        print('no such binary: %s' % binary)
        return 2

    env = dict(os.environ)
    env.setdefault('TERM', 'xterm')
    env.pop('ATUI', None)

    pid, fd = pty.fork()
    if pid == 0:                                     # the child becomes atui
        try:
            os.execve(binary, [binary], env)
        except Exception as e:                        # pragma: no cover
            sys.stderr.write('exec failed: %s\n' % e)
            os._exit(127)

    # Give the pty a size.  A pty is created 0x0 unless somebody says otherwise, and
    # a terminal library told the screen is zero wide draws nothing at all: the first
    # run of this script read 248 bytes of terminal-query noise in 70 seconds and no
    # screen, which is exactly what that looks like.
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))

    out = b''
    start = time.time()
    typed = False
    dead = False
    while time.time() - start < budget:
        if not typed and time.time() - start > type_after:
            # Character by character, with a gap.  Written as one burst the text
            # arrived with its spaces missing and the Enter never reached the app,
            # which is what a terminal library that is still waiting for a query
            # response does with bytes that turn up mid-negotiation.
            for ch in message:
                os.write(fd, ch.encode('latin-1'))
                time.sleep(0.05)
            time.sleep(0.3)
            os.write(fd, b'\r')
            typed = True
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                dead = True
                break
            if not chunk:
                dead = True
                break
            out += chunk
            if marker in strip(out):
                break
        else:
            pid_done, _ = os.waitpid(pid, os.WNOHANG)
            if pid_done:
                dead = True
                break

    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except OSError:
        pass
    os.close(fd)

    screen = strip(out)
    print('--- screen (%d bytes, %.1fs, %s) ---'
          % (len(out), time.time() - start, 'exited' if dead else 'still running'))
    print(screen[-4000:])
    print('--- end ---')
    if marker in screen:
        print('SMOKE OK: the answer reached the transcript')
        return 0
    if dead:
        print('SMOKE FAIL: atui exited before answering')
        return 2
    print('SMOKE FAIL: no %s on screen within the budget (marker must not appear '
          'in the message: the message is echoed to the transcript)' % marker)
    return 1


sys.exit(main())
