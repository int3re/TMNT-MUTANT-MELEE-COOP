"""A stand-in for GitHub's release pages on this PC, for the launcher's tests.

    python tools/lab/serve_release.py <port> <folder with meleenet-latest.json and meleenet-launcher.exe> [<version>]

Serves /r/releases/latest/download/<file>, /r/releases/download/v<version>/<file>, and answers
/r/releases/latest with a redirect to /r/releases/tag/v<version>, as GitHub does. Writes each request to
requests.log in the folder."""
import http.server, json, os, sys

PORT, ROOT = int(sys.argv[1]), sys.argv[2]
VERSION = sys.argv[3] if len(sys.argv) > 3 else json.load(open(os.path.join(ROOT, "meleenet-latest.json"), encoding="utf-8"))["version"]


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        with open(os.path.join(ROOT, "requests.log"), "a") as f:
            f.write(self.path + "\n")
        p = self.path.split("?")[0]
        if p == "/r/releases/latest":
            self.send_response(302)
            self.send_header("Location", "/r/releases/tag/v" + VERSION)
            self.end_headers()
            return
        for prefix in ("/r/releases/latest/download/", "/r/releases/download/v%s/" % VERSION):
            if p.startswith(prefix):
                name = p[len(prefix):]
                path = os.path.join(ROOT, name)
                if "/" not in name and os.path.isfile(path):
                    data = open(path, "rb").read()
                    self.send_response(200)
                    self.send_header("Content-Type", "application/octet-stream")
                    self.send_header("Content-Length", str(len(data)))
                    self.end_headers()
                    self.wfile.write(data)
                    return
        if p.startswith("/r/releases/tag/"):
            body = b"<html><body>release</body></html>"
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(404)
        self.send_header("Content-Length", "0")
        self.end_headers()


http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
