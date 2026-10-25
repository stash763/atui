#!/usr/bin/env python3
"""A deterministic streaming endpoint: one fixed answer, fixed chunks, fixed pace.

    python3 tools/fake_stream.py [--port 8799] [--words 2400] [--chunk 6] [--delay 0.01]

Streams a canned answer of `--words` words, `--chunk` words at a time, `--delay`
seconds between chunks.  It exists so two builds of atui can be shown the
*identical* stream: measuring a layout cost against a live model compares two
answers of different length, which measures nothing.
"""
import argparse
import json
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CFG = {}
STATS = {"chunks": 0, "bytes": 0}

WORDS = ("the driver keeps a shadow of the screen and sends only what changed "
         "which is why a row written by something else stays wrong forever "
         "so a repaint has to be asked for out loud by the code that knows "
         "the screen has been disturbed and not by the view that drew it ").split()


def answer(words):
    out = []
    i = 0
    while len(out) < words:
        if i and i % 40 == 0:
            out.append("\n\n## Section %d\n\n" % (i // 40))
        out.append(WORDS[i % len(WORDS)] + " ")
        i += 1
    return "".join(out)


class H(BaseHTTPRequestHandler):
    def do_GET(self):
        body = json.dumps({"object": "list",
                           "data": [{"id": "fakemodel", "object": "model"}]})
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body.encode())

    def do_POST(self):
        text = answer(CFG["words"])
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        step = CFG["chunk"]
        for i in range(0, len(text), step):
            piece = text[i:i + step]
            obj = {"choices": [{"index": 0, "delta": {"content": piece}}]}
            try:
                self.wfile.write(b"data: " + json.dumps(obj).encode() + b"\n\n")
                self.wfile.flush()
            except Exception:
                return
            STATS["chunks"] += 1
            STATS["bytes"] += len(piece)
            time.sleep(CFG["delay"])
        try:
            self.wfile.write(b'data: {"choices":[{"index":0,"delta":{},'
                             b'"finish_reason":"stop"}]}\n\ndata: [DONE]\n\n')
            self.wfile.flush()
        except Exception:
            pass

    def log_message(self, *a):
        pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8799)
    p.add_argument("--words", type=int, default=2400)
    p.add_argument("--chunk", type=int, default=6)
    p.add_argument("--delay", type=float, default=0.01)
    a = p.parse_args()
    CFG.update(words=a.words, chunk=a.chunk, delay=a.delay)
    print("fake stream on %d: %d words, %d-byte chunks, %s s apart"
          % (a.port, a.words, a.chunk, a.delay), flush=True)
    ThreadingHTTPServer(("127.0.0.1", a.port), H).serve_forever()


main()
