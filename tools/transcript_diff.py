#!/usr/bin/env python3
"""Compare two atui builds on one identical streamed transcript, at the pty level.

The settled-prefix cache in the transcript view (SettleScan in tui/markdown.h)
must not change what the screen ends up with -- only what it costs to get
there.  The markdown layer proves its half with the differential self-tests;
this proves the view's half: it runs a build on a pty against fake_stream
serving one fixed document, watches the `layout a=<cost> b=<lines>` debug
lines, and reports the final row count.  Same document, same pty size, same
chunking: two builds that agree on the final b (and never went backwards) kept
the same transcript.  A keepRows off-by-one shows up as b too large (rows kept
twice) or too small (rows thrown away and not rebuilt).

    python3 tools/fake_stream.py --port 8799 --raw doc.md --chunk 9 --delay 0.02 &
    python3 tools/transcript_diff.py ./build/atui 8799 ENDTOKEN-9137
    ...  prints: layouts=<k> b_final=<n> monotonic=<yes|no> screen=<sha1 of tail>

Exit 0 when the end token reached the screen and the row counts never went
backwards, 1 otherwise.  Diffing is done by comparing two runs' lines; the
final b is the number that has to match.

Two harness facts, each learned the way the others in this file were:
  * the debug lines go to stderr, and stderr on the pty swallows them -- the
    child's stderr is a file instead, and the file's growth is what says
    "a layout just happened";
  * a config file that exists replaces the environment (TESTING.md), so the
    run gets a scratch HOME and an ATUI_CONFIG that points at nothing.
"""
import fcntl
import hashlib
import os
import pty
import re
import select
import signal
import struct
import sys
import termios
import time

ANSI = re.compile(rb'\x1b\[[0-9;?]*[a-zA-Z]|\x1b[()\d][B0]|\x1b[=>]|\r')
LAYOUT = re.compile(rb'layout a=\d+ b=(-?\d+)')


def strip(buf):
    return ANSI.sub(b'', buf)


def run(binary, port, endtoken, seconds=120):
    home = '/tmp/atui-transcript-diff'
    os.makedirs(home, exist_ok=True)
    env = dict(os.environ)
    env.update(TERM='xterm', HOME=home,
               ATUI_CONFIG=os.path.join(home, 'nosuch.ini'),
               ATUI_BASE_URL='http://127.0.0.1:%d/v1' % port,
               ATUI_MODEL='fakemodel', ATUI_TURN_DEBUG='1')

    errpath = os.path.join(home, 'err-%d.txt' % os.getpid())
    err = open(errpath, 'wb')

    pid, fd = pty.fork()
    if pid == 0:
        os.dup2(err.fileno(), 2)
        try:
            os.execve(binary, [binary], env)
        except Exception:
            os._exit(127)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack('HHHH', 25, 80, 0, 0))

    out = b''
    start = time.time()
    typed = False
    last_debug = start
    errsize = 0
    while time.time() - start < seconds:
        if not typed and time.time() - start > 2.0:
            for ch in 'stream the settle test please':   # one char at a time:
                os.write(fd, ch.encode('latin-1'))       # burst typing loses
                time.sleep(0.02)                         # bytes to tvision
            time.sleep(0.3)
            os.write(fd, b'\r')
            typed = True
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        es = os.fstat(err.fileno()).st_size
        if es != errsize:
            errsize = es
            last_debug = time.time()
        if endtoken.encode() in strip(out) and time.time() - last_debug > 3.0:
            break       # stream closed, final layout done

    try:
        os.kill(pid, signal.SIGKILL)
        os.waitpid(pid, 0)
    except OSError:
        pass
    err.close()

    s = strip(out)
    with open(errpath, 'rb') as f:
        slog = f.read()
    os.unlink(errpath)
    bs = [int(m) for m in LAYOUT.findall(slog)]
    monotonic = all(bs[i] >= bs[i - 1] for i in range(1, len(bs)))
    tail = s.split(b'\n')[-4:]
    print('binary=%s layouts=%d b_final=%d monotonic=%s screen=%s'
          % (binary, len(bs), bs[-1] if bs else -1,
             'yes' if monotonic else 'NO',
             hashlib.sha1(b'\n'.join(tail)).hexdigest()[:12]))
    return 0 if (bs and monotonic and endtoken.encode() in s) else 1


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(__doc__)
        sys.exit(2)
    sys.exit(run(sys.argv[1], int(sys.argv[2]), sys.argv[3]))
