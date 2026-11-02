#!/usr/bin/env python3
"""Lokalny mirror Debiana po HTTP, który pobiera pliki z oficjalnych serwerów przez HTTPS.

Po co: w kontenerze Claude ruch wychodzi tylko przez proxy HTTPS (zwykłe HTTP nie działa),
a live-build/debootstrap najprościej działają z mirrorem http://. Ten skrypt wystawia
  http://127.0.0.1:<port>/debian/...           -> https://deb.debian.org/debian/...
  http://127.0.0.1:<port>/debian-security/...  -> https://security.debian.org/debian-security/...
Chroot dzieli sieć z hostem, więc mirror działa też w środku budowanego systemu.

Użycie: python3 scripts/mirror-proxy.py [port]   (domyślnie 3142)
"""
import os
import shutil
import ssl
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

UPSTREAMS = {
    "/debian-security/": "https://security.debian.org/debian-security/",
    "/debian/": "https://deb.debian.org/debian/",
}
CA = os.environ.get("KONS_PROXY_CA", "/root/.ccr/ca-bundle.crt")
ctx = ssl.create_default_context(cafile=CA if os.path.exists(CA) else None)
opener = urllib.request.build_opener(
    urllib.request.ProxyHandler(),  # https_proxy z otoczenia
    urllib.request.HTTPSHandler(context=ctx),
)
PASS_HEADERS = ("Content-Type", "Content-Length", "Last-Modified", "ETag")


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def _upstream(self):
        for prefix, base in UPSTREAMS.items():
            if self.path.startswith(prefix):
                return base + self.path[len(prefix):]
        return None

    def _forward(self, head_only):
        url = self._upstream()
        if not url:
            self.send_error(404, "unknown mirror path")
            return
        req = urllib.request.Request(url, method="HEAD" if head_only else "GET")
        for h in ("If-Modified-Since", "Range"):
            if self.headers.get(h):
                req.add_header(h, self.headers[h])
        try:
            resp = opener.open(req, timeout=120)
            status = resp.status
        except urllib.error.HTTPError as e:
            resp, status = e, e.code
        except Exception as e:  # sieć
            self.send_error(502, str(e)[:200])
            return
        with resp:
            self.send_response(status)
            for h in PASS_HEADERS:
                if resp.headers.get(h):
                    self.send_header(h, resp.headers[h])
            if not resp.headers.get("Content-Length"):
                self.send_header("Connection", "close")
                self.close_connection = True
            self.end_headers()
            if not head_only and status not in (304,):
                shutil.copyfileobj(resp, self.wfile, 1024 * 256)

    def do_GET(self):
        self._forward(False)

    def do_HEAD(self):
        self._forward(True)

    def log_message(self, fmt, *args):
        if os.environ.get("KONS_PROXY_VERBOSE"):
            super().log_message(fmt, *args)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 3142
    ThreadingHTTPServer.daemon_threads = True
    print(f"mirror-proxy: http://127.0.0.1:{port}/debian/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", port), Handler).serve_forever()
