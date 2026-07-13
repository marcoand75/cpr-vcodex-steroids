#!/usr/bin/env python3
"""Fetch KOReader mdlight and src SVG icons into a local cache directory.

Usage:
    python scripts/fetch_koreader_icons.py [--repo koreader/koreader] [--branch master] [--cache-dir build/koreader_icons]

The script queries the GitHub Contents API for resources/icons/mdlight and
resources/icons/src, downloads every .svg file, and stores it under:

    <cache-dir>/mdlight/<name>.svg
    <cache-dir>/src/<name>.svg
"""

import argparse
import json
import os
import sys
import urllib.request
import urllib.error


def gh_list(repo, branch, path):
    url = f"https://api.github.com/repos/{repo}/contents/{path}?ref={branch}"
    req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json", "User-Agent": "cpr-icon-script"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read())


def gh_download(download_url, dest_path):
    req = urllib.request.Request(download_url, headers={"User-Agent": "cpr-icon-script"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read()
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    with open(dest_path, "wb") as f:
        f.write(data)


def fetch(repo, branch, cache_dir):
    mdlight_dir = os.path.join(cache_dir, "mdlight")
    src_dir = os.path.join(cache_dir, "src")

    for subdir, api_path in [("mdlight", "resources/icons/mdlight"), ("src", "resources/icons/src")]:
        out_dir = mdlight_dir if subdir == "mdlight" else src_dir
        print(f"Fetching {api_path} ...")
        try:
            items = gh_list(repo, branch, api_path)
        except urllib.error.HTTPError as e:
            print(f"  ERROR listing {api_path}: {e}")
            continue

        count = 0
        for item in items:
            if item["type"] != "file" or not item["name"].endswith(".svg"):
                continue
            dest = os.path.join(out_dir, item["name"])
            if os.path.exists(dest):
                count += 1
                continue
            try:
                gh_download(item["download_url"], dest)
                count += 1
            except Exception as e:
                print(f"  WARNING: failed to download {item['name']}: {e}")
        print(f"  -> {count} SVGs cached under {out_dir}")


def main():
    parser = argparse.ArgumentParser(description="Fetch KOReader icons into a local cache")
    parser.add_argument("--repo", default="koreader/koreader", help="GitHub repo (default: koreader/koreader)")
    parser.add_argument("--branch", default="master", help="Branch (default: master)")
    parser.add_argument("--cache-dir", default=os.path.join("build", "koreader_icons"), help="Cache directory")
    args = parser.parse_args()

    fetch(args.repo, args.branch, args.cache_dir)
    print("Done.")


if __name__ == "__main__":
    main()
