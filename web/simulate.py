"""Local UI simulation only: python web/simulate.py (no Pi, Bluetooth or keys)."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from threading import Lock
from time import monotonic
from urllib.parse import parse_qs
import argparse
import json

ROOT = Path(__file__).resolve().parent
CANDIDATE = "aabb0011223344556677889900000000"


class Simulation:
    def __init__(self):
        self.lock = Lock()
        self.state = {"simulation": True, "ready": True, "brightness": 50, "rotation": 3,
                      "wifi": {"connected": True, "ip": "127.0.0.1", "error": ""},
                      "operation": {"id": 0, "state": 0, "error": 0, "flags": 0, "scanSeconds": 0},
                      "nodes": [], "candidates": [], "menu": [{"type": i, "name": name, "address": 0} for i, name in enumerate(("climate", "soil", "lights", "plug", "blinds", "thermo"))]}
        self.scan_end = self.finish_at = 0
        self.action = None
        self.next_address = 256

    def tick(self):
        now = monotonic()
        self.state["operation"]["scanSeconds"] = max(0, int(self.scan_end - now + .999))
        if self.scan_end and now >= self.scan_end:
            self.scan_end = 0
        if self.finish_at and now >= self.finish_at:
            self.finish_at = 0
            path, fields = self.action
            if path == "/api/bind":
                self.state["nodes"].append({"address": self.next_address, "uuid": fields["uuid"], "type": 1,
                                            "configured": True, "fresh": True, "removing": False})
                self.next_address += 1
                self.state["candidates"] = []
            elif path in ("/api/remove", "/api/forget"):
                self.state["nodes"] = [n for n in self.state["nodes"] if n["address"] != int(fields["address"])]
                for entry in self.state["menu"]:
                    if entry["address"] == int(fields["address"]): entry["address"] = 0
            elif path == "/api/retry":
                for node in self.state["nodes"]:
                    if node["address"] == int(fields["address"]):
                        node["configured"] = True
            self.state["operation"].update(state=6, flags=24 if path == "/api/remove" else 0)

    def request(self, path, fields):
        self.tick()
        if path == "/api/menu":
            count = int(fields["count"])
            if not 0 <= count <= 16 or len(fields) != 1 + count * 3: raise ValueError()
            entries = []
            for i in range(count):
                kind, name, address = int(fields[f"type{i}"]), fields[f"name{i}"], int(fields[f"address{i}"])
                if not 0 <= kind <= 5 or not 1 <= len(name) <= 16 or any(ch not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-." for ch in name): raise ValueError()
                if address and (kind != 0 or not any(n["address"] == address and n["type"] == 1 and not n["removing"] for n in self.state["nodes"])): raise ValueError()
                entries.append({"type": kind, "name": name, "address": address})
            self.state["menu"] = entries
            return 200, {"accepted": True}
        if path == "/api/display":
            if len(fields) != 1:
                return 400, {"error": "Write one display setting at a time"}
            key = next(iter(fields))
            if key not in ("brightness", "rotation"):
                return 400, {"error": "Invalid setting"}
            value = int(fields[key])
            if not (1 <= value <= 4 if key == "rotation" else 0 <= value <= 100):
                return 400, {"error": "Invalid setting value"}
            self.state[key] = value
            return 200, {"accepted": True}
        if path not in ("/api/scan", "/api/bind", "/api/remove", "/api/forget", "/api/retry"):
            return 404, {"error": "Unknown action"}
        request_id = int(fields["id"])
        if not 1 <= request_id <= 65535:
            return 400, {"error": "Invalid request ID"}
        if self.finish_at:
            return 409, {"error": "Hub busy", "code": 128}
        if path == "/api/bind" and fields["uuid"] not in self.state["candidates"]:
            return 409, {"error": "Node not found"}
        if path in ("/api/remove", "/api/forget", "/api/retry") and not any(n["address"] == int(fields["address"]) for n in self.state["nodes"]):
            return 409, {"error": "Node not found"}
        self.state["operation"].update(id=request_id, state=6, error=0, flags=0)
        if path == "/api/scan":
            self.scan_end = monotonic() + 15
            self.state["candidates"] = [] if self.state["nodes"] else [CANDIDATE]
        else:
            self.action = path, fields
            self.finish_at = monotonic() + 1
            self.state["operation"].update(state={"/api/bind": 2, "/api/remove": 4, "/api/forget": 5, "/api/retry": 3}[path], flags=64)
        return 200, {"accepted": True}


class Handler(BaseHTTPRequestHandler):
    simulation = Simulation()

    def reply(self, status, body, content_type="application/json"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; object-src 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path == "/api/state":
            with self.simulation.lock:
                self.simulation.tick()
                self.reply(200, self.simulation.state)
            return
        asset = {"/": ("index.html", "text/html; charset=utf-8"), "/index.html": ("index.html", "text/html; charset=utf-8"),
                 "/app.js": ("app.js", "text/javascript; charset=utf-8"), "/style.css": ("style.css", "text/css; charset=utf-8")}.get(self.path)
        if asset:
            self.reply(200, (ROOT / asset[0]).read_bytes(), asset[1])
        else:
            self.reply(404, {})

    def do_POST(self):
        if self.headers.get("X-SR-Request") != "1" or self.headers.get("Content-Type") != "application/x-www-form-urlencoded":
            self.reply(403, {"error": "Same-origin request required"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 <= length <= 2048:
                raise ValueError()
            raw = parse_qs(self.rfile.read(length).decode(), keep_blank_values=True, strict_parsing=True)
            if any(len(values) != 1 for values in raw.values()):
                raise ValueError()
            fields = {key: values[0] for key, values in raw.items()}
            with self.simulation.lock:
                status, result = self.simulation.request(self.path, fields)
            self.reply(status, result)
        except (ValueError, KeyError, UnicodeError):
            self.reply(400, {"error": "Invalid request"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"Smart Rabbit SIMULATION: http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
