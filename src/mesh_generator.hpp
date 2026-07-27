#pragma once
#include <vector>
#include <array>
#include <cmath>
#include <cstdint>

struct Vertex {
    float x, y, z;
    float nx, ny, nz;  // normal
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

class PrimitiveGenerator {
public:
    // ICO sphere: recursive subdivision of base octahedron
    static Mesh generateIcoSphere(float radius, int subdivisions = 4) {
        Mesh mesh;

        // Start with octahedron (6 vertices)
        std::vector<std::array<float, 3>> verts = {
            {0, 1, 0}, {0, -1, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}
        };
        std::vector<std::array<int, 3>> faces = {
            {0, 4, 2}, {0, 2, 5}, {0, 5, 3}, {0, 3, 4},
            {1, 2, 4}, {1, 5, 2}, {1, 3, 5}, {1, 4, 3}
        };

        // Subdivide
        for (int sub = 0; sub < subdivisions; sub++) {
            std::vector<std::array<int, 3>> new_faces;
            std::vector<std::array<float, 3>> new_verts = verts;

            // Midpoint cache
            std::vector<std::array<int, 3>> edges;
            std::vector<int> edge_midpoint;

            auto get_midpoint = [&](int a, int b) -> int {
                // Find or create midpoint
                for (size_t i = 0; i < edges.size(); i++) {
                    if ((edges[i][0] == a && edges[i][1] == b) ||
                        (edges[i][0] == b && edges[i][1] == a)) {
                        return edge_midpoint[i];
                    }
                }
                edges.push_back({a, b});
                std::array<float, 3> mid = {
                    (verts[a][0] + verts[b][0]) / 2.0f,
                    (verts[a][1] + verts[b][1]) / 2.0f,
                    (verts[a][2] + verts[b][2]) / 2.0f
                };
                // Normalize to sphere
                float len = std::sqrt(mid[0]*mid[0] + mid[1]*mid[1] + mid[2]*mid[2]);
                if (len > 0) {
                    mid[0] /= len; mid[1] /= len; mid[2] /= len;
                }
                edge_midpoint.push_back(new_verts.size());
                new_verts.push_back(mid);
                return new_verts.size() - 1;
            };

            for (const auto& face : faces) {
                int a = get_midpoint(face[0], face[1]);
                int b = get_midpoint(face[1], face[2]);
                int c = get_midpoint(face[2], face[0]);

                new_faces.push_back({face[0], a, c});
                new_faces.push_back({face[1], b, a});
                new_faces.push_back({face[2], c, b});
                new_faces.push_back({a, b, c});
            }

            verts = new_verts;
            faces = new_faces;
        }

        // Build mesh
        for (const auto& v : verts) {
            float len = std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
            Vertex vert;
            vert.x = v[0] * radius;
            vert.y = v[1] * radius;
            vert.z = v[2] * radius;
            vert.nx = v[0] / len;
            vert.ny = v[1] / len;
            vert.nz = v[2] / len;
            mesh.vertices.push_back(vert);
        }

        for (const auto& f : faces) {
            mesh.indices.push_back(f[0]);
            mesh.indices.push_back(f[1]);
            mesh.indices.push_back(f[2]);
        }

        return mesh;
    }

    // Box: centered at origin
    static Mesh generateBox(float size) {
        Mesh mesh;
        float s = size / 2.0f;

        // 8 vertices
        std::vector<std::array<float, 3>> verts = {
            {-s,-s,-s}, {s,-s,-s}, {s,s,-s}, {-s,s,-s},
            {-s,-s,s}, {s,-s,s}, {s,s,s}, {-s,s,s}
        };

        // 6 faces (2 triangles each)
        std::vector<std::array<int, 3>> faces = {
            {0,1,2}, {0,2,3},  // -Z
            {4,6,5}, {4,7,6},  // +Z
            {0,3,7}, {0,7,4},  // -X
            {1,5,6}, {1,6,2},  // +X
            {3,2,6}, {3,6,7},  // +Y
            {0,4,5}, {0,5,1}   // -Y
        };

        for (const auto& v : verts) {
            Vertex vert;
            vert.x = v[0]; vert.y = v[1]; vert.z = v[2];
            vert.nx = 0; vert.ny = 0; vert.nz = 0;
            mesh.vertices.push_back(vert);
        }

        // Compute normals
        for (const auto& f : faces) {
            mesh.indices.push_back(f[0]);
            mesh.indices.push_back(f[1]);
            mesh.indices.push_back(f[2]);
        }

        return mesh;
    }

    // Cylinder: along Y axis
    static Mesh generateCylinder(float radius, float height, int segments = 32) {
        Mesh mesh;
        float half_h = height / 2.0f;

        // Vertices: top ring, bottom ring, center top, center bottom
        for (int i = 0; i < segments; i++) {
            float angle = 2.0f * M_PI * i / segments;
            float x = std::cos(angle) * radius;
            float z = std::sin(angle) * radius;

            // Top
            Vertex v;
            v.x = x; v.y = half_h; v.z = z;
            v.nx = x / radius; v.ny = 0; v.nz = z / radius;
            mesh.vertices.push_back(v);

            // Bottom
            v.y = -half_h;
            v.nx = x / radius; v.ny = 0; v.nz = z / radius;
            mesh.vertices.push_back(v);
        }

        // Center caps
        Vertex top_cap = {0, half_h, 0, 0, 1, 0};
        Vertex bot_cap = {0, -half_h, 0, 0, -1, 0};
        mesh.vertices.push_back(top_cap);
        mesh.vertices.push_back(bot_cap);

        int top_center = mesh.vertices.size() - 2;
        int bot_center = mesh.vertices.size() - 1;

        // Side faces
        for (int i = 0; i < segments; i++) {
            int i0 = i * 2;
            int i1 = ((i + 1) % segments) * 2;
            int i2 = i1 + 1;
            int i3 = i0 + 1;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i3);
            mesh.indices.push_back(i2);
        }

        // Top cap
        for (int i = 0; i < segments; i++) {
            int i0 = i * 2;
            int i1 = ((i + 1) % segments) * 2;
            mesh.indices.push_back(top_center);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i0);
        }

        // Bottom cap
        for (int i = 0; i < segments; i++) {
            int i0 = i * 2;
            int i1 = ((i + 1) % segments) * 2;
            mesh.indices.push_back(bot_center);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
        }

        return mesh;
    }

    // Cone: apex at top, base at bottom
    static Mesh generateCone(float radius, float height, int segments = 32) {
        Mesh mesh;
        float half_h = height / 2.0f;

        // Base ring vertices
        for (int i = 0; i < segments; i++) {
            float angle = 2.0f * M_PI * i / segments;
            float x = std::cos(angle) * radius;
            float z = std::sin(angle) * radius;

            Vertex v;
            v.x = x; v.y = -half_h; v.z = z;
            // Normal: angle between base and apex
            float nx = x / radius;
            float nz = z / radius;
            float ny = radius / height;  // slope
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            v.nx = nx / len; v.ny = ny / len; v.nz = nz / len;
            mesh.vertices.push_back(v);
        }

        // Apex
        Vertex apex = {0, half_h, 0, 0, 1, 0};
        mesh.vertices.push_back(apex);
        int apex_idx = mesh.vertices.size() - 1;

        // Center bottom
        Vertex bot_center = {0, -half_h, 0, 0, -1, 0};
        mesh.vertices.push_back(bot_center);
        int bot_center_idx = mesh.vertices.size() - 1;

        // Side faces
        for (int i = 0; i < segments; i++) {
            int i0 = i;
            int i1 = (i + 1) % segments;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(apex_idx);
        }

        // Bottom cap
        for (int i = 0; i < segments; i++) {
            int i0 = i;
            int i1 = (i + 1) % segments;
            mesh.indices.push_back(bot_center_idx);
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
        }

        return mesh;
    }

    // Tube: cylinder shell (for free curves)
    static Mesh generateTube(float radius, float length, int segments = 32) {
        return generateCylinder(radius, length, segments);
    }
};
