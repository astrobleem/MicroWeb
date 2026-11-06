#!/usr/bin/env python3
"""Verify a carl-style plaintext HTTP proxy contract, not upstream TLS."""
import http.server
import subprocess
import threading

requests = []
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def handle_request(self):
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        requests.append((self.command, self.path, self.headers.get('Host'), body))
        if self.path.startswith('https://origin.invalid:8443/start'):
            self.send_response(302); self.send_header('Location', '//other.invalid/final?q=ok'); payload = b''
        elif self.path == 'https://origin.invalid/post':
            self.send_response(307); self.send_header('Location', '/accepted'); payload = b''
        elif self.path == 'https://origin.invalid/downgrade':
            self.send_response(307); self.send_header('Location', 'http://origin.invalid/plain'); payload = b''
        else:
            self.send_response(200); payload = b'gateway response'
        self.send_header('Content-Length', str(len(payload))); self.end_headers(); self.wfile.write(payload)
    do_GET = handle_request
    do_POST = handle_request

with http.server.HTTPServer(('127.0.0.1', 0), Handler) as server:
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    try:
        subprocess.run(['build/tests/https-proxy', str(server.server_port)], check=True, timeout=30)
    finally:
        server.shutdown(); thread.join()
body = b'a=1%26b%3D2'
assert requests == [
    ('GET', 'https://origin.invalid:8443/start?q=hello%20world', 'origin.invalid:8443', b''),
    ('GET', 'https://other.invalid/final?q=ok', 'other.invalid', b''),
    ('POST', 'https://origin.invalid/post', 'origin.invalid', body),
    ('POST', 'https://origin.invalid/accepted', 'origin.invalid', body),
    ('POST', 'https://origin.invalid/downgrade', 'origin.invalid', body)]
print('PASS: gateway captured exact absolute HTTPS request lines, origin Host, methods and bodies; no HTTP downgrade request')
