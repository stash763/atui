#!/usr/bin/env python3
"""Is the app's event loop alive while a turn is in flight?

What this measures, and what it does not, both matter more than the result.

Typed at atui: a message, then -- before its answer arrives -- a second message.  A
working event loop answers the second one with `(busy, try again)`, because a turn is in
flight, and then still delivers the first answer.  That single observation covers three
things at once: the keystroke was processed during the wait, the in-flight turn was not
disturbed by it, and a second message was not smuggled into the conversation.

What it does not measure is the reason the threading work was started.  The two waits
that a state machine cannot yield -- `gethostbyname()` before the first byte, and a tool
call's child process -- are the ones only a thread fixes, and neither is reachable from
here: the endpoint this test uses is an IP literal, and reaching a tool call needs the
model to ask for one and a person to approve it.  Those are measured on the OS/2 box
(TESTING.md, the tool-call freeze section) and not claimed from here.

Both configurations are worth running, and they are expected to *agree* during streaming
-- the unthreaded build pumps with non-blocking steps, so its loop is alive between
bytes too.  They differ only inside the two waits named above.

    ATUI_BASE_URL=http://host:port/v1 ATUI_MODEL=name \\
        python3 tools/tui_liveness.py [binary]

Exit 0 when the busy line appeared during the turn and the answer still arrived, 1 when
either did not, 3 when the model answered too fast to poke at (re-run, or ask it
something slower).
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

BUSY = '(busy, try again)'
# The answer's marker.  It must not appear in either typed message: the transcript echoes
# what was sent, and an echo read as an answer is how this harness once lied
# (tui_smoke.py's third lesson).
MARKER = '8042'
FIRST = 'What is 4021 plus 4021? Reply with just the number.'
SECOND = 'Later.'
# How long to wait before poking.  Long enough that the app is certainly mid-turn, short
# enough that this runs before the answer on a fast model.
POKE_AT = 3.0


# Filled in by the cleanup: how long SIGTERM took to become an exit, or None.
LATENCY = [ None ]


def strip(buf):
    return ANSI.sub(b'', buf).decode('latin-1', 'replace')


def type_line(master, text):
    # One character at a time: burst typing loses spaces and the Enter (lesson two).
    for ch in text:
        os.write(master, ch.encode())
        time.sleep(0.02)
    os.write(master, b'\r')


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else './build/atui'

    master, slave = pty.openpty()
    # A pty is created 0x0 and a zero-wide screen draws nothing (lesson one).
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
    proc = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave,
                            preexec_fn=os.setsid)

    out = b''
    started = time.time()
    sent_first = False
    poked = False
    poked_at = None
    typed_at = None
    t_answer = None
    t_busy = None

    try:
        while time.time() - started < 120:
            r, _, _ = select.select([master], [], [], 0.2)
            if r:
                try:
                    data = os.read(master, 65536)
                except OSError:
                    break
                if not data:
                    break
                out += data

            screen = strip(out)
            now = time.time() - started

            if not sent_first and now > 2.0:
                type_line( master, FIRST )
                sent_first = True
                typed_at = now
            elif sent_first and not poked and t_answer is None \
                    and now - typed_at > POKE_AT:
                # Mid-turn: the first answer has not arrived.  If it happens during the
                # typing below, that is what `t_answer is None` is for on the next pass.
                type_line( master, SECOND )
                poked = True
                poked_at = now

            if t_answer is None and MARKER in screen:
                t_answer = now
            if t_busy is None and BUSY in screen:
                t_busy = now
            if t_answer is not None and ( t_busy is not None or poked ):
                break
    finally:
        # Term, five seconds, then kill.  The grace period is not tidiness: an earlier
        # version of this harness hung forever on `wait()`, which turned out to be a real
        # finding -- atui ignores SIGTERM (measured: an idle instance survived it for six
        # seconds and needed SIGKILL), so any harness that asks politely waits forever.
        # That belongs to the terminal layer, not to threading, and is noted in TESTING.md.
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
        except OSError:
            pass
        killed = time.time()
        while proc.poll() is None and time.time() - killed < 5.0:
            time.sleep(0.05)
        if proc.poll() is None:
            try:
                os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
            except OSError:
                pass
            proc.wait()
        LATENCY[0] = time.time() - killed
        try:
            os.close(master)
        except OSError:
            pass

    # One last look: both lines may have arrived in the bytes after the loop's last read.
    screen = strip(out)
    if t_busy is None and BUSY in screen:
        t_busy = t_answer if t_answer else 0.0

    def show( v ):
        return '%5.1fs' % v if v is not None else ' never'
    print( '--- liveness: %s ---' % binary )
    print( '  closed in %5.1fs%s' % ( LATENCY[0], '' if LATENCY[0] < 4.9
                                      else ' (only after SIGKILL: SIGTERM is ignored)' ) )
    print( '  message sent at %s   poked at %s   busy line at %s   answer at %s'
           % ( show( typed_at ), show( poked_at ), show( t_busy ),
               show( t_answer ) ) )

    if not poked:
        print( 'INCONCLUSIVE: the model answered before the poke (exit 3, re-run)' )
        return 3
    if t_busy is None:
        print('FAIL: typed during the turn and nothing answered it (loop blocked?)')
        return 1
    if t_answer is None:
        print('FAIL: the poke was answered but the first answer never arrived')
        return 1
    print('OK: the loop ran during the turn, the turn finished, and no second message '
          'was sent')
    return 0


if __name__ == '__main__':
    sys.exit(main())
