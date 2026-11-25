#!/usr/bin/env python3
"""Dump the raw body of a request atui sent, so it can be read as a document.

    python3 tools/dump_request.py [--port 8811] [--dir /tmp/atui_req]
    ATUI_LIVE_BASE_URL=http://127.0.0.1:8811/v1 ./build/atui --self-test

    With --listen 0.0.0.0 another machine can point at it, which is how a keystroke typed
    on Windows ends up as bytes this side: what that machine's console decided to send is
    a fact about it, and the only way to read that fact is the captured body.

Answers every POST with a tiny valid SSE stream, writes the bytes it was given to
`<dir>/NNN.json`, and prints whether Python's `json` can parse them.

It exists because a server answering `500` with `parse error at line 1, column 2217` is
making a statement about a document, and there is only one way to answer that: get the
bytes. Validate them with `json.load`, then replay them at the real endpoint with and
without whatever change you suspect, and you know whose side the problem is on rather than
guessing. Guessing is what `TESTING.md` records in the entry before this tool was written
-- an accusation about a server, made while the fault was a missing brace in a tool schema
in this program (W7).

Two things worth noticing when you use it. A server here never parses the body, so the
live tests pass against it; that is the point, and also a warning about what "passes"
meant before. And the captured bodies differ from each other, and from the same test on
another machine, because the system prompt names the host -- so a defect position that
moves between runs is a position in the request, not in the model's answer.
"""
import argparse
import json
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

CFG = {}
COUNT = [0]


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = json.dumps({"object": "list",
                           "data": [{"id": "dumpmodel", "object": "model"}]}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length") or 0))
        COUNT[0] += 1
        path = os.path.join(CFG["dir"], "%03d.json" % COUNT[0])
        with open(path, "wb") as f:
            f.write(raw)
        try:
            json.loads(raw.decode("utf-8"))
            verdict = "valid JSON"
        except Exception as e:
            verdict = "INVALID: %s" % e
        print("%s (%d bytes): %s" % (path, len(raw), verdict), flush=True)

        sse = b"data: " + json.dumps(
            {"choices": [{"index": 0, "delta": {"content": "ok"}}]}).encode() + b"\n\n"
        sse += b"data: " + json.dumps(
            {"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]}).encode() + b"\n\n"
        sse += b"data: [DONE]\n\n"
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(sse)))
        self.end_headers()
        self.wfile.write(sse)

    def log_message(self, *args):
        pass


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", type=int, default=8811)
    p.add_argument("--dir", default="/tmp/atui_req")
    p.add_argument("--listen", default="127.0.0.1",
                   help="bind address; 0.0.0.0 to let another machine reach it")
    a = p.parse_args()
    os.makedirs(a.dir, exist_ok=True)
    CFG.update(dir=a.dir)
    print("dumping requests to %s, listening on %s:%d" % (a.dir, a.listen, a.port), flush=True)
    ThreadingHTTPServer((a.listen, a.port), Handler).serve_forever()


main()
