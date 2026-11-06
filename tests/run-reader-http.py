#!/usr/bin/env python3
"""Check the production reader link's parameter reaches a local HTTP service intact."""
import http.server
import subprocess
import threading
from urllib.parse import urlsplit, parse_qs

requests = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        requests.append((self.command, self.path, self.headers.get('Host')))
        self.send_response(200)
        self.send_header('Content-Length', '8')
        self.end_headers()
        self.wfile.write(b'accepted')

with http.server.HTTPServer(('127.0.0.1', 0), Handler) as server:
    port = server.server_port
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        subprocess.run(['build/tests/reader-http', str(port)], check=True, timeout=30)
    finally:
        server.shutdown()
        thread.join()
assert len(requests) == 1
method, target, host = requests[0]
assert method == 'GET' and host == f'127.0.0.1:{port}'
url = urlsplit(target)
assert url.path == '/read.php' and not url.fragment
assert parse_qs(url.query) == {'lg': ['en-us'], 'a': ['https://example.test/a%20b?q=x&n=two+words#section']}
print('PASS: local reader received the exact HTTPS destination, query, escaping, plus sign and fragment')
