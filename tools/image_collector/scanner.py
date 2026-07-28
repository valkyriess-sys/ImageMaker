"""
M9 Image Collection Pipeline — Local Folder Scanner (A)

Scans a directory tree for image files, extracts expression tags from
folder names or filename prefixes, and registers them in the metadata DB.
"""

import os
import re
import glob
from typing import Optional

from .db import (
    connect, sha256_file, image_exists, add_image,
    start_collection, finish_collection, SOURCE_LOCAL, EXPRESSIONS
)

# Supported image extensions
IMAGE_EXTS = {".jpg", ".jpeg", ".png", ".webp", ".bmp", ".tiff", ".tif", ".gif"}

# Expression tag extraction patterns
# Priority: folder name > filename prefix > filename keyword
EXPR_FOLDER_PATTERN = re.compile(
    r'(joy|anger|sad|sadness|surprise|fear|disgust|neutral|other)',
    re.IGNORECASE
)
EXPR_FILENAME_PATTERN = re.compile(
    r'^(joy|anger|sad|sadness|surprise|fear|disgust|neutral|other)[_\-]',
    re.IGNORECASE
)
EXPR_KEYWORD_PATTERN = re.compile(
    r'(joy|happy|smile|anger|angry|sad|sadness|cry|surprise|shock|fear|scared|disgust|neutral)',
    re.IGNORECASE
)

# Map keyword variations to canonical names
EXPR_CANONICAL = {
    "joy": "joy", "happy": "joy", "smile": "joy",
    "anger": "anger", "angry": "anger",
    "sad": "sadness", "sadness": "sadness", "cry": "sadness",
    "surprise": "surprise", "shock": "surprise",
    "fear": "fear", "scared": "fear",
    "disgust": "disgust",
    "neutral": "neutral",
}


def _is_image(path: str) -> bool:
    """Check if file extension is a supported image format."""
    return os.path.splitext(path)[1].lower() in IMAGE_EXTS


def _extract_expression(filepath: str) -> Optional[str]:
    """
    Extract expression tag from file path.

    Priority:
      1. Parent folder name matches an expression
      2. Grandparent folder name matches
      3. Filename prefix (e.g. "joy_001.jpg")
      4. Keyword in filename

    Returns canonical expression name or None.
    """
    filename = os.path.basename(filepath)
    dirname = os.path.basename(os.path.dirname(filepath))
    parent_dirname = os.path.basename(os.path.dirname(os.path.dirname(filepath)))

    # 1. Parent folder
    m = EXPR_FOLDER_PATTERN.match(dirname)
    if m:
        key = m.group(1).lower()
        return EXPR_CANONICAL.get(key, key)

    # 2. Grandparent folder
    m = EXPR_FOLDER_PATTERN.match(parent_dirname)
    if m:
        key = m.group(1).lower()
        return EXPR_CANONICAL.get(key, key)

    # 3. Filename prefix
    m = EXPR_FILENAME_PATTERN.match(filename)
    if m:
        key = m.group(1).lower()
        return EXPR_CANONICAL.get(key, key)

    # 4. Keyword in filename
    m = EXPR_KEYWORD_PATTERN.search(filename.lower())
    if m:
        key = m.group(1).lower()
        return EXPR_CANONICAL.get(key, "other")

    return None


def scan_folder(conn, root_dir: str, collection_id: Optional[int] = None,
                copy_to: Optional[str] = None) -> dict:
    """
    Scan a directory recursively for images, extract expression tags, register in DB.

    Args:
        conn: SQLite connection
        root_dir: root directory to scan
        collection_id: optional collection run ID for grouping
        copy_to: if set, copy images to this reference library root

    Returns:
        dict with stats: total, new, skipped, errors, by_expression
    """
    stats = {"total": 0, "new": 0, "skipped": 0, "errors": 0, "by_expression": {}}

    # Walk
    for dirpath, dirnames, filenames in os.walk(root_dir):
        dirnames[:] = sorted(dirnames)
        for fname in sorted(filenames):
            fpath = os.path.join(dirpath, fname)
            if not _is_image(fpath):
                continue

            stats["total"] += 1

            try:
                # Extract expression tag
                expr = _extract_expression(fpath)
                if expr is None:
                    expr = "other"

                # Dedup by content hash
                csum = sha256_file(fpath)
                if image_exists(conn, csum):
                    stats["skipped"] += 1
                    continue

                # File info
                fsize = os.path.getsize(fpath)
                ext = os.path.splitext(fpath)[1].lower().lstrip(".")
                if ext == "jpeg":
                    ext = "jpg"

                # Try to get dimensions (optional, skip on failure)
                width, height = 0, 0
                try:
                    # lightweight: just read header bytes to get dimensions
                    import struct
                    with open(fpath, "rb") as f:
                        header = f.read(32)
                    if header[:4] == b'\x89PNG':
                        width = struct.unpack('>I', header[16:20])[0]
                        height = struct.unpack('>I', header[20:24])[0]
                    elif header[:2] == b'\xff\xd8':
                        # JPEG: scan for SOF marker
                        with open(fpath, "rb") as f:
                            f.seek(2)
                            while True:
                                marker = f.read(2)
                                if len(marker) < 2 or marker[0] != 0xff:
                                    break
                                if marker[1] in (0xC0, 0xC1, 0xC2):
                                    f.read(3)
                                    h = f.read(4)
                                    height = struct.unpack('>H', h[0:2])[0]
                                    width = struct.unpack('>H', h[2:4])[0]
                                    break
                                else:
                                    seg_len = struct.unpack('>H', f.read(2))[0]
                                    f.seek(seg_len - 2, 1)
                except Exception:
                    pass  # dimensions are optional

                local_path = None
                if copy_to:
                    # Copy to reference library organized by expression
                    dest_dir = os.path.join(copy_to, expr)
                    os.makedirs(dest_dir, exist_ok=True)
                    dest_path = os.path.join(dest_dir, fname)
                    # Avoid overwrite: append counter
                    if os.path.exists(dest_path):
                        base, ext2 = os.path.splitext(fname)
                        counter = 1
                        while os.path.exists(os.path.join(dest_dir, f"{base}_{counter}{ext2}")):
                            counter += 1
                        dest_path = os.path.join(dest_dir, f"{base}_{counter}{ext2}")
                    # For scratch workspace, just use the original path
                    local_path = dest_path
                    with open(fpath, "rb") as src, open(dest_path, "wb") as dst:
                        dst.write(src.read())

                img_id = add_image(
                    conn,
                    content_sha256=csum,
                    source_type=SOURCE_LOCAL,
                    source_path=fpath,
                    local_path=local_path,
                    file_size=fsize,
                    width=width,
                    height=height,
                    format=ext,
                    expression=expr,
                    confidence=1.0,  # folder tag = high confidence
                    license_type=None,
                    collection_id=collection_id,
                )

                if img_id is not None:
                    stats["new"] += 1
                    stats["by_expression"][expr] = stats["by_expression"].get(expr, 0) + 1
                else:
                    stats["skipped"] += 1

            except Exception as e:
                stats["errors"] += 1
                print(f"  ERROR: {fpath}: {e}")

    return stats


def run_local_scan(db_path: str, root_dir: str, copy_to: Optional[str] = None) -> dict:
    """
    Full local folder scan with collection tracking.

    Args:
        db_path: path to SQLite DB
        root_dir: root directory to scan
        copy_to: optional directory to copy images into (organized by expression)

    Returns:
        dict with stats
    """
    if not os.path.isdir(root_dir):
        print(f"ERROR: directory not found: {root_dir}")
        return {"total": 0, "new": 0, "skipped": 0, "errors": 1, "by_expression": {}}

    conn = connect(db_path)

    # Start collection
    cid = start_collection(conn, f"local_scan_{os.path.basename(root_dir)}",
                           SOURCE_LOCAL, root_dir)

    print(f"=== Local folder scan: {root_dir} ===")
    print(f"Collection #{cid} started")

    stats = scan_folder(conn, root_dir, collection_id=cid, copy_to=copy_to)

    # Finish
    finish_collection(conn, cid, total=stats["total"], new=stats["new"],
                      skipped=stats["skipped"], errors=stats["errors"])

    print(f"Scan complete: {stats['total']} files, {stats['new']} new, "
          f"{stats['skipped']} skipped, {stats['errors']} errors")
    for expr, cnt in sorted(stats["by_expression"].items()):
        print(f"  {expr}: {cnt}")

    conn.close()
    return stats
