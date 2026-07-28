#pragma once
// ─── ImageMaker M7: Expression Template System ─────────────────────────
// Standard face base mesh + 6 basic emotion expression deltas (blend shapes)
// Save/Load: JSON catalog + binary .delta files
// Apply: base + Σ(weight_i * delta_i) → morphed mesh

#include "mesh_generator.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <sys/stat.h>
#include <sys/types.h>

// ─── Blend shape delta: per-vertex position offset ─────────────────────
struct BlendDelta {
    std::vector<glm::vec3> offsets; // one per vertex of the base mesh
    std::string name;
};

// ─── Expression template catalog entry ──────────────────────────────────
struct TemplateEntry {
    std::string name;
    std::string file;     // relative to templates/expressions/
    uint32_t vertexCount;
};

// ═══════════════════════════════════════════════════════════════════════
// ExpressionTemplateSystem
// ═══════════════════════════════════════════════════════════════════════
class ExpressionTemplateSystem {
public:
    static constexpr int NUM_EXPRESSIONS = 7; // neutral + 6 emotions

    // ─── 1. Generate standard face base mesh ──────────────────────────
    //
    // Creates a parametric face using a deformed ico-sphere:
    //   - Start with ico-sphere (subdiv 3 = 258 verts, enough for expressions)
    //   - Apply face-like deformation: wider cheeks, pointed chin, brow ridge
    //   - The topology is consistent → blend shapes work
    //
    static Mesh generateFaceBase(float size = 1.0f) {
        Mesh base = PrimitiveGenerator::generateIcoSphere(size, 3);

        // Face deformation: stretch along X (width), compress along Y (height),
        // pointed chin at -Y, brow ridge at +Y
        for (auto& v : base.vertices) {
            float x = v.x, y = v.y, z = v.z;

            // Widen cheeks (XY plane adjustment)
            float cheekFactor = 1.0f - (y * y) * 0.25f; // wider at equator
            v.x *= (1.0f + cheekFactor * 0.15f);
            v.z *= (1.0f + cheekFactor * 0.10f);

            // Pointed chin (lower -Y region gets compressed)
            if (y < -0.3f) {
                float chinT = (y + 1.0f) / 0.7f; // 0 at y=-1, 1 at y=-0.3
                chinT = 1.0f - chinT;             // 1 at bottom, 0 at -0.3
                float squeeze = 1.0f - chinT * 0.5f;
                v.x *= squeeze;
                v.z *= squeeze;
                v.y *= (1.0f - chinT * 0.3f); // slightly flatter chin
            }

            // Brow ridge (upper region slight protrusion forward)
            if (y > 0.2f && y < 0.7f) {
                float browT = (y - 0.2f) / 0.5f;
                browT = browT * (1.0f - browT) * 4.0f; // bell curve 0→1→0
                v.z += browT * 0.08f * size;
                v.x *= (1.0f - browT * 0.05f); // slight narrowing at brow
            }

            // Slightly flatten the back of head
            if (z < -0.3f) {
                float backT = (-z - 0.3f) / 0.7f;
                v.z += backT * 0.12f * size;
            }

            // Re-normalize to preserve approximate size
            float len = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
            if (len > 0.001f) {
                float targetLen = size * (0.9f + 0.1f * (1.0f - fabs(y))); // slightly ellipsoid
                float scale = targetLen / len;
                v.x *= scale;
                v.y *= scale;
                v.z *= scale;
            }
        }

        // Recompute normals
        recomputeNormals(base);
        return base;
    }

    // ─── 2. Generate expression deltas ─────────────────────────────────
    //
    // Each expression is defined as per-vertex displacement from neutral.
    // Uses region-based deformation: vertices near face landmarks are moved.
    //
    // Landmark regions (in local space, assuming Y=up, Z=forward):
    //   eyes:     y≈0.3~0.5, |x|≈0.25~0.45, z≈0.6~0.85
    //   eyebrows: y≈0.45~0.65, |x|≈0.2~0.5, z≈0.55~0.8
    //   mouth:    y≈-0.3~0.0, |x|≈0.0~0.35, z≈0.6~0.85
    //   cheeks:   y≈-0.1~0.25, |x|≈0.3~0.55, z≈0.4~0.7
    //   nose:     y≈0.0~0.3, |x|≈0.0~0.15, z≈0.75~0.9
    //   jaw:      y≈-0.6~-0.3, |x|≈0.1~0.5, z≈0.5~0.8

    enum Region {
        REGION_EYES, REGION_EYEBROWS, REGION_MOUTH,
        REGION_CHEEKS, REGION_NOSE, REGION_JAW, REGION_COUNT
    };

    static float regionWeight(const Vertex& v, Region r) {
        float x = v.x, y = v.y, z = v.z;
        float absX = fabs(x);

        auto smooth = [](float val, float lo, float hi) -> float {
            if (val <= lo) return 0.0f;
            if (val >= hi) return 1.0f;
            float t = (val - lo) / (hi - lo);
            return t * t * (3.0f - 2.0f * t); // smoothstep
        };

        auto bell = [&](float val, float lo, float peak, float hi) -> float {
            if (val <= lo || val >= hi) return 0.0f;
            if (val <= peak) return smooth(val, lo, peak);
            return 1.0f - smooth(val, peak, hi);
        };

        switch (r) {
        case REGION_EYES:
            return bell(y, 0.25f, 0.38f, 0.50f) *
                   bell(absX, 0.20f, 0.33f, 0.48f) *
                   smooth(z, 0.50f, 0.65f); // forward-facing

        case REGION_EYEBROWS:
            return bell(y, 0.42f, 0.52f, 0.62f) *
                   bell(absX, 0.18f, 0.35f, 0.50f) *
                   smooth(z, 0.45f, 0.60f);

        case REGION_MOUTH:
            return bell(y, -0.35f, -0.15f, 0.05f) *
                   bell(absX, 0.0f, 0.18f, 0.38f) *
                   smooth(z, 0.55f, 0.70f);

        case REGION_CHEEKS:
            return bell(y, -0.15f, 0.08f, 0.30f) *
                   bell(absX, 0.28f, 0.42f, 0.58f) *
                   smooth(z, 0.35f, 0.55f);

        case REGION_NOSE:
            return bell(y, -0.05f, 0.12f, 0.28f) *
                   smooth(0.0f - absX, -0.18f, -0.02f) * // close to center X
                   smooth(z, 0.60f, 0.75f);

        case REGION_JAW:
            return bell(y, -0.65f, -0.48f, -0.28f) *
                   bell(absX, 0.08f, 0.30f, 0.52f) *
                   smooth(z, 0.40f, 0.60f);

        default: return 0.0f;
        }
    }

    // ─── Expression generators ─────────────────────────────────────────
    struct ExprConfig {
        float eye_close;       // 0=none, 1=fully closed
        float brow_raise;      // positive=up, negative=down
        float brow_inner_up;   // inner brow up (sadness)
        float mouth_corner_up; // positive=smile, negative=frown
        float mouth_open;      // 0=closed, 1=open wide
        float mouth_stretch;   // horizontal stretch
        float cheek_raise;     // cheek puff up
        float nose_wrinkle;    // nose scrunch
        float jaw_drop;        // jaw lowered
    };

    static BlendDelta generateExpression(const Mesh& base, const ExprConfig& cfg, const std::string& name) {
        BlendDelta delta;
        delta.name = name;
        delta.offsets.resize(base.vertices.size(), glm::vec3(0.0f));

        float strength = 0.08f; // base displacement magnitude

        for (size_t i = 0; i < base.vertices.size(); i++) {
            const auto& v = base.vertices[i];
            float x = v.x, y = v.y, z = v.z;
            float signX = (x >= 0) ? 1.0f : -1.0f;
            glm::vec3 d(0.0f);

            float we = regionWeight(v, REGION_EYES);
            float wb = regionWeight(v, REGION_EYEBROWS);
            float wm = regionWeight(v, REGION_MOUTH);
            float wc = regionWeight(v, REGION_CHEEKS);
            float wn = regionWeight(v, REGION_NOSE);
            float wj = regionWeight(v, REGION_JAW);

            // Eyebrows
            d.y += wb * cfg.brow_raise * strength * 8.0f;
            d.y += wb * cfg.brow_inner_up * strength * 5.0f * (1.0f - fabs(x) * 2.0f); // inner more

            // Eyes — close by moving upper lid down, lower lid up
            if (cfg.eye_close > 0.0f) {
                float lid = we;
                // upper lid (y > eye center): move down
                if (y > 0.35f) {
                    float upperBias = (y - 0.35f) / 0.15f;
                    if (upperBias > 1.0f) upperBias = 1.0f;
                    d.y -= cfg.eye_close * strength * 4.0f * lid * upperBias;
                }
                // lower lid (y < eye center): move up
                if (y < 0.35f && y > 0.22f) {
                    float lowerBias = (0.35f - y) / 0.13f;
                    if (lowerBias > 1.0f) lowerBias = 1.0f;
                    d.y += cfg.eye_close * strength * 3.0f * lid * lowerBias;
                }
            }

            // Mouth corners up/down
            float mouthCorner = wm * (fabs(x) / 0.35f); // stronger at corners
            if (mouthCorner > 1.0f) mouthCorner = 1.0f;
            d.y += mouthCorner * cfg.mouth_corner_up * strength * 6.0f;

            // Mouth open (move jaw down, lower lip vertices down)
            if (cfg.mouth_open > 0.0f) {
                // Lower lip+chin area moves down
                float lowerMouth = wm * (y < -0.1f ? 1.0f : smoothstep(y, -0.1f, 0.0f));
                d.y -= cfg.mouth_open * strength * 6.0f * lowerMouth;
                d.y -= cfg.mouth_open * strength * 4.0f * wj;

                // Upper lip opens by rotating outward
                if (y > -0.05f && y < 0.1f && wm > 0.3f) {
                    d.z -= cfg.mouth_open * strength * 3.0f * wm;
                    d.y += cfg.mouth_open * strength * 2.0f * wm;
                }
            }

            // Mouth horizontal stretch
            d.x += signX * wm * cfg.mouth_stretch * strength * 4.0f * fabs(x) * 2.0f;

            // Cheek raise (smile pushes cheeks up)
            d.y += wc * cfg.cheek_raise * strength * 5.0f;
            d.z += wc * cfg.cheek_raise * strength * 2.0f; // pushes forward slightly

            // Nose wrinkle
            d.y += wn * cfg.nose_wrinkle * strength * 3.0f;
            d.z -= wn * cfg.nose_wrinkle * strength * 1.5f;

            // Jaw drop
            d.y -= wj * cfg.jaw_drop * strength * 8.0f;

            delta.offsets[i] = d;
        }

        return delta;
    }

    // ─── 6 standard expressions ────────────────────────────────────────
    static BlendDelta joy(const Mesh& base) {
        return generateExpression(base, {
            0.4f,   // eye_close (squint)
            0.0f,   // brow_raise
            0.0f,   // brow_inner_up
            0.8f,   // mouth_corner_up (big smile)
            0.05f,  // mouth_open (slight)
            0.4f,   // mouth_stretch
            0.6f,   // cheek_raise
            0.0f,   // nose_wrinkle
            0.0f    // jaw_drop
        }, "joy");
    }

    static BlendDelta anger(const Mesh& base) {
        return generateExpression(base, {
            0.3f,   // eye_close (narrowed)
            -0.7f,  // brow_raise (lowered/furrowed)
            0.0f,   // brow_inner_up
            -0.5f,  // mouth_corner_up (frown)
            0.0f,   // mouth_open
            -0.2f,  // mouth_stretch (tight)
            0.0f,   // cheek_raise
            0.3f,   // nose_wrinkle
            0.1f    // jaw_drop (tightened, slightly forward)
        }, "anger");
    }

    static BlendDelta sadness(const Mesh& base) {
        return generateExpression(base, {
            0.3f,   // eye_close (droopy)
            0.0f,   // brow_raise
            0.6f,   // brow_inner_up (inner brows raised)
            -0.6f,  // mouth_corner_up (frown)
            0.02f,  // mouth_open (slight)
            -0.1f,  // mouth_stretch (slight narrow)
            0.0f,   // cheek_raise
            -0.1f,  // nose_wrinkle
            0.0f    // jaw_drop
        }, "sadness");
    }

    static BlendDelta surprise(const Mesh& base) {
        return generateExpression(base, {
            -0.6f,  // eye_close (wide open — negative = push lids apart)
            0.8f,   // brow_raise (raised high)
            0.0f,   // brow_inner_up
            0.1f,   // mouth_corner_up
            0.7f,   // mouth_open (wide O)
            0.2f,   // mouth_stretch
            0.0f,   // cheek_raise
            0.0f,   // nose_wrinkle
            0.4f    // jaw_drop
        }, "surprise");
    }

    static BlendDelta fear(const Mesh& base) {
        return generateExpression(base, {
            -0.5f,  // eye_close (wide open)
            0.6f,   // brow_raise (raised)
            0.4f,   // brow_inner_up (inner up = worried)
            -0.1f,  // mouth_corner_up
            0.5f,   // mouth_open (gasp)
            0.4f,   // mouth_stretch (horizontal stretch — grimace)
            0.0f,   // cheek_raise
            0.0f,   // nose_wrinkle
            0.3f    // jaw_drop
        }, "fear");
    }

    static BlendDelta disgust(const Mesh& base) {
        return generateExpression(base, {
            0.5f,   // eye_close (squint)
            -0.3f,  // brow_raise (slight lower)
            0.0f,   // brow_inner_up
            -0.3f,  // mouth_corner_up (slight frown)
            0.05f,  // mouth_open
            -0.3f,  // mouth_stretch
            0.0f,   // cheek_raise
            0.7f,   // nose_wrinkle (strong scrunch)
            0.0f    // jaw_drop
        }, "disgust");
    }

    // ─── 3. Apply expression to mesh ───────────────────────────────────
    static void applyExpression(std::vector<Vertex>& vertices,
                                 const BlendDelta& delta,
                                 float weight = 1.0f) {
        if (delta.offsets.size() != vertices.size()) {
            fprintf(stderr, "Expression apply: vertex count mismatch (%zu vs %zu)\n",
                    delta.offsets.size(), vertices.size());
            return;
        }
        for (size_t i = 0; i < vertices.size(); i++) {
            vertices[i].x += delta.offsets[i].x * weight;
            vertices[i].y += delta.offsets[i].y * weight;
            vertices[i].z += delta.offsets[i].z * weight;
        }
    }

    // ─── 4. Save/Load ─────────────────────────────────────────────────
    static bool saveBase(const Mesh& base, const std::string& path) {
        ensureDir(path);
        std::ofstream out(path, std::ios::binary);
        if (!out) { fprintf(stderr, "saveBase: cannot open %s\n", path.c_str()); return false; }

        uint32_t vc = (uint32_t)base.vertices.size();
        uint32_t ic = (uint32_t)base.indices.size();
        writeU32(out, vc);
        writeU32(out, ic);

        for (auto& v : base.vertices) {
            writeF32(out, v.x); writeF32(out, v.y); writeF32(out, v.z);
            writeF32(out, v.nx); writeF32(out, v.ny); writeF32(out, v.nz);
        }
        for (auto idx : base.indices) {
            writeU32(out, idx);
        }
        out.close();
        printf("Saved base mesh: %s (%u verts, %u tris)\n", path.c_str(), vc, ic/3);
        return true;
    }

    static Mesh loadBase(const std::string& path) {
        Mesh m;
        std::ifstream in(path, std::ios::binary);
        if (!in) { fprintf(stderr, "loadBase: cannot open %s\n", path.c_str()); return m; }

        uint32_t vc = readU32(in), ic = readU32(in);
        m.vertices.resize(vc);
        m.indices.resize(ic);

        for (auto& v : m.vertices) {
            v.x = readF32(in); v.y = readF32(in); v.z = readF32(in);
            v.nx = readF32(in); v.ny = readF32(in); v.nz = readF32(in);
        }
        for (auto& idx : m.indices) {
            idx = readU32(in);
        }
        in.close();
        printf("Loaded base mesh: %s (%u verts, %u tris)\n", path.c_str(), vc, ic/3);
        return m;
    }

    static bool saveDelta(const BlendDelta& delta, const std::string& path) {
        ensureDir(path);
        std::ofstream out(path, std::ios::binary);
        if (!out) { fprintf(stderr, "saveDelta: cannot open %s\n", path.c_str()); return false; }

        uint32_t vc = (uint32_t)delta.offsets.size();
        writeU32(out, vc);
        // Write name as length-prefixed string
        uint32_t nameLen = (uint32_t)delta.name.size();
        writeU32(out, nameLen);
        out.write(delta.name.data(), nameLen);

        for (auto& off : delta.offsets) {
            writeF32(out, off.x);
            writeF32(out, off.y);
            writeF32(out, off.z);
        }
        out.close();
        printf("Saved delta: %s (%u verts)\n", path.c_str(), vc);
        return true;
    }

    static BlendDelta loadDelta(const std::string& path) {
        BlendDelta delta;
        std::ifstream in(path, std::ios::binary);
        if (!in) { fprintf(stderr, "loadDelta: cannot open %s\n", path.c_str()); return delta; }

        uint32_t vc = readU32(in);
        uint32_t nameLen = readU32(in);
        delta.name.resize(nameLen);
        in.read(&delta.name[0], nameLen);

        delta.offsets.resize(vc);
        for (auto& off : delta.offsets) {
            off.x = readF32(in);
            off.y = readF32(in);
            off.z = readF32(in);
        }
        in.close();
        printf("Loaded delta: %s '%s' (%u verts)\n", path.c_str(), delta.name.c_str(), vc);
        return delta;
    }

    // ─── 5. Catalog (JSON) ─────────────────────────────────────────────
    static bool saveCatalog(const std::string& catalogDir,
                             const std::string& baseRelPath,
                             const std::vector<BlendDelta>& deltas) {
        ensureDir(catalogDir);
        std::string catalogPath = catalogDir + "/catalog.json";
        std::ofstream out(catalogPath);
        if (!out) return false;

        out << "{\n";
        out << "  \"base\": \"" << baseRelPath << "\",\n";
        out << "  \"generator\": \"ImageMaker M7\",\n";
        out << "  \"expressions\": [\n";
        for (size_t i = 0; i < deltas.size(); i++) {
            out << "    {\"name\":\"" << deltas[i].name
                << "\",\"file\":\"expressions/" << deltas[i].name << ".delta\""
                << ",\"vertexCount\":" << deltas[i].offsets.size() << "}";
            if (i + 1 < deltas.size()) out << ",";
            out << "\n";
        }
        out << "  ]\n}\n";
        out.close();
        printf("Saved catalog: %s\n", catalogPath.c_str());
        return true;
    }

    // ─── 6. Initialize all templates ───────────────────────────────────
    static bool initializeTemplates(const std::string& templateDir = "templates") {
        printf("=== M7: Initializing expression templates ===\n");

        // Generate face base
        Mesh base = generateFaceBase(1.0f);
        std::string basePath = templateDir + "/standard_face_base.mesh";
        if (!saveBase(base, basePath)) return false;

        // Generate 6 expressions
        BlendDelta deltas[NUM_EXPRESSIONS - 1] = {
            joy(base), anger(base), sadness(base),
            surprise(base), fear(base), disgust(base)
        };

        std::vector<BlendDelta> deltaVec;
        for (auto& d : deltas) {
            std::string dpath = templateDir + "/expressions/" + d.name + ".delta";
            if (!saveDelta(d, dpath)) return false;
            deltaVec.push_back(d);
        }

        // Save catalog
        if (!saveCatalog(templateDir, "standard_face_base.mesh", deltaVec)) return false;

        printf("=== M7: Templates initialized (%zu expressions) ===\n", deltaVec.size());
        return true;
    }

    // ─── 7. Build neutral mesh from base (with recomputed normals) ─────
    static Mesh neutralFromBase(const Mesh& base) {
        Mesh result = base; // copy
        recomputeNormals(result);
        return result;
    }

    // ─── 8. Blend multiple expressions ─────────────────────────────────
    // weights: array of 6 floats (joy, anger, sadness, surprise, fear, disgust)
    static Mesh applyMultiExpression(const Mesh& base,
                                      const BlendDelta deltas[6],
                                      const float weights[6]) {
        Mesh result = base;
        for (int i = 0; i < 6; i++) {
            if (fabs(weights[i]) > 0.001f) {
                applyExpression(result.vertices, deltas[i], weights[i]);
            }
        }
        recomputeNormals(result);
        return result;
    }

private:
    static void ensureDir(const std::string& path) {
        // Extract directory part
        size_t pos = path.find_last_of("/\\");
        if (pos != std::string::npos) {
            std::string dir = path.substr(0, pos);
            mkdir(dir.c_str(), 0755);
        }
    }

    static void mkdir(const char* path, mode_t mode) {
        struct stat st;
        if (::stat(path, &st) != 0) {
            // Simplified recursive mkdir
            std::string p(path);
            for (size_t i = 1; i < p.size(); i++) {
                if (p[i] == '/') {
                    p[i] = '\0';
                    ::mkdir(p.c_str(), mode);
                    p[i] = '/';
                }
            }
            ::mkdir(p.c_str(), mode);
        }
    }

    static void writeF32(std::ofstream& out, float v) {
        out.write(reinterpret_cast<const char*>(&v), 4);
    }

    static void writeU32(std::ofstream& out, uint32_t v) {
        out.write(reinterpret_cast<const char*>(&v), 4);
    }

    static float readF32(std::ifstream& in) {
        float v;
        in.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    static uint32_t readU32(std::ifstream& in) {
        uint32_t v;
        in.read(reinterpret_cast<char*>(&v), 4);
        return v;
    }

    static void recomputeNormals(Mesh& m) {
        // Reset normals
        for (auto& v : m.vertices) { v.nx = 0; v.ny = 0; v.nz = 0; }

        for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
            auto& v0 = m.vertices[m.indices[i]];
            auto& v1 = m.vertices[m.indices[i+1]];
            auto& v2 = m.vertices[m.indices[i+2]];

            glm::vec3 e1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
            glm::vec3 e2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);
            glm::vec3 n = glm::cross(e1, e2);

            v0.nx += n.x; v0.ny += n.y; v0.nz += n.z;
            v1.nx += n.x; v1.ny += n.y; v1.nz += n.z;
            v2.nx += n.x; v2.ny += n.y; v2.nz += n.z;
        }

        for (auto& v : m.vertices) {
            float len = std::sqrt(v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
            if (len > 1e-6f) { v.nx /= len; v.ny /= len; v.nz /= len; }
            else { v.nx = 0; v.ny = 1.0f; v.nz = 0; }
        }
    }

    static float smoothstep(float edge0, float edge1, float x) {
        float t = std::max(0.0f, std::min(1.0f, (x - edge0) / (edge1 - edge0)));
        return t * t * (3.0f - 2.0f * t);
    }
};
