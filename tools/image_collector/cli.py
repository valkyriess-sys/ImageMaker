#!/usr/bin/env python3
"""
M9: Image Collection Pipeline — CLI

Usage:
  python -m image_collector.cli scan <folder>          # Scan local folder
  python -m image_collector.cli crawl [--dir <dir>]     # Web crawl
  python -m image_collector.cli stats                   # Show DB stats
  python -m image_collector.cli all <folder> [--dir <dir> --crawl]  # Both

Common options:
  --db PATH     SQLite DB path (default: ./image_collection.db)
  --copy-to DIR Copy images to reference library (organized by expression)
"""

import os
import sys
import argparse

from .db import connect, get_stats, get_collections
from .scanner import run_local_scan
from .crawler import run_web_crawl


def cmd_scan(args):
    """Scan local folder for images."""
    if not os.path.isdir(args.folder):
        print(f"ERROR: folder not found: {args.folder}")
        sys.exit(1)
    run_local_scan(args.db, args.folder, copy_to=args.copy_to)


def cmd_crawl(args):
    """Crawl web for expression images."""
    download_dir = args.dir or os.path.join(os.path.dirname(args.db), "web_images")
    run_web_crawl(args.db, download_dir=download_dir, max_per_expression=args.max)


def cmd_stats(args):
    """Show database statistics."""
    conn = connect(args.db)
    stats = get_stats(conn)
    print(f"=== Image Collection DB: {args.db} ===")
    print(f"Total images: {stats['total']}")
    print(f"\nBy expression:")
    for expr, cnt in sorted(stats["by_expression"].items()):
        print(f"  {expr}: {cnt}")
    print(f"\nBy source:")
    for src, cnt in sorted(stats["by_source"].items()):
        print(f"  {src}: {cnt}")

    print(f"\nRecent collections:")
    for col in get_collections(conn, limit=5):
        status = "DONE" if col["finished_at"] else "RUNNING"
        print(f"  #{col['id']} [{status}] {col['name']} — "
              f"{col['new_files']} new, {col['skipped']} skipped"
              f"{' (' + col['notes'] + ')' if col['notes'] else ''}")
    conn.close()


def cmd_all(args):
    """Run both local scan and web crawl."""
    print("=" * 60)
    print("M9: Image Collection Pipeline — Full Run")
    print("=" * 60)

    # Scan local
    if os.path.isdir(args.folder):
        print("\n>>> PART A: Local folder scan")
        run_local_scan(args.db, args.folder, copy_to=args.copy_to)
    else:
        print(f"\n>>> PART A: SKIPPED (folder not found: {args.folder})")

    # Crawl web
    if args.crawl:
        print("\n>>> PART B: Web crawl")
        download_dir = args.dir or os.path.join(os.path.dirname(args.db), "web_images")
        run_web_crawl(args.db, download_dir=download_dir, max_per_expression=args.max)

    # Show final stats
    print("\n>>> FINAL STATS")
    conn = connect(args.db)
    stats = get_stats(conn)
    print(f"Total images in DB: {stats['total']}")
    for expr, cnt in sorted(stats["by_expression"].items()):
        print(f"  {expr}: {cnt}")
    conn.close()


def main():
    parser = argparse.ArgumentParser(
        description="ImageMaker M9: Image Collection Pipeline"
    )
    parser.add_argument("--db", default="image_collection.db",
                        help="SQLite DB path (default: ./image_collection.db)")

    sub = parser.add_subparsers(dest="command", help="Commands")

    # scan
    p_scan = sub.add_parser("scan", help="Scan local folder for images")
    p_scan.add_argument("folder", help="Root folder to scan")
    p_scan.add_argument("--copy-to", help="Copy images to reference library")

    # crawl
    p_crawl = sub.add_parser("crawl", help="Crawl web for expression images")
    p_crawl.add_argument("--dir", help="Download directory")
    p_crawl.add_argument("--max", type=int, default=10,
                          help="Max images per expression (default: 10)")

    # stats
    sub.add_parser("stats", help="Show database statistics")

    # all
    p_all = sub.add_parser("all", help="Run local scan + web crawl")
    p_all.add_argument("folder", help="Local folder to scan")
    p_all.add_argument("--dir", help="Download directory for web crawl")
    p_all.add_argument("--copy-to", help="Copy local images to reference library")
    p_all.add_argument("--crawl", action="store_true", default=True,
                       help="Enable web crawl (default: on)")
    p_all.add_argument("--max", type=int, default=10,
                        help="Max images per expression for crawl (default: 10)")

    args = parser.parse_args()

    if args.command == "scan":
        cmd_scan(args)
    elif args.command == "crawl":
        cmd_crawl(args)
    elif args.command == "stats":
        cmd_stats(args)
    elif args.command == "all":
        cmd_all(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
