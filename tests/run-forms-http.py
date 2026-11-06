#!/usr/bin/env python3
"""Local TCP integration of production form serialization and HTTP handling."""
import http.server
import subprocess
import threading

requests = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def handle_request(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        requests.append((self.command, self.path, body, self.headers.get('Content-Type')))
        if self.path.startswith('/start1'):
            self.send_response(307); self.send_header('Location', '/accepted1')
            payload = b''
        elif self.path.startswith('/start2'):
            self.send_response(303); self.send_header('Location', '/accepted2')
            payload = b''
        else:
            self.send_response(200); payload = b'accepted'
        self.send_header('Content-Length', str(len(payload))); self.end_headers()
        self.wfile.write(payload)
    do_GET = handle_request
    do_POST = handle_request

with http.server.HTTPServer(('127.0.0.1', 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        subprocess.run(['build/tests/forms-http', str(server.server_port)], check=True, timeout=30)
    finally:
        server.shutdown(); thread.join()
expected = b'token=a%26b%3D%2B%25+space'
assert [(m,p,b) for m,p,b,_ in requests] == [
    ('GET', '/start0?' + expected.decode(), b''),
    ('POST', '/start1?old=1', b'v=' + b'x'*509),
    ('POST', '/accepted1', b'v=' + b'x'*509),
    ('POST', '/start2?old=1', expected),
    ('GET', '/accepted2', b'')]
assert all(c == 'application/x-www-form-urlencoded' for m,_,_,c in requests if m == 'POST')
print('PASS: server verified exact methods, paths, payloads and POST content types')
