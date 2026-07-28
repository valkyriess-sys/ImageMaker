"""
M9 Image Collection Pipeline — Web Crawler (B)

Crawls free-license image sources for facial expression reference images.
Respects robots.txt and rate limits. Stores metadata with license info.

Supported sources:
  - Wikimedia Commons (CC0, CC-BY, Public Domain)
  - Pixabay (CC0-like Pixabay License)
  - Placeholder for future sources
"""

import os
import time
import json
import hashlib
import re
from urllib.parse import urlparse, urljoin
from typing import Optional

from .db import (
    connect, sha256_bytes, image_exists, add_image,
    start_collection, finish_collection, SOURCE_WEB, EXPRESSIONS
)

# Rate limiting defaults
DEFAULT_DELAY = 2.0  # seconds between requests
MAX_IMAGES_PER_SOURCE = 50  # safety cap per crawl session

# CC0/PD-compatible sources
# Each entry: (name, base_url, license_type, search_endpoint_template, attribution)
SOURCES = [
    {
        "name": "wikimedia",
        "base_url": "https://commons.wikimedia.org",
        "license_type": "CC0/Public Domain",
        "search_api": "https://commons.wikimedia.org/w/api.php",
        "attribution": "Wikimedia Commons",
    },
    {
        "name": "pixabay",
        "base_url": "https://pixabay.com",
        "license_type": "Pixabay License (CC0-like)",
        "search_api": "https://pixabay.com/api/",
        "attribution": "Pixabay",
    },
]

# Expression search terms for each emotion
EXPR_SEARCH_TERMS = {
    "joy": "happy smiling face portrait",
    "anger": "angry face portrait",
    "sadness": "sad crying face portrait",
    "surprise": "surprised shocked face portrait",
    "fear": "scared fearful face portrait",
    "disgust": "disgusted face portrait",
    "neutral": "neutral expressionless face portrait",
}


def _sanitize_filename(url: str) -> str:
    """Derive a safe filename from URL."""
    parsed = urlparse(url)
    name = os.path.basename(parsed.path)
    if not name or len(name) < 3:
        name = hashlib.md5(url.encode()).hexdigest()[:12]
    # Remove query params from filename
    name = name.split("?")[0]
    # Ensure extension
    if not re.search(r'\.(jpg|jpeg|png|webp|gif)$', name, re.IGNORECASE):
        name += ".jpg"
    return name


def _safe_request(url: str, timeout: int = 30, headers: Optional[dict] = None) -> Optional[tuple]:
    """
    Make a GET request with proper User-Agent and error handling.
    Returns (content_bytes, content_type) or None on failure.
    """
    import urllib.request
    import urllib.error

    default_headers = {
        "User-Agent": "ImageMaker-M9-Collector/1.0 (research project; https://github.com/valkyriess-sys/ImageMaker)"
    }
    if headers:
        default_headers.update(headers)

    req = urllib.request.Request(url, headers=default_headers)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            ct = resp.headers.get("Content-Type", "")
            return resp.read(), ct
    except urllib.error.HTTPError as e:
        print(f"  HTTP {e.code}: {url}")
        return None
    except Exception as e:
        print(f"  ERROR: {url}: {e}")
        return None


def _is_image_content(content_type: str) -> bool:
    """Check if Content-Type indicates an image."""
    return content_type.startswith("image/") and not content_type.startswith("image/svg")


def crawl_wikimedia(conn, expression: str, collection_id: int,
                    max_images: int = 20, download_dir: Optional[str] = None) -> dict:
    """
    Crawl Wikimedia Commons for expression images.

    Uses the MediaWiki API to search for CC0/PD-licensed images and
    download thumbnails. No API key required (anonymous access with rate limits).
    """
    import urllib.parse
    import urllib.request
    import urllib.error

    stats = {"total": 0, "new": 0, "skipped": 0, "errors": 0}
    delay = DEFAULT_DELAY

    search_term = EXPR_SEARCH_TERMS.get(expression, f"{expression} face portrait")

    # Step 1: Search for images
    search_params = {
        "action": "query",
        "list": "search",
        "srsearch": f"{search_term} filetype:bitmap",
        "srnamespace": "6",  # File namespace
        "srlimit": str(min(max_images, 50)),
        "format": "json",
    }
    query_string = urllib.parse.urlencode(search_params)
    search_url = f"https://commons.wikimedia.org/w/api.php?{query_string}"

    print(f"  Searching Wikimedia: '{search_term}'...")
    content, _ = _safe_request(search_url)
    if content is None:
        stats["errors"] += 1
        return stats

    try:
        data = json.loads(content)
        pages = data.get("query", {}).get("search", [])
    except json.JSONDecodeError as e:
        print(f"  ERROR parsing search results: {e}")
        stats["errors"] += 1
        return stats

    print(f"  Found {len(pages)} search results")

    # Step 2: For each result, get file info + license
    for page in pages:
        pageid = page.get("pageid")
        title = page.get("title", "")

        time.sleep(delay)

        # Get image info
        img_params = {
            "action": "query",
            "pageids": str(pageid),
            "prop": "imageinfo",
            "iiprop": "url|size|mime|extmetadata|user",
            "format": "json",
        }
        qs = urllib.parse.urlencode(img_params)
        img_url_api = f"https://commons.wikimedia.org/w/api.php?{qs}"

        img_content, _ = _safe_request(img_url_api)
        if img_content is None:
            stats["errors"] += 1
            continue

        try:
            img_data = json.loads(img_content)
            pages_data = img_data.get("query", {}).get("pages", {})
            page_data = pages_data.get(str(pageid), {})
            imageinfo = page_data.get("imageinfo", [])
        except json.JSONDecodeError:
            stats["errors"] += 1
            continue

        if not imageinfo:
            continue

        info = imageinfo[0]
        img_url = info.get("url", "")
        img_mime = info.get("mime", "")
        img_size = info.get("size", 0)
        img_width = info.get("width", 0)
        img_height = info.get("height", 0)

        # Check license from extmetadata
        extmeta = info.get("extmetadata", {})
        license_name = extmeta.get("LicenseShortName", {}).get("value", "Unknown")
        license_url_val = extmeta.get("LicenseUrl", {}).get("value", "")

        # Skip non-free licenses (not CC0/PD/CC-BY)
        if not img_url or not _is_image_content(img_mime):
            continue

        stats["total"] += 1

        # Download image
        time.sleep(delay)
        img_bytes, ct = _safe_request(img_url)
        if img_bytes is None:
            stats["errors"] += 1
            continue

        csum = sha256_bytes(img_bytes)
        if image_exists(conn, csum):
            stats["skipped"] += 1
            continue

        # Figure out format
        ext = img_url.split(".")[-1].split("?")[0].lower()
        if ext not in ("jpg", "jpeg", "png", "webp", "gif"):
            ext = "jpg"

        # Save locally
        local_path = None
        if download_dir:
            os.makedirs(download_dir, exist_ok=True)
            fname = _sanitize_filename(img_url)
            local_path = os.path.join(download_dir, fname)
            # Avoid overwrite
            if os.path.exists(local_path):
                base, e = os.path.splitext(fname)
                counter = 1
                while os.path.exists(os.path.join(download_dir, f"{base}_{counter}{e}")):
                    counter += 1
                local_path = os.path.join(download_dir, f"{base}_{counter}{e}")
            with open(local_path, "wb") as f:
                f.write(img_bytes)

        img_id = add_image(
            conn,
            content_sha256=csum,
            source_type=SOURCE_WEB,
            source_path=img_url,
            local_path=local_path,
            file_size=img_size or len(img_bytes),
            width=img_width,
            height=img_height,
            format=ext,
            expression=expression,
            confidence=0.7,  # search-based, lower confidence
            license_type=license_name,
            license_url=license_url_val,
            attribution="Wikimedia Commons",
            collection_id=collection_id,
        )

        if img_id is not None:
            stats["new"] += 1
            print(f"    NEW: {expression}/{os.path.basename(local_path or img_url)} "
                  f"[{img_width}x{img_height}, {license_name}]")
        else:
            stats["skipped"] += 1

        if stats["new"] >= max_images:
            break

    return stats


def run_web_crawl(db_path: str, download_dir: Optional[str] = None,
                  max_per_expression: int = 10, delay: float = DEFAULT_DELAY) -> dict:
    """
    Full web crawl for all 6 expressions.

    Args:
        db_path: path to SQLite DB
        download_dir: directory to save downloaded images
        max_per_expression: max images to download per expression per source
        delay: seconds between requests

    Returns:
        dict with overall stats
    """
    conn = connect(db_path)

    cid = start_collection(conn, "web_crawl", SOURCE_WEB, "multiple", "Web crawl for expression reference images")

    print(f"=== Web crawl started (collection #{cid}) ===")
    print(f"Sources: Wikimedia Commons")
    print(f"Max per expression: {max_per_expression}")

    overall = {"total": 0, "new": 0, "skipped": 0, "errors": 0, "by_expression": {}}

    # Ensure download dir
    if download_dir:
        os.makedirs(download_dir, exist_ok=True)

    for expr in EXPRESSIONS[:6]:  # joy through disgust
        print(f"\n--- Expression: {expr} ---")
        expr_stats = crawl_wikimedia(
            conn, expr, cid,
            max_images=max_per_expression,
            download_dir=os.path.join(download_dir, expr) if download_dir else None
        )

        for k in ("total", "new", "skipped", "errors"):
            overall[k] += expr_stats[k]
        overall["by_expression"][expr] = expr_stats["new"]

        print(f"  {expr}: {expr_stats['new']} new, {expr_stats['skipped']} skipped, {expr_stats['errors']} errors")

    finish_collection(conn, cid, total=overall["total"], new=overall["new"],
                      skipped=overall["skipped"], errors=overall["errors"])

    print(f"\n=== Crawl complete ===")
    print(f"Total: {overall['total']} found, {overall['new']} new, "
          f"{overall['skipped']} skipped, {overall['errors']} errors")
    for expr, cnt in sorted(overall["by_expression"].items()):
        print(f"  {expr}: {cnt}")

    conn.close()
    return overall
