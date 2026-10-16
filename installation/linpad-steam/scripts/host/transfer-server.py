from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

base = Path(__file__).resolve().parent
logs = base / 'device-logs'
logs.mkdir(exist_ok=True)
allowed = {'steam-access.log', 'steam-debug.log', 'steamwebhelper.log', 'cef_log.txt', 'bootstrap_log.txt', 'console-linux.txt', 'fex.log'}

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(base / 'serve'), **kwargs)

    def do_POST(self):
        name = self.path.removeprefix('/reports/')
        size = int(self.headers.get('Content-Length', '-1'))
        if not self.path.startswith('/reports/') or name not in allowed or not 0 <= size <= 2_000_000:
            self.send_error(400)
            return
        (logs / name).write_bytes(self.rfile.read(size))
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'OK\n')

ThreadingHTTPServer(('192.168.0.104', 8765), Handler).serve_forever()
