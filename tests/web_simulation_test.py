"""HTTP contract checks for the local simulator; never access hardware."""
import importlib.util
import json
from pathlib import Path
from threading import Thread
from time import monotonic
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import Request, urlopen

spec = importlib.util.spec_from_file_location("simulate", Path(__file__).resolve().parents[1] / "web" / "simulate.py")
simulate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(simulate)


class WebSimulationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = simulate.ThreadingHTTPServer(("127.0.0.1", 0), simulate.Handler)
        cls.thread = Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f"http://127.0.0.1:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def setUp(self):
        simulate.Handler.simulation = simulate.Simulation()

    def request(self, path, fields=None, authorized=True):
        headers = {"Content-Type": "application/x-www-form-urlencoded"}
        if authorized:
            headers["X-SR-Request"] = "1"
        request = Request(self.url + path, data=None if fields is None else urlencode(fields).encode(), headers=headers)
        try:
            response = urlopen(request, timeout=3)
        except HTTPError as error:
            response = error
        with response:
            data = response.read()
            return response.status, json.loads(data) if response.headers.get_content_type() == "application/json" else data

    def finish(self):
        with simulate.Handler.simulation.lock:
            simulate.Handler.simulation.finish_at = monotonic() - 1

    def test_assets_and_settings(self):
        for path in ("/", "/app.js", "/style.css"):
            self.assertEqual(self.request(path)[0], 200)
        self.assertEqual(self.request("/../README.md")[0], 404)
        self.assertEqual(self.request("/api/display", {"brightness": 0})[0], 200)
        self.assertEqual(self.request("/api/display", {"rotation": 4})[0], 200)
        state = self.request("/api/state")[1]
        self.assertEqual((state["brightness"], state["rotation"]), (0, 4))
        for value in (51, 100):
            self.assertEqual(self.request("/api/display", {"brightness": value})[0], 200)
            self.assertEqual(self.request("/api/state")[1]["brightness"], value)
        for value in (-1, 101):
            self.assertEqual(self.request("/api/display", {"brightness": value})[0], 400)
        self.assertEqual(self.request("/api/display", {"rotation": 0})[0], 400)
        self.assertEqual(self.request("/api/display", {"brightness": 20}, authorized=False)[0], 403)

    def test_menu_edit_assignment_and_forget(self):
        fields = {"count": 2, "type0": 2, "name0": "Desk", "address0": 0,
                  "type1": 0, "name1": "Room", "address1": 0}
        self.assertEqual(self.request("/api/menu", fields)[0], 200)
        self.assertEqual([e["name"] for e in self.request("/api/state")[1]["menu"]], ["Desk", "Room"])
        fields["address1"] = 256
        self.assertEqual(self.request("/api/menu", fields)[0], 400)
        self.request("/api/scan", {"id": 1})
        self.request("/api/bind", {"id": 2, "uuid": simulate.CANDIDATE})
        self.finish()
        self.assertEqual(self.request("/api/menu", fields)[0], 200)
        self.assertEqual(self.request("/api/forget", {"id": 3, "address": 256})[0], 200)
        self.finish()
        state = self.request("/api/state")[1]
        self.assertEqual(state["nodes"], [])
        self.assertEqual(state["menu"][1]["address"], 0)
        self.assertEqual(self.request("/api/menu", {"count": 0})[0], 200)
        self.assertEqual(self.request("/api/state")[1]["menu"], [])

    def test_scan_bind_and_remove_are_asynchronous(self):
        self.assertEqual(self.request("/api/scan", {"id": 1})[0], 200)
        state = self.request("/api/state")[1]
        self.assertEqual(state["nodes"], [])
        uuid = state["candidates"][0]
        self.assertEqual(self.request("/api/bind", {"id": 2, "uuid": uuid})[0], 200)
        self.assertEqual(self.request("/api/state")[1]["operation"]["state"], 2)
        self.assertEqual(self.request("/api/scan", {"id": 3})[0], 409)
        self.finish()
        node = self.request("/api/state")[1]["nodes"][0]
        self.assertEqual(self.request("/api/remove", {"id": 4, "address": node["address"]})[0], 200)
        self.assertEqual(len(self.request("/api/state")[1]["nodes"]), 1)
        self.finish()
        state = self.request("/api/state")[1]
        self.assertEqual(state["nodes"], [])
        self.assertEqual(state["operation"]["flags"], 24)


if __name__ == "__main__":
    unittest.main()
