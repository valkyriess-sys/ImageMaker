#!/usr/bin/env python3
"""
ImageMaker M10: Expression Extraction Pipeline — CLI

Extracts 3D expression deltas from 2D facial images using MediaPipe Face Mesh
and retargets them to M7's standard face base mesh.

Usage:
  python tools/expression_extractor/run.py extract <image> [--expr NAME] [--out DIR]
  python tools/expression_extractor/run.py batch --db PATH [--out DIR] [--limit N]
  python tools/expression_extractor/run.py init-neutral [--out PATH]
  python tools/expression_extractor/run.py catalog [--dir DIR]

Examples:
  # Extract from single image
  python tools/expression_extractor/run.py extract joy_001.jpg --expr joy --out templates

  # Batch process from M9 database
  python tools/expression_extractor/run.py batch --db image_collection.db --out templates

  # Generate canonical neutral landmarks
  python tools/expression_extractor/run.py init-neutral
"""

import sys
import os
import argparse
import json
import numpy as np
from typing import Optional

# Ensure project root is on path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.expression_extractor.extract import (
    extract_expression_delta,
    extract_landmarks,
    get_canonical_neutral,
    _generate_symmetric_neutral,
)
from tools.expression_extractor.delta_io import (
    load_m7_base_mesh,
    save_m7_delta,
    load_m7_delta,
)


def get_default_base_mesh() -> np.ndarray:
    """Find and load the M7 standard face base mesh."""
    # Search common locations
    search_paths = [
        "templates/standard_face_base.mesh",
        "../templates/standard_face_base.mesh",
        os.path.join(os.path.dirname(__file__), "../../templates/standard_face_base.mesh"),
    ]
    for p in search_paths:
        if os.path.exists(p):
            verts, _ = load_m7_base_mesh(p)
            return verts

    # Not found — generate synthetically (ico-sphere subdiv 3)
    print("  WARNING: M7 base mesh not found, generating synthetic base")
    from tools.expression_extractor.mesh_gen import generate_m7_base
    return generate_m7_base()


def cmd_extract(args):
    """Extract expression delta from a single image."""
    if not os.path.exists(args.image):
        print(f"ERROR: image not found: {args.image}")
        sys.exit(1)

    base_verts = get_default_base_mesh()

    # Extract expression name from filename if not provided
    expr_name = args.expr
    if expr_name is None:
        fname = os.path.splitext(os.path.basename(args.image))[0]
        expr_name = fname  # fallback to filename

    print(f"Extracting expression from: {args.image}")
    print(f"  Expression: {expr_name}")
    print(f"  M7 base: {base_verts.shape[0]} vertices")

    deltas = extract_expression_delta(args.image, base_verts)
    if deltas is None:
        print("FAILED: could not extract expression")
        sys.exit(1)

    # Save
    out_dir = args.out or "templates/expressions"
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{expr_name}.delta")
    save_m7_delta(out_path, expr_name, deltas)

    # Print stats
    magnitude = np.linalg.norm(deltas, axis=1)
    print(f"  Delta magnitude: min={magnitude.min():.4f}, max={magnitude.max():.4f}, "
          f"mean={magnitude.mean():.4f}")
    print(f"  Saved to: {out_path}")


def cmd_batch(args):
    """Batch extract from M9 database."""
    # Import M9 DB module
    from tools.image_collector.db import connect, EXPRESSIONS

    db_path = args.db
    if not os.path.exists(db_path):
        print(f"ERROR: DB not found: {db_path}")
        sys.exit(1)

    conn = connect(db_path)
    base_verts = get_default_base_mesh()

    # Get neutral reference landmarks
    neutral_landmarks = get_canonical_neutral()

    # Query images grouped by expression
    limit_clause = f"LIMIT {args.limit}" if args.limit else ""
    target_exprs = ["joy", "anger", "sadness", "surprise", "fear", "disgust"]

    out_dir = args.out or "templates/expressions"
    os.makedirs(out_dir, exist_ok=True)

    total_extracted = 0
    total_failed = 0

    print(f"=== M10: Batch expression extraction ===")
    print(f"  DB: {db_path}")
    print(f"  Output: {out_dir}/")

    for expr in target_exprs:
        rows = conn.execute(
            f"SELECT id, local_path, source_path, confidence FROM images "
            f"WHERE expression=? AND local_path IS NOT NULL "
            f"ORDER BY confidence DESC {limit_clause}",
            (expr,)
        ).fetchall()

        if not rows:
            # Try source_path if no local_path
            rows = conn.execute(
                f"SELECT id, source_path, source_path, confidence FROM images "
                f"WHERE expression=? AND source_type='local' "
                f"ORDER BY confidence DESC {limit_clause}",
                (expr,)
            ).fetchall()

        if not rows:
            print(f"  {expr}: no images found")
            continue

        print(f"\n  {expr}: {len(rows)} images")

        # Try each image until we get a good extraction
        best_deltas = None
        best_conf = 0.0
        best_img = None

        for row in rows:
            img_path = row[1] or row[2]
            if not img_path or not os.path.exists(img_path):
                continue

            conf = row[3] if row[3] else 1.0

            print(f"    Trying: {os.path.basename(img_path)} (conf={conf:.2f})")
            deltas = extract_expression_delta(
                img_path, base_verts, neutral_landmarks
            )

            if deltas is not None:
                # Check if this is better than current best
                mag = np.linalg.norm(deltas, axis=1).mean()
                if mag > 0.001 and conf >= best_conf:
                    best_deltas = deltas
                    best_conf = conf
                    best_img = img_path
                    if conf >= 0.9:  # high confidence — stop searching
                        break

        if best_deltas is not None:
            out_path = os.path.join(out_dir, f"{expr}.delta")
            save_m7_delta(out_path, expr, best_deltas)
            total_extracted += 1
            print(f"    ✓ Extracted from: {os.path.basename(best_img)}")
        else:
            total_failed += 1
            print(f"    ✗ No usable face found for {expr}")

    conn.close()

    print(f"\n=== Done: {total_extracted} extracted, {total_failed} failed ===")

    # Update catalog
    if total_extracted > 0:
        cmd_catalog_like(out_dir)


def cmd_init_neutral(args):
    """Generate and save canonical neutral landmarks."""
    neutral = get_canonical_neutral()
    out_path = args.out or "templates/canonical_neutral.npy"
    np.save(out_path, neutral)
    print(f"Saved canonical neutral landmarks: {out_path}")
    print(f"  Shape: {neutral.shape}")
    print(f"  Center: {neutral.mean(axis=0)}")
    print(f"  Max radius: {np.max(np.linalg.norm(neutral, axis=1)):.4f}")


def cmd_catalog(args):
    """Update the expression template catalog."""
    catalog_dir = args.dir or "templates"
    cmd_catalog_like(catalog_dir)


def cmd_catalog_like(catalog_dir: str):
    """Generate/update catalog.json from .delta files in expressions/."""
    expr_dir = os.path.join(catalog_dir, "expressions")
    if not os.path.isdir(expr_dir):
        print(f"No expressions directory: {expr_dir}")
        return

    deltas = []
    for fname in sorted(os.listdir(expr_dir)):
        if not fname.endswith(".delta"):
            continue
        fpath = os.path.join(expr_dir, fname)
        try:
            offsets, name = load_m7_delta(fpath)
            deltas.append({
                "name": name,
                "file": f"expressions/{fname}",
                "vertexCount": offsets.shape[0],
            })
        except Exception as e:
            print(f"  WARNING: failed to read {fpath}: {e}")

    if not deltas:
        print("No .delta files found")
        return

    catalog = {
        "base": "standard_face_base.mesh",
        "generator": "ImageMaker M10 (MediaPipe extraction)",
        "expressions": deltas,
    }

    catalog_path = os.path.join(catalog_dir, "catalog.json")
    os.makedirs(catalog_dir, exist_ok=True)
    with open(catalog_path, "w") as f:
        json.dump(catalog, f, indent=2)

    print(f"Catalog saved: {catalog_path}")
    print(f"  Expressions: {len(deltas)}")
    for d in deltas:
        print(f"    {d['name']}: {d['vertexCount']} verts")


def main():
    parser = argparse.ArgumentParser(
        description="ImageMaker M10: Expression Extraction Pipeline"
    )

    sub = parser.add_subparsers(dest="command", help="Commands")

    # extract
    p_extract = sub.add_parser("extract", help="Extract from single image")
    p_extract.add_argument("image", help="Path to expression image")
    p_extract.add_argument("--expr", help="Expression name (auto-detected if omitted)")
    p_extract.add_argument("--out", default="templates/expressions",
                           help="Output directory (default: templates/expressions)")

    # batch
    p_batch = sub.add_parser("batch", help="Batch extract from M9 database")
    p_batch.add_argument("--db", required=True, help="Path to M9 SQLite DB")
    p_batch.add_argument("--out", default="templates/expressions",
                         help="Output directory")
    p_batch.add_argument("--limit", type=int, help="Max images per expression")

    # init-neutral
    p_neutral = sub.add_parser("init-neutral", help="Generate canonical neutral landmarks")
    p_neutral.add_argument("--out", help="Output path (default: templates/canonical_neutral.npy)")

    # catalog
    p_catalog = sub.add_parser("catalog", help="Update expression catalog")
    p_catalog.add_argument("--dir", default="templates", help="Templates directory")

    args = parser.parse_args()

    if args.command == "extract":
        cmd_extract(args)
    elif args.command == "batch":
        cmd_batch(args)
    elif args.command == "init-neutral":
        cmd_init_neutral(args)
    elif args.command == "catalog":
        cmd_catalog(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
