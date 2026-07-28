#!/usr/bin/env python3
"""
ImageMaker M9: Image Collection Pipeline Runner

Runs local folder scan + web crawl in parallel.

Usage:
  python tools/image_collector/run.py scan <folder>
  python tools/image_collector/run.py crawl [--max N]
  python tools/image_collector/run.py all <folder> [--crawl]
  python tools/image_collector/run.py stats
"""

import sys
import os

# Ensure the project root is on the path so we can import the module
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.image_collector.cli import main

if __name__ == "__main__":
    main()
