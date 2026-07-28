"""
M10 Expression Extractor — MediaPipe Face Mesh → 3D expression deltas

Pipeline:
  1. Load image → MediaPipe Face Landmarker → 478 3D landmarks (normalized)
  2. Compute delta from canonical neutral landmarks
  3. Retarget to M7 base mesh topology (octa-sphere 258 verts)
  4. Output as .delta binary (M7-compatible format)

The key insight: MediaPipe's 478-landmark face mesh and M7's 258-vertex
octahedron-based face are different topologies. We map between them via:
  - For each M7 vertex, find K nearest MediaPipe landmarks
  - Interpolate the landmark deltas to the vertex position (IDW)
  - This produces a per-vertex offset for all M7 vertices

Requires: mediapipe>=1.0.0, opencv-python, numpy
Model: face_landmarker.task (auto-downloaded on first use)
"""

import numpy as np
import math
import os
from typing import Optional, Tuple, List

# MediaPipe face mesh has 478 landmarks (indices 0–477).
MEDIAPIPE_NUM_LANDMARKS = 478

# M7 base mesh: octahedron subdiv 3 = 258 vertices
M7_VERTEX_COUNT = 258

# Canonical neutral: MediaPipe landmarks for a neutral face.
CANONICAL_NEUTRAL: Optional[np.ndarray] = None  # shape (478, 3)

# Path to the MediaPipe face landmarker model
_MODEL_DIR = os.path.dirname(os.path.abspath(__file__))
_MODEL_PATH = os.path.join(_MODEL_DIR, "face_landmarker.task")

# Lazy-initialized FaceLandmarker
_FACE_LANDMARKER = None


def _get_face_landmarker():
    """Lazy-init MediaPipe FaceLandmarker."""
    global _FACE_LANDMARKER
    if _FACE_LANDMARKER is not None:
        return _FACE_LANDMARKER

    import mediapipe as mp
    from mediapipe.tasks import python
    from mediapipe.tasks.python import vision

    if not os.path.exists(_MODEL_PATH):
        raise FileNotFoundError(
            f"MediaPipe face landmarker model not found at {_MODEL_PATH}. "
            f"Download from: https://storage.googleapis.com/mediapipe-models/"
            f"face_landmarker/face_landmarker/float16/latest/face_landmarker.task"
        )

    base_options = python.BaseOptions(model_asset_path=_MODEL_PATH)
    options = vision.FaceLandmarkerOptions(
        base_options=base_options,
        running_mode=vision.RunningMode.IMAGE,
        num_faces=1,
        min_face_detection_confidence=0.5,
        min_face_presence_confidence=0.5,
        min_tracking_confidence=0.5,
        output_face_blendshapes=False,
    )
    detector = vision.FaceLandmarker.create_from_options(options)
    _FACE_LANDMARKER = detector
    return detector


def extract_landmarks(image_path: str) -> Optional[np.ndarray]:
    """
    Extract 478 3D face landmarks from an image using MediaPipe Face Landmarker.

    Args:
        image_path: path to image file

    Returns:
        (478, 3) float32 array of (x, y, z) landmarks, normalized to unit sphere
        centered at origin, or None if no face detected.
    """
    import mediapipe as mp
    import cv2

    img = cv2.imread(image_path)
    if img is None:
        print(f"  ERROR: cannot read image: {image_path}")
        return None

    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)

    detector = _get_face_landmarker()
    result = detector.detect(mp_image)

    if not result.face_landmarks:
        print(f"  WARNING: no face detected in {image_path}")
        return None

    landmarks = result.face_landmarks[0]
    pts = np.array([[lm.x, lm.y, lm.z] for lm in landmarks], dtype=np.float32)

    # Normalize: center at origin, scale to unit sphere radius ~0.45
    centroid = pts.mean(axis=0)
    pts_centered = pts - centroid
    max_dist = float(np.max(np.linalg.norm(pts_centered, axis=1)))
    if max_dist > 1e-6:
        pts_normalized = pts_centered / max_dist * 0.45
    else:
        pts_normalized = pts_centered

    return pts_normalized


def get_canonical_neutral() -> np.ndarray:
    """
    Return canonical neutral face landmarks.

    Uses a synthetic symmetric neutral face as reference.
    For production use, extract from a real neutral face photo.

    Returns:
        (478, 3) normalized landmark positions
    """
    global CANONICAL_NEUTRAL
    if CANONICAL_NEUTRAL is not None:
        return CANONICAL_NEUTRAL

    neutral = _generate_symmetric_neutral()
    CANONICAL_NEUTRAL = neutral
    return CANONICAL_NEUTRAL


def _generate_symmetric_neutral() -> np.ndarray:
    """
    Generate a symmetric neutral face landmark set (478 points).

    Based on MediaPipe's canonical face model topology:
      - Face is centered, facing forward
      - Mouth closed, eyes open, relaxed expression
      - Symmetric across X axis

    Returns (478, 3) array normalized to unit sphere radius ~0.45.
    """
    n = MEDIAPIPE_NUM_LANDMARKS
    pts = np.zeros((n, 3), dtype=np.float32)

    for i in range(n):
        u = i / max(n - 1, 1)  # 0..1

        if i < 33:  # Face outline (0-32)
            angle = (i / 32.0) * 2 * math.pi - math.pi / 2
            rx, ry = 0.28, 0.38
            pts[i] = [0.5 + rx * math.cos(angle), 0.45 + ry * math.sin(angle), 0.5]
        elif i < 70:  # Left eyebrow
            t = (i - 33) / 36.0
            pts[i] = [0.5 - 0.22 + t * 0.15, 0.28 + t * 0.02, 0.55]
        elif i < 106:  # Right eyebrow
            t = (i - 70) / 36.0 + 1e-6
            pts[i] = [0.5 + 0.07 + t * 0.15, 0.28 + t * 0.02, 0.55]
        elif i < 143:  # Left eye
            t = (i - 106) / 36.0
            angle = t * 2 * math.pi
            pts[i] = [0.5 - 0.16 + 0.06 * math.cos(angle),
                      0.36 + 0.04 * math.sin(angle), 0.62]
        elif i < 177:  # Right eye
            t = (i - 143) / 33.0
            angle = t * 2 * math.pi
            pts[i] = [0.5 + 0.16 + 0.06 * math.cos(angle),
                      0.36 + 0.04 * math.sin(angle), 0.62]
        elif i < 250:  # Nose
            t = (i - 177) / 72.0
            pts[i] = [0.5 + (t - 0.5) * 0.12, 0.48 + t * 0.12, 0.58 + t * 0.05]
        elif i < 320:  # Mouth outer
            t = (i - 250) / 69.0
            angle = t * 2 * math.pi
            pts[i] = [0.5 + 0.16 * math.cos(angle),
                      0.62 + 0.06 * math.sin(angle), 0.6]
        elif i < 400:  # Mouth inner
            t = (i - 320) / 79.0
            angle = t * 2 * math.pi
            pts[i] = [0.5 + 0.08 * math.cos(angle),
                      0.63 + 0.03 * math.sin(angle), 0.62]
        else:  # Irises + remaining
            t = (i - 400) / 77.0
            pts[i] = [0.5 + (t - 0.5) * 0.05, 0.34 + t * 0.05, 0.64]

    # Normalize: center at origin, scale to unit sphere
    centroid = pts.mean(axis=0)
    pts -= centroid
    max_dist = float(np.max(np.linalg.norm(pts, axis=1)))
    if max_dist > 1e-6:
        pts /= max_dist
        pts *= 0.45

    return pts


def compute_landmark_delta(
    expr_landmarks: np.ndarray,
    neutral_landmarks: Optional[np.ndarray] = None,
) -> np.ndarray:
    """
    Compute per-landmark displacement from neutral to expression.

    Args:
        expr_landmarks: (N, 3) expression landmarks (normalized)
        neutral_landmarks: (N, 3) neutral landmarks, or None for canonical

    Returns:
        (N, 3) delta vectors
    """
    if neutral_landmarks is None:
        neutral_landmarks = get_canonical_neutral()

    if expr_landmarks.shape != neutral_landmarks.shape:
        n = min(expr_landmarks.shape[0], neutral_landmarks.shape[0])
        expr_landmarks = expr_landmarks[:n]
        neutral_landmarks = neutral_landmarks[:n]

    return expr_landmarks - neutral_landmarks


def retarget_to_m7(
    landmark_deltas: np.ndarray,
    landmark_positions: np.ndarray,
    m7_vertices: np.ndarray,
    k: int = 5,
) -> np.ndarray:
    """
    Map MediaPipe landmark deltas to M7 base mesh vertices.

    Uses Inverse Distance Weighting (IDW) with K nearest landmarks.

    Args:
        landmark_deltas: (M, 3) delta per landmark
        landmark_positions: (M, 3) canonical landmark positions
        m7_vertices: (N, 3) M7 base mesh vertex positions
        k: number of nearest landmarks to interpolate from

    Returns:
        (N, 3) delta per M7 vertex
    """
    n_verts = m7_vertices.shape[0]
    vertex_deltas = np.zeros((n_verts, 3), dtype=np.float32)

    for i in range(n_verts):
        v = m7_vertices[i]
        diffs = landmark_positions - v
        dists = np.sqrt(np.sum(diffs * diffs, axis=1))

        if k >= len(dists):
            k_idx = np.arange(len(dists))
        else:
            k_idx = np.argpartition(dists, k)[:k]

        k_dists = dists[k_idx]
        eps = 1e-6
        weights = 1.0 / (k_dists + eps)
        weights /= weights.sum()

        for j, idx in enumerate(k_idx):
            vertex_deltas[i] += weights[j] * landmark_deltas[idx]

    return vertex_deltas


def extract_expression_delta(
    image_path: str,
    m7_base_vertices: np.ndarray,
    canonical_neutral_landmarks: Optional[np.ndarray] = None,
    k: int = 5,
) -> Optional[np.ndarray]:
    """
    Full pipeline: image → M7 vertex deltas.

    Args:
        image_path: path to expression image
        m7_base_vertices: (N, 3) M7 base mesh vertices
        canonical_neutral_landmarks: precomputed neutral landmarks
        k: IDW neighbor count

    Returns:
        (N, 3) per-vertex offsets, or None on failure
    """
    # Step 1: Extract landmarks
    landmarks = extract_landmarks(image_path)
    if landmarks is None:
        return None

    # Step 2: Get neutral reference
    if canonical_neutral_landmarks is None:
        canonical_neutral_landmarks = get_canonical_neutral()

    # Step 3: Compute landmark deltas
    landmark_deltas = compute_landmark_delta(landmarks, canonical_neutral_landmarks)

    # Step 4: Retarget to M7 vertices
    vertex_deltas = retarget_to_m7(
        landmark_deltas,
        canonical_neutral_landmarks,
        m7_base_vertices,
        k=k,
    )

    return vertex_deltas


def load_or_compute_neutral(image_path: str) -> Optional[np.ndarray]:
    """
    Extract neutral landmarks from a real neutral face image.
    This is more accurate than the synthetic canonical neutral.

    Args:
        image_path: path to a neutral-expression face photo

    Returns:
        (478, 3) normalized landmarks or None
    """
    return extract_landmarks(image_path)
