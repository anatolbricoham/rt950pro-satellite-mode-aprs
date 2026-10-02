"""
firmware.py - Published firmware images

Lists the firmware (.BTF) files attached to the project's GitHub releases
and downloads them to a local cache, so the desktop application can update
the radio without the user hunting for files. The web flasher uses the same
files, copied to the project site.
"""

from __future__ import annotations

import hashlib
import json
import os
import urllib.request
from dataclasses import dataclass
from typing import List, Optional

from . import APP_ID, GITHUB_REPO, __version__

API = "https://api.github.com/repos/%s/releases" % GITHUB_REPO
UA = {"User-Agent": "%s/%s" % (APP_ID, __version__), "Accept": "application/vnd.github+json"}


@dataclass
class FirmwareAsset:
    release: str          # tag, e.g. v0.3.0
    name: str             # file name
    url: str              # browser download URL
    size: int
    published: str
    prerelease: bool
    sha256: str = ""      # from <name>.sha256 when published

    @property
    def label(self) -> str:
        return "%s  %s  (%d KB)%s" % (self.release, self.name, self.size // 1024,
                                      "  [pre-release]" if self.prerelease else "")


def cache_dir() -> str:
    base = os.environ.get("APPDATA") or os.path.join(os.path.expanduser("~"), ".cache")
    d = os.path.join(base, APP_ID, "firmware")
    os.makedirs(d, exist_ok=True)
    return d


def _get(url: str, timeout: float = 20.0) -> bytes:
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def list_published(timeout: float = 20.0) -> List[FirmwareAsset]:
    releases = json.loads(_get(API, timeout))
    out: List[FirmwareAsset] = []
    for rel in releases:
        assets = {a["name"]: a for a in rel.get("assets", [])}
        for name, a in assets.items():
            if not name.upper().endswith(".BTF"):
                continue
            out.append(FirmwareAsset(rel["tag_name"], name, a["browser_download_url"], a["size"],
                                     rel.get("published_at", "")[:10], bool(rel.get("prerelease")),
                                     ""))
            sha = assets.get(name + ".sha256")
            if sha:
                try:
                    out[-1].sha256 = _get(sha["browser_download_url"], timeout).decode().split()[0]
                except Exception:
                    pass
    return out


def download(asset: FirmwareAsset, timeout: float = 60.0) -> str:
    path = os.path.join(cache_dir(), "%s-%s" % (asset.release, asset.name))
    if not os.path.exists(path) or os.path.getsize(path) != asset.size:
        data = _get(asset.url, timeout)
        if asset.sha256 and hashlib.sha256(data).hexdigest() != asset.sha256:
            raise IOError("downloaded firmware does not match its published SHA-256")
        with open(path, "wb") as f:
            f.write(data)
    return path


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()
