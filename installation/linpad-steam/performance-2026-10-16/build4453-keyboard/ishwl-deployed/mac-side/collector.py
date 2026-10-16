#!/usr/bin/env python3
"""Device transfer server for the LinPad workspace.

Same as server.py -- serves this directory's ``serve/`` folder for the iPad's wget and
writes POSTs to ``reports/probe.txt`` -- with one addition: a POST to
``/report?name=<name>`` writes ``reports/<name>.txt`` instead, so a series of reports from
one device run (a watcher's samples, say) is kept instead of overwriting one file.

    cd work/performance && python3 collector.py        # 192.168.0.104:8766
"""
import re
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

BASE = Path(__file__).resolve().parent
REPORTS = BASE / "reports"
QUEUE = BASE / "cmd" / "queue"
SENT = BASE / "cmd" / "sent"
NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs, directory=str(BASE / "serve"))

    def do_POST(self):
        url = urlparse(self.path)
        size = int(self.headers.get("Content-Length", "-1"))
        if url.path != "/report" or not 0 <= size <= 1_000_000:
            self.send_error(400)
            return
        name = (parse_qs(url.query).get("name") or ["probe"])[0]
        if not NAME.match(name):
            self.send_error(400, "bad report name")
            return
        REPORTS.mkdir(exist_ok=True)
        body = self.rfile.read(size)
        (REPORTS / f"{name}.txt").write_bytes(body)
        print(f"report: {name}.txt {len(body)} bytes", flush=True)
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"OK")

    def do_GET(self):
        # The guest agent asks for one command at a time. The queue is a directory of shell
        # files on this Mac, so every command that reaches the device is on disk here first
        # and every answer lands in reports/agent-<name>.txt.
        if urlparse(self.path).path == "/agent/next":
            QUEUE.mkdir(parents=True, exist_ok=True)
            SENT.mkdir(parents=True, exist_ok=True)
            queued = sorted(p for p in QUEUE.glob("*.sh") if p.is_file())
            body = b"NO_COMMAND\n"
            if queued:
                source = queued[0]
                body = f"#ID {source.stem}\n".encode() + source.read_bytes()
                source.rename(SENT / source.name)
                print(f"agent: handed out {source.stem}", flush=True)
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        super().do_GET()

    def log_message(self, fmt, *args):
        pass


if __name__ == "__main__":
    ThreadingHTTPServer(("192.168.0.104", 8766), Handler).serve_forever()
