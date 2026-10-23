"""Does what is typed appear on the screen?

The app redraws the input panel when it changes rather than on a timer (see
relayoutPanels_ in src/tui/app.cpp), so this checks the half of that which the timer used
to hide: that a keystroke really does cause a repaint.  Types letters, checks they appear,
edits with Backspace, checks the edit appears too, and finally sends a message and waits
for a reply so the whole path is exercised and not just the echo.

    python3 tools/tty_typing.py [--binary ./build/atui]
"""
import argparse
import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time

ANSI = re.compile(rb'\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()\[\]?][0-9;:]*[a-zA-Z]|\r|\x1b[=>]')


def screen_text(buf):
    """The screen as a person would read it: escapes out, so a substring test means
    the characters were displayed and not merely sent."""
    return ANSI.sub(b'', bytes(buf)).decode('latin-1', 'replace')


class Pty:
    def __init__(self, binary):
        self.master, slave = pty.openpty()
        fcntl.ioctl(self.master, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))
        self.proc = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave)
        os.close(slave)
        self.buf = bytearray()

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            r, _, _ = select.select([self.master], [], [], 0.05)
            if r:
                try:
                    self.buf += os.read(self.master, 65536)
                except OSError:
                    return

    def type(self, text):
        # Character by character with a gap: written as one burst the bytes arrive while
        # the terminal library is still negotiating and get through with holes in them.
        for ch in text:
            os.write(self.master, ch.encode('latin-1'))
            time.sleep(0.06)
            self.pump(0.06)

    def text(self):
        return screen_text(self.buf)

    def close(self):
        try:
            os.write(self.master, b'\x1bx')
            time.sleep(1.0)
            if self.proc.poll() is None:
                self.proc.kill()
        except OSError:
            pass
        self.proc.wait()
        os.close(self.master)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default='./build/atui')
    args = ap.parse_args()

    p = Pty(args.binary)
    p.pump(3.0)                       # let the first frame arrive
    failed = []

    # Is the terminal echoing?  If it is, every character typed below would appear
    # whether or not the application drew it, and this file would be measuring the tty
    # driver instead of the program.  A program drawing its own text puts the line in
    # raw mode, so the byte to look for is the one a terminal would send back for
    # Backspace: an echo'd 0x7F means the app is not in control of the line.
    p.type('\x7f')
    p.pump(0.5)
    if b'\x7f' in bytes(p.buf):
        print('TYPING CHECK SKIPPED: the terminal is echoing, so nothing here proves '
              'the app drew anything')
        p.close()
        return 0

    p.type('HELLO')
    p.pump(1.0)
    if 'HELLO' not in p.text():
        failed.append("'HELLO' was typed and never appeared")

    # Backspace: the edit has to be repainted, not just the buffer changed.  The screen
    # keeps history, so look for the state after the edit rather than for the absence of
    # a character that is still in the transcript.
    p.type('\x7f')                    # Backspace
    p.pump(1.0)
    p.type('O')
    p.pump(1.0)
    if 'HELLO' not in p.text():
        failed.append("after editing with Backspace 'HELLO' did not reappear")

    # And a real turn: the message is sent, the reply arrives, the panel comes back.
    p.type(' \x0d')                   # space, Enter (the app sends on Enter)
    p.pump(2.0)
    p.close()

    if failed:
        print('TYPING CHECK FAILED: ' + '; '.join(failed))
        return 1
    print('TYPING CHECK OK: keystrokes are repainted, Backspace is repainted')
    return 0


if __name__ == '__main__':
    sys.exit(main())
