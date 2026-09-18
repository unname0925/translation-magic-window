"""下載 models.json 列出的模型，並用檔案大小和 SHA-256 驗證（見 docs/design.md 4.4）。

用法：
    python tools/fetch_models/fetch_models.py                  # 下載全部
    python tools/fetch_models/fetch_models.py --group ocr       # 只下載某一組
    python tools/fetch_models/fetch_models.py --only PP-OCRv6_medium_det PP-OCRv6_medium_rec
    python tools/fetch_models/fetch_models.py --verify-only    # 只檢查，不下載
    python tools/fetch_models/fetch_models.py --list           # 列出模型和大小

- 已經存在而且驗證通過的檔案不會重新下載；內容不對的檔案會重新下載。
- 下載時先寫到 <檔名>.part，驗證通過才改成正式檔名，中途失敗不會留下壞掉的檔案。
- 所有網址都固定在特定的版本（Hugging Face 的 commit），不會因為上游更新而拿到不同的檔案。
- 只使用 Python 標準函式庫。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = Path(__file__).resolve().with_name("models.json")
DEFAULT_MODELS_DIR = REPO_ROOT / "models"

CHUNK_SIZE = 1024 * 1024
USER_AGENT = "translation-magic-window-fetch-models/1"

_SAFE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_LOCAL_HOSTS = {"127.0.0.1", "localhost", "::1"}


class ManifestError(ValueError):
    pass


class DownloadError(RuntimeError):
    pass


@dataclass(frozen=True)
class ModelFile:
    name: str
    url: str
    size: int
    sha256: str


@dataclass(frozen=True)
class Model:
    id: str
    group: str
    purpose: str
    license: str
    source: str
    files: tuple[ModelFile, ...]

    @property
    def size(self) -> int:
        return sum(f.size for f in self.files)


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise ManifestError(message)


def _check_url(url: str, where: str) -> None:
    parsed = urllib.parse.urlparse(url)
    # 遠端一律要 HTTPS；只有測試用的本機伺服器可以用 HTTP
    _require(
        parsed.scheme == "https" or (parsed.scheme == "http" and parsed.hostname in _LOCAL_HOSTS),
        f"{where}: url must use https: {url}",
    )


def load_manifest(path: Path) -> list[Model]:
    """讀取並檢查 models.json。格式不對時丟出 ManifestError。"""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(f"cannot read manifest {path}: {error}") from error

    _require(isinstance(data, dict) and data.get("schemaVersion") == 1,
             "manifest: schemaVersion must be 1")
    entries = data.get("models")
    _require(isinstance(entries, list) and entries, "manifest: models must be a non-empty list")

    models: list[Model] = []
    seen: set[str] = set()
    for entry in entries:
        _require(isinstance(entry, dict), "manifest: each model must be an object")
        model_id = entry.get("id")
        _require(isinstance(model_id, str) and bool(_SAFE_NAME.match(model_id)),
                 f"manifest: invalid model id {model_id!r}")
        _require(model_id not in seen, f"manifest: duplicate model id {model_id}")
        seen.add(model_id)
        for key in ("group", "purpose", "license", "source"):
            _require(isinstance(entry.get(key), str) and entry[key] != "",
                     f"{model_id}: missing {key}")
        raw_files = entry.get("files")
        _require(isinstance(raw_files, list) and raw_files, f"{model_id}: files must be a non-empty list")

        files: list[ModelFile] = []
        names: set[str] = set()
        for raw in raw_files:
            _require(isinstance(raw, dict), f"{model_id}: each file must be an object")
            name = raw.get("name")
            where = f"{model_id}/{name}"
            # 檔名不能包含路徑，避免寫到 models 資料夾以外的地方
            _require(isinstance(name, str) and bool(_SAFE_NAME.match(name)) and ".." not in name,
                     f"{model_id}: invalid file name {name!r}")
            _require(name not in names, f"{where}: duplicate file name")
            names.add(name)
            url = raw.get("url")
            _require(isinstance(url, str), f"{where}: missing url")
            _check_url(url, where)
            size = raw.get("size")
            _require(isinstance(size, int) and not isinstance(size, bool) and size > 0,
                     f"{where}: size must be a positive integer")
            sha256 = raw.get("sha256")
            _require(isinstance(sha256, str) and bool(_SHA256.match(sha256)),
                     f"{where}: sha256 must be 64 lowercase hex digits")
            files.append(ModelFile(name, url, size, sha256))

        models.append(Model(model_id, entry["group"], entry["purpose"], entry["license"],
                            entry["source"], tuple(files)))
    return models


def select_models(models: list[Model], only: list[str] | None,
                  groups: list[str] | None) -> list[Model]:
    """依照 --only 和 --group 挑出要處理的模型。名稱不認得時丟出 ManifestError。"""
    known_ids = {m.id for m in models}
    known_groups = {m.group for m in models}
    for model_id in only or []:
        _require(model_id in known_ids, f"unknown model id: {model_id}")
    for group in groups or []:
        _require(group in known_groups, f"unknown group: {group}")
    if not only and not groups:
        return list(models)
    return [m for m in models if (only and m.id in only) or (groups and m.group in groups)]


def file_matches(path: Path, expected: ModelFile) -> bool:
    """檔案存在，而且大小和 SHA-256 都正確。"""
    try:
        if path.stat().st_size != expected.size:
            return False
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(CHUNK_SIZE), b""):
                digest.update(chunk)
        return digest.hexdigest() == expected.sha256
    except FileNotFoundError:
        return False


def _download_once(expected: ModelFile, target: Path, log) -> None:
    partial = target.with_name(target.name + ".part")
    digest = hashlib.sha256()
    received = 0
    next_report = 0.1
    request = urllib.request.Request(expected.url, headers={"User-Agent": USER_AGENT})
    try:
        with urllib.request.urlopen(request, timeout=60) as response, partial.open("wb") as out:
            while True:
                chunk = response.read(CHUNK_SIZE)
                if not chunk:
                    break
                received += len(chunk)
                if received > expected.size:
                    raise DownloadError(
                        f"{expected.name}: server sent more than the expected {expected.size} bytes")
                digest.update(chunk)
                out.write(chunk)
                if expected.size >= 10 * CHUNK_SIZE and received >= next_report * expected.size:
                    log(f"    {int(next_report * 100)}%")
                    next_report += 0.1
        if received != expected.size:
            raise DownloadError(
                f"{expected.name}: size mismatch (expected {expected.size}, got {received})")
        if digest.hexdigest() != expected.sha256:
            raise DownloadError(
                f"{expected.name}: SHA-256 mismatch (expected {expected.sha256}, "
                f"got {digest.hexdigest()})")
        os.replace(partial, target)
    finally:
        # 驗證失敗或中途出錯時，不留下不完整的檔案
        if partial.exists():
            partial.unlink()


def download_file(expected: ModelFile, target: Path, *, attempts: int = 3,
                  retry_delay: float = 2.0, log=print) -> None:
    """下載並驗證一個檔案。網路錯誤會重試；內容驗證失敗不重試（重試也拿不到正確的檔案）。"""
    target.parent.mkdir(parents=True, exist_ok=True)
    for attempt in range(1, attempts + 1):
        try:
            _download_once(expected, target, log)
            return
        except urllib.error.HTTPError as error:
            error.close()
            # 4xx（例如 404）重試也不會成功；5xx 可能是暫時的
            if 400 <= error.code < 500 or attempt == attempts:
                raise DownloadError(f"{expected.name}: HTTP {error.code} from {expected.url}") from error
            log(f"    HTTP {error.code}; retrying ({attempt}/{attempts - 1})")
            time.sleep(retry_delay)
        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            if attempt == attempts:
                raise DownloadError(f"{expected.name}: download failed: {error}") from error
            log(f"    network error ({error}); retrying ({attempt}/{attempts - 1})")
            time.sleep(retry_delay)


def fetch(models: list[Model], models_dir: Path, *, verify_only: bool = False,
          attempts: int = 3, retry_delay: float = 2.0, log=print) -> list[str]:
    """處理所有模型，回傳失敗的項目（空清單代表全部成功）。"""
    failures: list[str] = []
    for model in models:
        log(f"{model.id} ({model.size / 1e6:.1f} MB, {model.license})")
        for expected in model.files:
            target = models_dir / model.id / expected.name
            if file_matches(target, expected):
                log(f"  ok        {expected.name}")
                continue
            if verify_only:
                state = "corrupt" if target.exists() else "missing"
                log(f"  {state:9} {expected.name}")
                failures.append(f"{model.id}/{expected.name}: {state}")
                continue
            log(f"  download  {expected.name} ({expected.size / 1e6:.1f} MB)")
            try:
                download_file(expected, target, attempts=attempts, retry_delay=retry_delay, log=log)
                log(f"  ok        {expected.name}")
            except (DownloadError, OSError) as error:
                log(f"  FAILED    {error}")
                failures.append(f"{model.id}/{expected.name}: {error}")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Download and verify the models in models.json.")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--models-dir", type=Path, default=DEFAULT_MODELS_DIR)
    parser.add_argument("--only", nargs="+", metavar="ID", help="only these model ids")
    parser.add_argument("--group", nargs="+", metavar="GROUP", help="only these groups")
    parser.add_argument("--verify-only", action="store_true", help="check files without downloading")
    parser.add_argument("--list", action="store_true", help="list models and exit")
    args = parser.parse_args(argv)

    try:
        models = select_models(load_manifest(args.manifest), args.only, args.group)
    except ManifestError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    total = sum(m.size for m in models)
    if args.list:
        for model in models:
            print(f"{model.id:28} {model.group:6} {model.size / 1e6:8.1f} MB  {model.license:11} "
                  f"{model.purpose}")
        print(f"{len(models)} models, {total / 1e6:.1f} MB")
        return 0

    print(f"{len(models)} models, {total / 1e6:.1f} MB -> {args.models_dir}")
    failures = fetch(models, args.models_dir, verify_only=args.verify_only)
    if failures:
        print(f"\n{len(failures)} file(s) failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("\nall files verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
