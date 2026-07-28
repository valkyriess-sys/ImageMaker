"""
M9 Image Collection Pipeline — Metadata Database (SQLite)

Schema:
  - images: core image metadata (path/url, expression, source, license, collection date)
  - collections: collection runs (batch metadata)
  - collection_images: many-to-many join

The DB is designed to feed M10 (auto extraction) and track provenance.
"""

import sqlite3
import os
import hashlib
import datetime
from typing import Optional

# Standard 6 expressions + neutral/other
EXPRESSIONS = ["joy", "anger", "sadness", "surprise", "fear", "disgust", "neutral", "other"]

# Valid source types
SOURCE_LOCAL = "local"
SOURCE_WEB = "web"

SCHEMA_SQL = """
CREATE TABLE IF NOT EXISTS collections (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    name        TEXT NOT NULL,
    source_type TEXT NOT NULL CHECK(source_type IN ('local', 'web')),
    source_path TEXT,           -- root folder path or base URL
    started_at  TEXT NOT NULL,
    finished_at TEXT,
    total_files INTEGER DEFAULT 0,
    new_files   INTEGER DEFAULT 0,
    skipped     INTEGER DEFAULT 0,
    errors      INTEGER DEFAULT 0,
    notes       TEXT
);

CREATE TABLE IF NOT EXISTS images (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    -- Content identity (hash of file contents, dedup key)
    content_sha256 TEXT NOT NULL UNIQUE,
    -- Where the image lives
    source_type TEXT NOT NULL CHECK(source_type IN ('local', 'web')),
    source_path TEXT NOT NULL,  -- absolute file path OR original URL
    local_path  TEXT,           -- where stored in reference library
    -- Image properties
    file_size   INTEGER,
    width       INTEGER,
    height      INTEGER,
    format      TEXT,           -- jpg, png, webp, etc.
    -- Expression metadata
    expression  TEXT NOT NULL CHECK(expression IN ('joy','anger','sadness','surprise','fear','disgust','neutral','other')),
    confidence  REAL DEFAULT 1.0,  -- how confident the expression tag is (1.0 = manually tagged, <1.0 = heuristics)
    -- Legal
    license_type TEXT,          -- CC0, Public Domain, CC-BY, etc.
    license_url  TEXT,
    attribution  TEXT,
    -- Tracking
    collected_at TEXT NOT NULL,
    collection_id INTEGER REFERENCES collections(id),
    notes       TEXT
);

CREATE INDEX IF NOT EXISTS idx_images_expression ON images(expression);
CREATE INDEX IF NOT EXISTS idx_images_source_type ON images(source_type);
CREATE INDEX IF NOT EXISTS idx_images_collection_id ON images(collection_id);
CREATE INDEX IF NOT EXISTS idx_images_content_sha256 ON images(content_sha256);

CREATE TABLE IF NOT EXISTS collection_images (
    collection_id INTEGER NOT NULL REFERENCES collections(id),
    image_id      INTEGER NOT NULL REFERENCES images(id),
    PRIMARY KEY (collection_id, image_id)
);
"""


def _now_iso() -> str:
    return datetime.datetime.now().strftime("%Y-%m-%dT%H:%M:%S")


def connect(db_path: str) -> sqlite3.Connection:
    """Open DB connection and initialize schema."""
    os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
    conn = sqlite3.connect(db_path)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA foreign_keys=ON")
    conn.executescript(SCHEMA_SQL)
    conn.commit()
    return conn


def sha256_file(filepath: str) -> str:
    """SHA-256 hash of file contents."""
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def start_collection(conn: sqlite3.Connection, name: str, source_type: str,
                     source_path: Optional[str] = None, notes: Optional[str] = None) -> int:
    """Begin a collection run. Returns collection_id."""
    c = conn.execute(
        "INSERT INTO collections (name, source_type, source_path, started_at, notes) VALUES (?,?,?,?,?)",
        (name, source_type, source_path, _now_iso(), notes)
    )
    conn.commit()
    return c.lastrowid


def finish_collection(conn: sqlite3.Connection, collection_id: int,
                      total: int = 0, new: int = 0, skipped: int = 0, errors: int = 0):
    """Mark a collection run as finished with stats."""
    conn.execute(
        """UPDATE collections SET finished_at=?, total_files=?, new_files=?, skipped=?, errors=?
           WHERE id=?""",
        (_now_iso(), total, new, skipped, errors, collection_id)
    )
    conn.commit()


def add_image(conn: sqlite3.Connection,
              content_sha256: str,
              source_type: str,
              source_path: str,
              local_path: Optional[str],
              file_size: int,
              width: int,
              height: int,
              format: str,
              expression: str,
              confidence: float = 1.0,
              license_type: Optional[str] = None,
              license_url: Optional[str] = None,
              attribution: Optional[str] = None,
              collection_id: Optional[int] = None,
              notes: Optional[str] = None) -> Optional[int]:
    """
    Insert or skip image by content hash.
    Returns image_id if new, None if already exists.
    """
    try:
        c = conn.execute(
            """INSERT INTO images (content_sha256, source_type, source_path, local_path,
               file_size, width, height, format, expression, confidence,
               license_type, license_url, attribution, collected_at, collection_id, notes)
               VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
            (content_sha256, source_type, source_path, local_path,
             file_size, width, height, format, expression, confidence,
             license_type, license_url, attribution, _now_iso(), collection_id, notes)
        )
        conn.commit()
        image_id = c.lastrowid

        if collection_id:
            conn.execute(
                "INSERT OR IGNORE INTO collection_images (collection_id, image_id) VALUES (?,?)",
                (collection_id, image_id)
            )
            conn.commit()

        return image_id
    except sqlite3.IntegrityError:
        # UNIQUE constraint on content_sha256 — already exists
        return None


def image_exists(conn: sqlite3.Connection, content_sha256: str) -> bool:
    """Check if an image with this content hash already exists."""
    c = conn.execute("SELECT 1 FROM images WHERE content_sha256=?", (content_sha256,))
    return c.fetchone() is not None


def get_stats(conn: sqlite3.Connection) -> dict:
    """Get summary statistics."""
    total = conn.execute("SELECT COUNT(*) FROM images").fetchone()[0]
    by_expr = {}
    for row in conn.execute(
        "SELECT expression, COUNT(*) as cnt FROM images GROUP BY expression ORDER BY cnt DESC"
    ):
        by_expr[row[0]] = row[1]
    by_source = {}
    for row in conn.execute(
        "SELECT source_type, COUNT(*) as cnt FROM images GROUP BY source_type"
    ):
        by_source[row[0]] = row[1]
    return {"total": total, "by_expression": by_expr, "by_source": by_source}


def get_collections(conn: sqlite3.Connection, limit: int = 10) -> list[dict]:
    """Get recent collections."""
    rows = conn.execute(
        "SELECT id, name, source_type, source_path, started_at, finished_at, total_files, new_files, skipped, errors, notes FROM collections ORDER BY id DESC LIMIT ?",
        (limit,)
    )
    return [dict(zip(["id","name","source_type","source_path","started_at","finished_at","total_files","new_files","skipped","errors","notes"], row)) for row in rows]
