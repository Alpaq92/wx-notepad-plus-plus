#!/usr/bin/env python3
"""Local preview server for site/, with caching fully disabled.

http.server sends no Cache-Control/ETag by default (only Last-Modified), so
browsers apply heuristic caching and can silently serve a stale copy on a
normal reload - every edit needs a hard refresh to actually show up. This
subclass adds Cache-Control: no-store to every response so edits are always
visible on the next reload, matching what you'd expect from a dev server.
"""
import functools
import http.server
import sys


class NoCacheHandler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, must-revalidate")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 4173
    directory = sys.argv[2] if len(sys.argv) > 2 else "site"
    handler = functools.partial(NoCacheHandler, directory=directory)
    with http.server.ThreadingHTTPServer(("", port), handler) as httpd:
        print(f"Serving {directory} on http://localhost:{port} (Cache-Control: no-store)")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
