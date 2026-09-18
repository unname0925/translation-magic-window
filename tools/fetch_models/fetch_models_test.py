"""fetch_models.py 的測試：用本機的 HTTP 伺服器模擬下載來源，不需要網路。

執行：python -m unittest discover -s tools/fetch_models -p "*_test.py"
（ctest 也會執行，見 tests/CMakeLists.txt）
"""

from __future__ import annotations

import hashlib
import http.server
import json
import re
import tempfile
import threading
import unittest
from pathlib import Path

import fetch_models as fm


class _Handler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):  # 不要把每個請求都印出來
        pass

    def do_GET(self):
        self.server.requests.append(self.path)
        super().do_GET()


class LocalServer:
    """在背景執行緒提供 root 資料夾中的檔案，並記錄收到的請求。"""

    def __init__(self, root: Path):
        handler = lambda *a, **kw: _Handler(*a, directory=str(root), **kw)  # noqa: E731
        self.httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
        self.httpd.requests = []
        self.thread = threading.Thread(
            target=lambda: self.httpd.serve_forever(poll_interval=0.05), daemon=True)
        self.thread.start()

    @property
    def requests(self) -> list[str]:
        return self.httpd.requests

    def url(self, name: str) -> str:
        return f"http://127.0.0.1:{self.httpd.server_address[1]}/{name}"

    def close(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class FetchTestBase(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        base = Path(self._temp.name)
        self.served = base / "served"
        self.served.mkdir()
        self.models_dir = base / "models"
        self.server = LocalServer(self.served)
        self.logs: list[str] = []

    def tearDown(self):
        self.server.close()
        self._temp.cleanup()

    def serve(self, name: str, data: bytes) -> fm.ModelFile:
        (self.served / name).write_bytes(data)
        return fm.ModelFile(name, self.server.url(name), len(data), sha256(data))

    def run_fetch(self, models, **kwargs) -> list[str]:
        kwargs.setdefault("retry_delay", 0)
        return fm.fetch(models, self.models_dir, log=self.logs.append, **kwargs)

    def model(self, *files: fm.ModelFile, model_id="m1") -> fm.Model:
        return fm.Model(model_id, "ocr", "test", "MIT", "https://example.com", tuple(files))


class FetchTest(FetchTestBase):
    def test_downloads_and_verifies_files(self):
        a = self.serve("a.onnx", b"model-a" * 1000)
        b = self.serve("b.yml", b"dict: [x]")
        failures = self.run_fetch([self.model(a, b)])
        self.assertEqual(failures, [])
        self.assertEqual((self.models_dir / "m1" / "a.onnx").read_bytes(), b"model-a" * 1000)
        self.assertEqual((self.models_dir / "m1" / "b.yml").read_bytes(), b"dict: [x]")
        self.assertEqual(list((self.models_dir / "m1").glob("*.part")), [])

    def test_valid_existing_file_is_not_downloaded_again(self):
        a = self.serve("a.onnx", b"payload")
        self.assertEqual(self.run_fetch([self.model(a)]), [])
        self.server.requests.clear()
        self.assertEqual(self.run_fetch([self.model(a)]), [])
        self.assertEqual(self.server.requests, [])

    def test_corrupt_existing_file_is_replaced(self):
        a = self.serve("a.onnx", b"payload")
        target = self.models_dir / "m1" / "a.onnx"
        target.parent.mkdir(parents=True)
        target.write_bytes(b"paylo4d")  # 大小相同、內容不同
        self.assertEqual(self.run_fetch([self.model(a)]), [])
        self.assertEqual(target.read_bytes(), b"payload")
        self.assertEqual(len(self.server.requests), 1)

    def test_sha256_mismatch_fails_and_leaves_no_file(self):
        a = self.serve("a.onnx", b"payload")
        wrong = fm.ModelFile(a.name, a.url, a.size, sha256(b"something else"))
        failures = self.run_fetch([self.model(wrong)])
        self.assertEqual(len(failures), 1)
        self.assertIn("SHA-256 mismatch", failures[0])
        self.assertEqual(list(self.models_dir.rglob("*")), [self.models_dir / "m1"])
        # 內容錯誤不重試
        self.assertEqual(len(self.server.requests), 1)

    def test_size_mismatch_fails_and_leaves_no_file(self):
        a = self.serve("a.onnx", b"payload")
        too_big = fm.ModelFile(a.name, a.url, a.size + 5, a.sha256)
        too_small = fm.ModelFile("b.onnx", self.serve("b.onnx", b"payload-b").url, 3,
                                 sha256(b"pay"))
        failures = self.run_fetch([self.model(too_big, too_small)])
        self.assertEqual(len(failures), 2)
        self.assertIn("size mismatch", failures[0])
        self.assertIn("more than the expected", failures[1])
        self.assertFalse((self.models_dir / "m1" / "a.onnx").exists())
        self.assertFalse((self.models_dir / "m1" / "b.onnx").exists())
        self.assertEqual(list(self.models_dir.rglob("*.part")), [])

    def test_http_404_fails_without_retrying(self):
        missing = fm.ModelFile("gone.onnx", self.server.url("gone.onnx"), 10, sha256(b"x"))
        failures = self.run_fetch([self.model(missing)], attempts=3)
        self.assertEqual(len(failures), 1)
        self.assertIn("HTTP 404", failures[0])
        self.assertEqual(len(self.server.requests), 1)

    def test_other_files_continue_after_a_failure(self):
        missing = fm.ModelFile("gone.onnx", self.server.url("gone.onnx"), 10, sha256(b"x"))
        good = self.serve("good.onnx", b"good")
        failures = self.run_fetch([self.model(missing, good)])
        self.assertEqual(len(failures), 1)
        self.assertTrue((self.models_dir / "m1" / "good.onnx").exists())

    def test_verify_only_reports_missing_and_corrupt_without_downloading(self):
        a = self.serve("a.onnx", b"payload")
        b = self.serve("b.onnx", b"other")
        corrupt = self.models_dir / "m1" / "b.onnx"
        corrupt.parent.mkdir(parents=True)
        corrupt.write_bytes(b"wrong")
        failures = self.run_fetch([self.model(a, b)], verify_only=True)
        self.assertEqual(failures, ["m1/a.onnx: missing", "m1/b.onnx: corrupt"])
        self.assertEqual(self.server.requests, [])
        self.assertEqual(corrupt.read_bytes(), b"wrong")


class ManifestTest(unittest.TestCase):
    def setUp(self):
        self._temp = tempfile.TemporaryDirectory()
        self.path = Path(self._temp.name) / "models.json"

    def tearDown(self):
        self._temp.cleanup()

    def write(self, files, model_id="m1"):
        manifest = {"schemaVersion": 1, "models": [{
            "id": model_id, "group": "ocr", "purpose": "p", "license": "MIT",
            "source": "https://example.com", "files": files}]}
        self.path.write_text(json.dumps(manifest), encoding="utf-8")

    def good_file(self, **overrides):
        entry = {"name": "a.onnx", "url": "https://example.com/a.onnx", "size": 3,
                 "sha256": sha256(b"abc")}
        entry.update(overrides)
        return entry

    def test_valid_manifest_loads(self):
        self.write([self.good_file()])
        models = fm.load_manifest(self.path)
        self.assertEqual(models[0].files[0].name, "a.onnx")

    def test_file_names_cannot_escape_the_model_folder(self):
        for name in ["../a.onnx", "..", "sub/a.onnx", "sub\\a.onnx", "C:a.onnx", ".hidden", ""]:
            with self.subTest(name=name):
                self.write([self.good_file(name=name)])
                with self.assertRaises(fm.ManifestError):
                    fm.load_manifest(self.path)

    def test_model_ids_cannot_escape_the_models_folder(self):
        for model_id in ["../m1", "a/b", ""]:
            with self.subTest(model_id=model_id):
                self.write([self.good_file()], model_id=model_id)
                with self.assertRaises(fm.ManifestError):
                    fm.load_manifest(self.path)

    def test_remote_urls_must_use_https(self):
        self.write([self.good_file(url="http://example.com/a.onnx")])
        with self.assertRaises(fm.ManifestError):
            fm.load_manifest(self.path)

    def test_hash_and_size_must_be_well_formed(self):
        for overrides in [{"sha256": "ABC"}, {"sha256": sha256(b"x").upper()}, {"size": 0},
                          {"size": "3"}, {"size": True}]:
            with self.subTest(overrides=overrides):
                self.write([self.good_file(**overrides)])
                with self.assertRaises(fm.ManifestError):
                    fm.load_manifest(self.path)

    def test_unknown_selection_is_rejected(self):
        self.write([self.good_file()])
        models = fm.load_manifest(self.path)
        with self.assertRaises(fm.ManifestError):
            fm.select_models(models, ["nope"], None)
        with self.assertRaises(fm.ManifestError):
            fm.select_models(models, None, ["nope"])


class RealManifestTest(unittest.TestCase):
    """倉庫中的 models.json 本身也要檢查：格式正確，而且每個網址都固定在特定版本。"""

    def setUp(self):
        self.models = fm.load_manifest(fm.DEFAULT_MANIFEST)

    def test_every_url_is_pinned(self):
        for model in self.models:
            for f in model.files:
                with self.subTest(model=model.id, file=f.name):
                    if "huggingface.co" in f.url:
                        # 固定在 commit（40 位十六進位），不能用 main 這種會變動的分支
                        self.assertRegex(f.url, r"/resolve/[0-9a-f]{40}/")
                    else:
                        self.assertRegex(f.url, r"^https://github\.com/.+/releases/download/")

    def test_groups_cover_the_m0_evaluation(self):
        ids = {m.id for m in self.models}
        for required in ["PP-OCRv6_medium_det", "PP-OCRv6_medium_rec", "PP-OCRv5_server_det",
                         "PP-OCRv5_server_rec", "korean_PP-OCRv5_mobile_rec",
                         "comic-text-detector", "manga-ocr-base"]:
            self.assertIn(required, ids)

    def test_recognition_models_ship_their_dictionary(self):
        # 辨識模型的字元表在 inference.yml 裡，少了它就無法把輸出轉成文字
        for model in self.models:
            if re.search(r"_rec$", model.id):
                with self.subTest(model=model.id):
                    self.assertIn("inference.yml", [f.name for f in model.files])


if __name__ == "__main__":
    unittest.main()
