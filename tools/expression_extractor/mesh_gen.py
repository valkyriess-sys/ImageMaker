"""
M10 Mesh Generator — Synthesize M7-compatible base mesh when .mesh file not available.

Generates the same octahedron-based sphere mesh that M7's PrimitiveGenerator
produces (subdiv 3 = 258 vertices), with face-like deformations.
"""

import numpy as np
import math
from typing import Tuple


def generate_octasphere(subdiv: int = 3) -> Tuple[np.ndarray, np.ndarray]:
    """
    Generate an octahedron-based sphere with given subdivision level.
    Matches M7's PrimitiveGenerator::generateIcoSphere (actually octahedron-based).

    Returns:
        (vertices: (N, 3), indices: (M,)) as numpy arrays.
        Vertices are on the unit sphere.
    """
    # Octahedron base (6 vertices)
    verts = np.array([
        [0.0, 1.0, 0.0],   # top
        [0.0, -1.0, 0.0],  # bottom
        [1.0, 0.0, 0.0],   # right
        [-1.0, 0.0, 0.0],  # left
        [0.0, 0.0, 1.0],   # front
        [0.0, 0.0, -1.0],  # back
    ], dtype=np.float32)

    # Octahedron faces (8 triangles)
    faces = np.array([
        [0, 4, 2], [0, 2, 5], [0, 5, 3], [0, 3, 4],
        [1, 2, 4], [1, 5, 2], [1, 3, 5], [1, 4, 3],
    ], dtype=np.int32)

    vert_list = list(verts)
    face_list = list(faces)

    # Midpoint cache
    mid_cache = {}

    def midpoint(i1, i2):
        key = (min(i1, i2), max(i1, i2))
        if key in mid_cache:
            return mid_cache[key]
        mid = (vert_list[i1] + vert_list[i2]) / 2.0
        mid /= np.linalg.norm(mid)
        idx = len(vert_list)
        vert_list.append(mid)
        mid_cache[key] = idx
        return idx

    for _ in range(subdiv):
        new_faces = []
        for tri in face_list:
            a, b, c = tri
            ab = midpoint(a, b)
            bc = midpoint(b, c)
            ca = midpoint(c, a)
            new_faces.extend([
                [a, ab, ca],
                [b, bc, ab],
                [c, ca, bc],
                [ab, bc, ca],
            ])
        face_list = new_faces

    return np.array(vert_list, dtype=np.float32), np.array(face_list, dtype=np.int32).flatten()


def generate_m7_base(size: float = 1.0) -> np.ndarray:
    """
    Generate M7-compatible face base mesh vertices.

    Mimics ExpressionTemplateSystem::generateFaceBase():
      - Start with octahedron sphere (subdiv 3 = 258 verts)
      - Apply face-like deformations (wider cheeks, pointed chin, brow ridge)
      - Re-normalize to approximate sphere

    Returns:
        (258, 3) float32 vertex positions
    """
    verts, _ = generate_octasphere(subdiv=3)

    for i in range(len(verts)):
        x, y, z = verts[i]

        # Widen cheeks (XY plane adjustment)
        cheek_factor = 1.0 - (y * y) * 0.25
        x *= (1.0 + cheek_factor * 0.15)
        z *= (1.0 + cheek_factor * 0.10)

        # Pointed chin (lower -Y region)
        if y < -0.3:
            chin_t = (y + 1.0) / 0.7
            chin_t = 1.0 - chin_t
            squeeze = 1.0 - chin_t * 0.5
            x *= squeeze
            z *= squeeze
            y *= (1.0 - chin_t * 0.3)

        # Brow ridge (upper region)
        if 0.2 < y < 0.7:
            brow_t = (y - 0.2) / 0.5
            brow_t = brow_t * (1.0 - brow_t) * 4.0  # bell curve
            z += brow_t * 0.08 * size
            x *= (1.0 - brow_t * 0.05)

        # Flatten back of head
        if z < -0.3:
            back_t = (-z - 0.3) / 0.7
            z += back_t * 0.12 * size

        # Re-normalize to preserve approximate size
        length = math.sqrt(x * x + y * y + z * z)
        if length > 0.001:
            target_len = size * (0.9 + 0.1 * (1.0 - abs(y)))
            scale = target_len / length
            x *= scale
            y *= scale
            z *= scale

        verts[i] = [x, y, z]

    return verts
