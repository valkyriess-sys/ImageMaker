# ImageMaker M10: Expression Extractor

2D facial expression images → 3D blend shape deltas for M7 expression system.

Uses MediaPipe Face Landmarker (478 3D landmarks) to extract facial geometry,
then retargets to M7's 258-vertex face base mesh via IDW interpolation.

## Quick Start

```bash
# 1. Download the MediaPipe model (one-time)
cd tools/expression_extractor
curl -sLO "https://storage.googleapis.com/mediapipe-models/face_landmarker/face_landmarker/float16/latest/face_landmarker.task"

# 2. Extract from a single image
python run.py extract path/to/face.jpg --expr joy --out templates/expressions

# 3. Batch process from M9 database
python run.py batch --db image_collection.db --out templates/expressions

# 4. Update the catalog
python run.py catalog --dir templates
```

## Pipeline

```
Image → MediaPipe Face Landmarker → 478 3D landmarks
  → Normalize (center, scale)
  → Compute delta from canonical neutral
  → IDW retarget to M7 base mesh (258 verts)
  → Save as .delta file (M7 binary format)
```

## Files

| File | Purpose |
|------|---------|
| `extract.py` | MediaPipe extraction + delta computation + retargeting |
| `delta_io.py` | M7 .delta/.mesh binary format read/write |
| `mesh_gen.py` | M7 base mesh synthesis (octa-sphere subdiv 3) |
| `run.py` | CLI entry point |
| `face_landmarker.task` | MediaPipe model file (~3.6 MB, downloaded separately) |

## M7 Integration

Extracted .delta files are compatible with M7's `ExpressionTemplateSystem`:

```cpp
// In ImageMaker main.cpp:
auto base = ExpressionTemplateSystem::generateFaceBase();
auto extracted = ExpressionTemplateSystem::loadExtractedTemplates(base, "templates/");
for (auto& delta : extracted) {
    ExpressionTemplateSystem::applyExpression(mesh.vertices, delta, 1.0f);
}
```

## Dependencies

- Python 3.10+
- mediapipe >= 1.0.0
- opencv-python
- numpy
