#!/usr/bin/env python3
# WEB PORT test server: serves build/ with no-cache headers so mapping.js /
# Scripts.rxdata / rgss.rb reloads always fetch fresh (the browser's heuristic
# caching otherwise serves a stale mapping.js and ignores our ?h= hash bumps).
import http.server, socketserver, sys, os

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8124
DIRECTORY = sys.argv[2] if len(sys.argv) > 2 else "mkxp-web/build"

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k):
        super().__init__(*a, directory=DIRECTORY, **k)
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        super().end_headers()
    def log_message(self, *a):
        pass

socketserver.TCPServer.allow_reuse_address = True
with socketserver.TCPServer(("", PORT), Handler) as httpd:
    print(f"no-cache server on {PORT} serving {DIRECTORY}")
    httpd.serve_forever()
