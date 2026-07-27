#pragma once
// ─── ImageMaker M4: Mesh Post-Processing ──────────────────────────────
// Decimation (polygon reduction), UV unwrap, normal map bake
// CPU-side algorithms, no external dependencies

#include "mesh_generator.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// ─── UV vertex ────────────────────────────────────────────────────────
struct UVVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;  // texture coords
};

// ─── Decimation result ────────────────────────────────────────────────
struct DecimatedMesh {
    std::vector<UVVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t originalTris;
    uint32_t reducedTris;
    float reductionRatio;
};

class MeshPostProcess {
public:
    // ═══════════════════════════════════════════════════════════════════
    // 1. DECIMATION — vertex clustering with centroid merging
    //    Reduces vertex count by merging vertices within a grid cell.
    //    Uses iterative refinement to hit target ratio accurately,
    //    accounting for surface vs. volumetric mesh topology.
    // ═══════════════════════════════════════════════════════════════════
    static DecimatedMesh decimate(const std::vector<Vertex>& verts,
                                   const std::vector<uint32_t>& indices,
                                   float targetRatio = 0.1f) {
        DecimatedMesh result;
        result.originalTris = (uint32_t)indices.size() / 3;

        if (verts.empty() || indices.empty()) return result;

        // Compute bounding box
        glm::vec3 bbMin(1e9f), bbMax(-1e9f);
        for (auto& v : verts) {
            bbMin = glm::min(bbMin, glm::vec3(v.x, v.y, v.z));
            bbMax = glm::max(bbMax, glm::vec3(v.x, v.y, v.z));
        }
        glm::vec3 bbSize = bbMax - bbMin;
        float maxDim = std::max({bbSize.x, bbSize.y, bbSize.z});
        if (maxDim < 1e-6f) maxDim = 1.0f;

        // Iterative refinement: adjust cellSize to converge on target ratio.
        // Single-pass cube-root formula assumes volume-filling; real meshes
        // are surfaces needing larger cells for the same reduction.
        float cellSize = maxDim * 0.25f;
        float bestRatio = 1.0f;
        std::vector<UVVertex> bestVerts;
        std::vector<uint32_t> bestIndices;

        for (int iter = 0; iter < 4; iter++) {
            // Quantize vertices to grid cells
            struct CellKey {
                int cx, cy, cz;
                bool operator==(const CellKey& o) const { return cx==o.cx && cy==o.cy && cz==o.cz; }
            };
            struct CellKeyHash {
                size_t operator()(const CellKey& k) const {
                    return ((size_t)k.cx * 73856093) ^ ((size_t)k.cy * 19349663) ^ ((size_t)k.cz * 83492791);
                }
            };

            std::unordered_map<CellKey, std::vector<uint32_t>, CellKeyHash> cellMap;
            for (uint32_t i = 0; i < (uint32_t)verts.size(); i++) {
                CellKey k;
                k.cx = (int)std::floor((verts[i].x - bbMin.x) / cellSize);
                k.cy = (int)std::floor((verts[i].y - bbMin.y) / cellSize);
                k.cz = (int)std::floor((verts[i].z - bbMin.z) / cellSize);
                cellMap[k].push_back(i);
            }

            // Build old→new vertex mapping + new vertices (centroids)
            std::vector<uint32_t> oldToNew(verts.size(), UINT32_MAX);
            std::vector<UVVertex> newVerts;

            for (auto& [cell, vtxIds] : cellMap) {
                glm::vec3 centroid(0);
                glm::vec3 avgNormal(0);
                for (auto vi : vtxIds) {
                    centroid += glm::vec3(verts[vi].x, verts[vi].y, verts[vi].z);
                    avgNormal += glm::vec3(verts[vi].nx, verts[vi].ny, verts[vi].nz);
                }
                centroid /= (float)vtxIds.size();
                avgNormal = glm::normalize(avgNormal);

                UVVertex nv;
                nv.x = centroid.x; nv.y = centroid.y; nv.z = centroid.z;
                nv.nx = avgNormal.x; nv.ny = avgNormal.y; nv.nz = avgNormal.z;
                nv.u = 0; nv.v = 0;
                uint32_t newIdx = (uint32_t)newVerts.size();
                newVerts.push_back(nv);
                for (auto vi : vtxIds) oldToNew[vi] = newIdx;
            }

            // Build new index buffer (skip degenerate triangles)
            std::vector<uint32_t> newIndices;
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                uint32_t a = oldToNew[indices[i]];
                uint32_t b = oldToNew[indices[i+1]];
                uint32_t c = oldToNew[indices[i+2]];
                if (a == UINT32_MAX || b == UINT32_MAX || c == UINT32_MAX) continue;
                if (a == b || b == c || a == c) continue;
                newIndices.push_back(a);
                newIndices.push_back(b);
                newIndices.push_back(c);
            }

            uint32_t reducedTris = (uint32_t)newIndices.size() / 3;
            float achievedRatio = (float)reducedTris / (float)result.originalTris;

            // Track best match
            if (fabs(achievedRatio - targetRatio) < fabs(bestRatio - targetRatio)) {
                bestRatio = achievedRatio;
                bestVerts = std::move(newVerts);
                bestIndices = std::move(newIndices);
            }

            // Within 3% tolerance → done
            if (fabs(achievedRatio - targetRatio) < 0.03f) break;

            // Adjust cellSize: if ratio too high (not enough reduction),
            // cellSize is too small → increase it; vice versa.
            float adjust = std::sqrt(achievedRatio / (targetRatio + 1e-6f));
            adjust = std::max(0.5f, std::min(2.0f, adjust));
            cellSize *= adjust;

            cellSize = std::max(cellSize, maxDim * 0.005f);
            cellSize = std::min(cellSize, maxDim * 5.0f);
        }

        if (!bestVerts.empty()) {
            generatePlanarUV(bestVerts, bestIndices);
            result.vertices = std::move(bestVerts);
            result.indices = std::move(bestIndices);
            result.reducedTris = (uint32_t)result.indices.size() / 3;
            result.reductionRatio = (float)result.reducedTris / (float)result.originalTris;
        }

        // Safety floor: very small meshes may collapse entirely.
        // Return at least one valid triangle.
        if (result.indices.size() < 3 && result.originalTris > 0) {
            result.vertices.clear();
            result.indices.clear();
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                UVVertex v0, v1, v2;
                v0.x = verts[indices[i]].x;   v0.y = verts[indices[i]].y;   v0.z = verts[indices[i]].z;
                v0.nx = verts[indices[i]].nx; v0.ny = verts[indices[i]].ny; v0.nz = verts[indices[i]].nz;
                v1.x = verts[indices[i+1]].x; v1.y = verts[indices[i+1]].y; v1.z = verts[indices[i+1]].z;
                v1.nx = verts[indices[i+1]].nx; v1.ny = verts[indices[i+1]].ny; v1.nz = verts[indices[i+1]].nz;
                v2.x = verts[indices[i+2]].x; v2.y = verts[indices[i+2]].y; v2.z = verts[indices[i+2]].z;
                v2.nx = verts[indices[i+2]].nx; v2.ny = verts[indices[i+2]].ny; v2.nz = verts[indices[i+2]].nz;
                v0.u = 0.0f; v0.v = 0.0f;
                v1.u = 1.0f; v1.v = 0.0f;
                v2.u = 0.5f; v2.v = 1.0f;
                result.vertices = {v0, v1, v2};
                result.indices = {0, 1, 2};
                result.reducedTris = 1;
                result.reductionRatio = 1.0f / (float)result.originalTris;
                break;
            }
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 2. UV UNWRAP — simple planar projection (best-fit axis)
    //    Computes UV coords by projecting onto the dominant plane.
    // ═══════════════════════════════════════════════════════════════════
    static void generatePlanarUV(std::vector<UVVertex>& verts,
                                  const std::vector<uint32_t>& indices) {
        if (verts.empty()) return;

        // Find bounding box extent on each axis
        glm::vec3 bbMin(1e9f), bbMax(-1e9f);
        for (auto& v : verts) {
            bbMin = glm::min(bbMin, glm::vec3(v.x, v.y, v.z));
            bbMax = glm::max(bbMax, glm::vec3(v.x, v.y, v.z));
        }
        glm::vec3 extent = bbMax - bbMin;

        // Pick the two largest axes for UV projection
        // Axis mapping: X→U, Y→V  or  X→U, Z→V  or  Y→U, Z→V
        int axU = 0, axV = 1;
        if (extent.x >= extent.y && extent.x >= extent.z)  { axU = 1; axV = 2; } // project YZ
        else if (extent.y >= extent.x && extent.y >= extent.z) { axU = 0; axV = 2; } // project XZ
        else { axU = 0; axV = 1; } // project XY

        // Remap to [0,1]
        auto getVal = [](const UVVertex& v, int ax) -> float {
            if (ax == 0) return v.x;
            if (ax == 1) return v.y;
            return v.z;
        };

        float uMin = 1e9f, uMax = -1e9f, vMin = 1e9f, vMax = -1e9f;
        for (auto& v : verts) {
            float u = getVal(v, axU);
            float vv = getVal(v, axV);
            uMin = std::min(uMin, u); uMax = std::max(uMax, u);
            vMin = std::min(vMin, vv); vMax = std::max(vMax, vv);
        }
        float uRange = uMax - uMin, vRange = vMax - vMin;
        if (uRange < 1e-6f) uRange = 1.0f;
        if (vRange < 1e-6f) vRange = 1.0f;

        for (auto& v : verts) {
            v.u = (getVal(v, axU) - uMin) / uRange;
            v.v = (getVal(v, axV) - vMin) / vRange;
        }
    }

    // ═══════════════════════════════════════════════════════════════════
    // 3. NORMAL MAP BAKE — high-poly → low-poly normal transfer
    //    For each low-poly texel, find nearest high-poly surface normal.
    //    Returns RGB float buffer (normal map, resolution x resolution).
    // ═══════════════════════════════════════════════════════════════════
    struct NormalMap {
        std::vector<float> data;  // RGB, row-major, resolution×resolution×3
        int resolution;
    };

    static NormalMap bakeNormalMap(const std::vector<UVVertex>& lowVertices,
                                    const std::vector<uint32_t>& lowIndices,
                                    const std::vector<Vertex>& highVertices,
                                    const std::vector<uint32_t>& highIndices,
                                    int resolution = 512) {
        NormalMap nm;
        nm.resolution = resolution;
        nm.data.resize(resolution * resolution * 3, 0.5f); // default: flat (0.5, 0.5, 1.0)

        if (lowVertices.empty() || lowIndices.empty() || highVertices.empty()) return nm;

        // Build high-poly BVH (simple bounding box per triangle for now)
        // For each texel, find the closest triangle in low-poly, then sample high-poly normal

        // Simple approach: for each low-poly triangle, rasterize to texel grid
        for (size_t ti = 0; ti + 2 < lowIndices.size(); ti += 3) {
            const auto& v0 = lowVertices[lowIndices[ti]];
            const auto& v1 = lowVertices[lowIndices[ti+1]];
            const auto& v2 = lowVertices[lowIndices[ti+2]];

            // Triangle bounding box in UV space
            float u0 = v0.u, v0v = v0.v;
            float u1 = v1.u, v1v = v1.v;
            float u2 = v2.u, v2v = v2.v;

            float uMin = std::min({u0, u1, u2});
            float uMax = std::max({u0, u1, u2});
            float vMin = std::min({v0v, v1v, v2v});
            float vMax = std::max({v0v, v1v, v2v});

            int x0 = std::max(0, (int)(uMin * resolution));
            int x1 = std::min(resolution - 1, (int)(uMax * resolution));
            int y0 = std::max(0, (int)(vMin * resolution));
            int y1 = std::min(resolution - 1, (int)(vMax * resolution));

            // Low-poly triangle normal in world space
            glm::vec3 e1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
            glm::vec3 e2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
            glm::vec3 triNormal = glm::normalize(glm::cross(e1, e2));

            // Find closest matching high-poly normal via spatial query
            glm::vec3 triCenter(
                (v0.x + v1.x + v2.x) / 3.0f,
                (v0.y + v1.y + v2.y) / 3.0f,
                (v0.z + v1.z + v2.z) / 3.0f
            );

            // Find nearest vertex in high-poly mesh
            glm::vec3 highNormal(0, 0, 1);
            float bestDist = 1e9f;
            for (size_t hi = 0; hi < highVertices.size(); hi++) {
                glm::vec3 hp(highVertices[hi].x, highVertices[hi].y, highVertices[hi].z);
                float d = glm::distance(triCenter, hp);
                if (d < bestDist) {
                    bestDist = d;
                    highNormal = glm::vec3(highVertices[hi].nx, highVertices[hi].ny, highVertices[hi].nz);
                }
            }

            // Convert high-poly normal to tangent space (relative to low-poly triangle)
            // Simplified: store world-space normal transformed to [0,1]
            glm::vec3 mappedNormal = highNormal * 0.5f + 0.5f;

            // Rasterize to texels
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    float texU = ((float)x + 0.5f) / resolution;
                    float texV = ((float)y + 0.5f) / resolution;

                    // Barycentric test
                    float denom = (v1v - v2v) * (u0 - u2) + (u2 - u1) * (v0v - v2v);
                    if (fabs(denom) < 1e-8f) continue;
                    float w0 = ((v1v - v2v) * (texU - u2) + (u2 - u1) * (texV - v2v)) / denom;
                    float w1 = ((v2v - v0v) * (texU - u2) + (u0 - u2) * (texV - v2v)) / denom;
                    float w2 = 1.0f - w0 - w1;

                    if (w0 >= -0.001f && w1 >= -0.001f && w2 >= -0.001f) {
                        int idx = (y * resolution + x) * 3;
                        nm.data[idx + 0] = mappedNormal.x;
                        nm.data[idx + 1] = mappedNormal.y;
                        nm.data[idx + 2] = mappedNormal.z;
                    }
                }
            }
        }
        return nm;
    }

    // ═══════════════════════════════════════════════════════════════════
    // 4. Recompute normals for a mesh (after decimation)
    // ═══════════════════════════════════════════════════════════════════
    static void recomputeNormals(std::vector<UVVertex>& verts,
                                  const std::vector<uint32_t>& indices) {
        // Reset normals
        for (auto& v : verts) { v.nx = 0; v.ny = 0; v.nz = 0; }

        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            auto& v0 = verts[indices[i]];
            auto& v1 = verts[indices[i+1]];
            auto& v2 = verts[indices[i+2]];

            glm::vec3 e1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
            glm::vec3 e2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
            glm::vec3 n = glm::cross(e1, e2);

            v0.nx += n.x; v0.ny += n.y; v0.nz += n.z;
            v1.nx += n.x; v1.ny += n.y; v1.nz += n.z;
            v2.nx += n.x; v2.ny += n.y; v2.nz += n.z;
        }

        for (auto& v : verts) {
            float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
            if (len > 1e-6f) { v.nx /= len; v.ny /= len; v.nz /= len; }
            else { v.nx = 0; v.ny = 1; v.nz = 0; }
        }
    }
};
