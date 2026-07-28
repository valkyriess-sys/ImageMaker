"""
M10 Delta I/O — Read/Write M7 .delta and .mesh binary formats

M7 uses a simple binary format:
  .mesh:  [u32 vert_count][u32 idx_count][verts: (f32*6)][indices: u32*N]
          Each vertex: x, y, z, nx, ny, nz (6 floats)
  .delta: [u32 vert_count][u32 name_len][name bytes][offsets: (f32*3)*N]
          Each offset: x, y, z (3 floats)
"""

import struct
import os
import numpy as np
from typing import Tuple, Optional


def load_m7_base_mesh(path: str) -> Tuple[np.ndarray, np.ndarray]:
    """
    Load an M7 .mesh file.

    Returns:
        (vertices: (N, 3), indices: (M,)) as numpy arrays.
        vertices are position-only (first 3 of 6 floats per vertex).
    """
    with open(path, "rb") as f:
        vc = struct.unpack("<I", f.read(4))[0]
        ic = struct.unpack("<I", f.read(4))[0]

        vertices = np.zeros((vc, 3), dtype=np.float32)
        for i in range(vc):
            vertices[i, 0] = struct.unpack("<f", f.read(4))[0]  # x
            vertices[i, 1] = struct.unpack("<f", f.read(4))[0]  # y
            vertices[i, 2] = struct.unpack("<f", f.read(4))[0]  # z
            f.read(12)  # skip nx, ny, nz

        indices = np.zeros(ic, dtype=np.uint32)
        for i in range(ic):
            indices[i] = struct.unpack("<I", f.read(4))[0]

    return vertices, indices


def save_m7_delta(path: str, name: str, offsets: np.ndarray) -> bool:
    """
    Save offsets as an M7-compatible .delta file.

    Args:
        path: output file path
        name: expression name (e.g. "joy", "anger")
        offsets: (N, 3) float32 per-vertex offsets
    """
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

    vc = offsets.shape[0]
    name_bytes = name.encode("utf-8")
    name_len = len(name_bytes)

    with open(path, "wb") as f:
        f.write(struct.pack("<I", vc))
        f.write(struct.pack("<I", name_len))
        f.write(name_bytes)
        for i in range(vc):
            f.write(struct.pack("<f", float(offsets[i, 0])))
            f.write(struct.pack("<f", float(offsets[i, 1])))
            f.write(struct.pack("<f", float(offsets[i, 2])))

    print(f"  Saved delta: {path} ({vc} verts, name='{name}')")
    return True


def load_m7_delta(path: str) -> Tuple[np.ndarray, str]:
    """
    Load an M7 .delta file.

    Returns:
        (offsets: (N, 3), name: str)
    """
    with open(path, "rb") as f:
        vc = struct.unpack("<I", f.read(4))[0]
        name_len = struct.unpack("<I", f.read(4))[0]
        name = f.read(name_len).decode("utf-8")

        offsets = np.zeros((vc, 3), dtype=np.float32)
        for i in range(vc):
            offsets[i, 0] = struct.unpack("<f", f.read(4))[0]
            offsets[i, 1] = struct.unpack("<f", f.read(4))[0]
            offsets[i, 2] = struct.unpack("<f", f.read(4))[0]

    return offsets, name
