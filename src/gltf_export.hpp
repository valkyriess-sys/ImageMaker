#pragma once
// ─── ImageMaker M7: glTF 2.0 Binary (.glb) Export ───────────────────
// Simple standalone writer — no external dependencies.
// Binary glTF format: 12-byte header + JSON chunk + BIN chunk
//
// Layout:
//   uint32 magic    = 0x46546C67  ("glTF")
//   uint32 version  = 2
//   uint32 length    = total file size
//   uint32 chunkLen  = JSON chunk length
//   uint32 chunkType = 0x4E4F534A  ("JSON")
//   <JSON data>
//   uint32 chunkLen  = BIN chunk length
//   uint32 chunkType = 0x004E4942  ("BIN\0")
//   <binary data>
//
// M7: Added morph target (blend shape) export support.

#include "mesh_postprocess.hpp"
#include "expression_template.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <iomanip>

class GLTFExporter {
public:
    // ─── Export decimated mesh as .glb ────────────────────────────────
    static bool exportGLB(const std::string& path,
                           const std::vector<UVVertex>& vertices,
                           const std::vector<uint32_t>& indices,
                           const glm::vec4& baseColor = glm::vec4(0.4f, 0.6f, 0.9f, 1.0f)) {
        if (vertices.empty() || indices.empty()) {
            fprintf(stderr, "glTF export: empty mesh\n");
            return false;
        }

        // 1. Build binary buffer (interleaved: pos[3]f + normal[3]f + uv[2]f = 8 floats per vertex)
        //    plus index buffer (uint32 per index)
        std::vector<float> vbuf;
        vbuf.reserve(vertices.size() * 8);
        for (auto& v : vertices) {
            vbuf.push_back(v.x); vbuf.push_back(v.y); vbuf.push_back(v.z);
            vbuf.push_back(v.nx); vbuf.push_back(v.ny); vbuf.push_back(v.nz);
            vbuf.push_back(v.u); vbuf.push_back(v.v);
        }

        uint32_t vertexByteOffset = 0;
        uint32_t vertexByteLength = (uint32_t)(vbuf.size() * sizeof(float));
        uint32_t indexByteOffset = vertexByteLength;
        // Pad index buffer to 4-byte alignment (already aligned since vbuf is float*4)
        uint32_t indexByteLength = (uint32_t)(indices.size() * sizeof(uint32_t));
        uint32_t totalBinSize = indexByteOffset + indexByteLength;

        // 2. Build JSON
        uint32_t vertexCount = (uint32_t)vertices.size();
        uint32_t indexCount = (uint32_t)indices.size();
        uint32_t triCount = indexCount / 3;

        std::ostringstream json;
        json << "{";
        json << "\"asset\":{\"version\":\"2.0\",\"generator\":\"ImageMaker M5\"},";
        json << "\"scene\":0,";
        json << "\"scenes\":[{\"nodes\":[0]}],";
        json << "\"nodes\":[{\"mesh\":0}],";
        json << "\"meshes\":[{";
        json << "\"primitives\":[{";
        json << "\"attributes\":{";
        json << "\"POSITION\":0,";
        json << "\"NORMAL\":1,";
        json << "\"TEXCOORD_0\":2";
        json << "},";
        json << "\"indices\":3,";
        json << "\"material\":0";
        json << "}],";
        json << "\"name\":\"ImageMaker_Mesh\"";
        json << "}],";
        // M5: material with base color
        json << "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":["
             << baseColor.r << "," << baseColor.g << "," << baseColor.b << "," << baseColor.a
             << "]}}],";
        json << "\"accessors\":[";

        // POSITION accessor
        json << "{";
        json << "\"bufferView\":0,";
        json << "\"componentType\":5126,";       // FLOAT
        json << "\"count\":" << vertexCount << ",";
        json << "\"type\":\"VEC3\",";
        json << "\"max\":[" << getMaxPos(vertices) << "],";
        json << "\"min\":[" << getMinPos(vertices) << "]";
        json << "},";

        // NORMAL accessor
        json << "{";
        json << "\"bufferView\":1,";
        json << "\"componentType\":5126,";
        json << "\"count\":" << vertexCount << ",";
        json << "\"type\":\"VEC3\"";
        json << "},";

        // TEXCOORD_0 accessor
        json << "{";
        json << "\"bufferView\":2,";
        json << "\"componentType\":5126,";
        json << "\"count\":" << vertexCount << ",";
        json << "\"type\":\"VEC2\"";
        json << "},";

        // INDICES accessor
        json << "{";
        json << "\"bufferView\":3,";
        json << "\"componentType\":5125,";       // UNSIGNED_INT
        json << "\"count\":" << indexCount << ",";
        json << "\"type\":\"SCALAR\"";
        json << "}";

        json << "],";

        // bufferViews
        json << "\"bufferViews\":[";
        // 0: POSITION
        json << "{\"buffer\":0,\"byteOffset\":" << vertexByteOffset
             << ",\"byteLength\":" << vertexByteLength << ",\"byteStride\":32},";
        // 1: NORMAL
        json << "{\"buffer\":0,\"byteOffset\":" << (vertexByteOffset + 12)
             << ",\"byteLength\":" << vertexByteLength << ",\"byteStride\":32},";
        // 2: TEXCOORD_0
        json << "{\"buffer\":0,\"byteOffset\":" << (vertexByteOffset + 24)
             << ",\"byteLength\":" << vertexByteLength << ",\"byteStride\":32},";
        // 3: INDICES
        json << "{\"buffer\":0,\"byteOffset\":" << indexByteOffset
             << ",\"byteLength\":" << indexByteLength << "}";
        json << "],";

        // buffers
        json << "\"buffers\":[{\"byteLength\":" << totalBinSize << "}]";
        json << "}";

        std::string jsonStr = json.str();

        // 3. Align JSON chunk to 4 bytes (pad with spaces — valid JSON whitespace)
        while (jsonStr.size() % 4 != 0) jsonStr += ' ';

        // 4. Write .glb file
        std::ofstream out(path, std::ios::binary);
        if (!out) {
            fprintf(stderr, "glTF export: cannot open %s\n", path.c_str());
            return false;
        }

        uint32_t magic = 0x46546C67;     // "glTF" little-endian
        uint32_t version = 2;
        uint32_t jsonChunkType = 0x4E4F534A; // "JSON"
        uint32_t binChunkType = 0x004E4942;  // "BIN\0"

        uint32_t headerSize = 12;
        uint32_t jsonChunkHeader = 8;
        uint32_t jsonPaddedLen = (uint32_t)jsonStr.size();
        uint32_t binChunkHeader = 8;
        uint32_t totalLength = headerSize + jsonChunkHeader + jsonPaddedLen + binChunkHeader + totalBinSize;

        // Header
        writeU32(out, magic);
        writeU32(out, version);
        writeU32(out, totalLength);

        // JSON chunk
        writeU32(out, jsonPaddedLen);
        writeU32(out, jsonChunkType);
        out.write(jsonStr.data(), jsonStr.size());

        // BIN chunk
        writeU32(out, totalBinSize);
        writeU32(out, binChunkType);
        // Vertex data
        out.write(reinterpret_cast<const char*>(vbuf.data()), vertexByteLength);
        // Index data
        out.write(reinterpret_cast<const char*>(indices.data()), indexByteLength);

        out.close();

        printf("glTF exported: %s\n", path.c_str());
        printf("  Vertices: %u, Triangles: %u\n", vertexCount, triCount);
        printf("  File size: %u bytes\n", totalLength);

        return true;
    }

    // ─── Export original mesh (no UV) as .glb ────────────────────────
    static bool exportOriginalGLB(const std::string& path,
                                   const std::vector<Vertex>& vertices,
                                   const std::vector<uint32_t>& indices,
                                   const glm::vec4& baseColor = glm::vec4(0.4f, 0.6f, 0.9f, 1.0f)) {
        if (vertices.empty() || indices.empty()) {
            fprintf(stderr, "glTF export: empty mesh\n");
            return false;
        }

        // Build interleaved buffer: pos[3]f + normal[3]f = 6 floats per vertex
        std::vector<float> vbuf;
        vbuf.reserve(vertices.size() * 6);
        for (auto& v : vertices) {
            vbuf.push_back(v.x); vbuf.push_back(v.y); vbuf.push_back(v.z);
            vbuf.push_back(v.nx); vbuf.push_back(v.ny); vbuf.push_back(v.nz);
        }

        uint32_t vertexByteOffset = 0;
        uint32_t vertexByteLength = (uint32_t)(vbuf.size() * sizeof(float));
        uint32_t indexByteOffset = vertexByteLength;
        uint32_t indexByteLength = (uint32_t)(indices.size() * sizeof(uint32_t));
        uint32_t totalBinSize = indexByteOffset + indexByteLength;

        uint32_t vertexCount = (uint32_t)vertices.size();
        uint32_t indexCount = (uint32_t)indices.size();
        uint32_t triCount = indexCount / 3;

        std::ostringstream json;
        json << "{";
        json << "\"asset\":{\"version\":\"2.0\",\"generator\":\"ImageMaker M5\"},";
        json << "\"scene\":0,";
        json << "\"scenes\":[{\"nodes\":[0]}],";
        json << "\"nodes\":[{\"mesh\":0}],";
        json << "\"meshes\":[{";
        json << "\"primitives\":[{";
        json << "\"attributes\":{";
        json << "\"POSITION\":0,";
        json << "\"NORMAL\":1";
        json << "},";
        json << "\"indices\":2,";
        json << "\"material\":0";
        json << "}],";
        json << "\"name\":\"ImageMaker_Original\"";
        json << "}],";
        // M5: material with base color
        json << "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":["
             << baseColor.r << "," << baseColor.g << "," << baseColor.b << "," << baseColor.a
             << "]}}],";
        json << "\"accessors\":[";

        // Build min/max strings for POSITION
        float mx = -1e9f, my = -1e9f, mz = -1e9f;
        float mnx = 1e9f, mny = 1e9f, mnz = 1e9f;
        for (auto& v : vertices) {
            if (v.x > mx) mx = v.x; if (v.y > my) my = v.y; if (v.z > mz) mz = v.z;
            if (v.x < mnx) mnx = v.x; if (v.y < mny) mny = v.y; if (v.z < mnz) mnz = v.z;
        }
        char mbuf[256];

        // POSITION
        json << "{";
        json << "\"bufferView\":0,\"componentType\":5126,\"count\":" << vertexCount
             << ",\"type\":\"VEC3\",";
        snprintf(mbuf, sizeof(mbuf), "\"max\":[%.6f,%.6f,%.6f],\"min\":[%.6f,%.6f,%.6f]",
                 mx, my, mz, mnx, mny, mnz);
        json << mbuf << "},";

        // NORMAL
        json << "{\"bufferView\":1,\"componentType\":5126,\"count\":" << vertexCount
             << ",\"type\":\"VEC3\"},";

        // INDICES
        json << "{\"bufferView\":2,\"componentType\":5125,\"count\":" << indexCount
             << ",\"type\":\"SCALAR\"}";

        json << "],\"bufferViews\":[";
        json << "{\"buffer\":0,\"byteOffset\":" << vertexByteOffset
             << ",\"byteLength\":" << vertexByteLength << ",\"byteStride\":24},";
        json << "{\"buffer\":0,\"byteOffset\":" << (vertexByteOffset + 12)
             << ",\"byteLength\":" << vertexByteLength << ",\"byteStride\":24},";
        json << "{\"buffer\":0,\"byteOffset\":" << indexByteOffset
             << ",\"byteLength\":" << indexByteLength << "}";
        json << "],\"buffers\":[{\"byteLength\":" << totalBinSize << "}]}";

        std::string jsonStr = json.str();
        while (jsonStr.size() % 4 != 0) jsonStr += ' ';

        std::ofstream out(path, std::ios::binary);
        if (!out) return false;

        uint32_t magic = 0x46546C67;
        uint32_t version = 2;
        uint32_t jsonChunkType = 0x4E4F534A;
        uint32_t binChunkType = 0x004E4942;

        uint32_t headerSize = 12;
        uint32_t jsonChunkHeader = 8;
        uint32_t jsonPaddedLen = (uint32_t)jsonStr.size();
        uint32_t binChunkHeader = 8;
        uint32_t totalLength = headerSize + jsonChunkHeader + jsonPaddedLen + binChunkHeader + totalBinSize;

        writeU32(out, magic);
        writeU32(out, version);
        writeU32(out, totalLength);
        writeU32(out, jsonPaddedLen);
        writeU32(out, jsonChunkType);
        out.write(jsonStr.data(), jsonStr.size());
        writeU32(out, totalBinSize);
        writeU32(out, binChunkType);
        out.write(reinterpret_cast<const char*>(vbuf.data()), vertexByteLength);
        out.write(reinterpret_cast<const char*>(indices.data()), indexByteLength);
        out.close();

        printf("glTF (original) exported: %s\n", path.c_str());
        printf("  Vertices: %u, Triangles: %u\n", vertexCount, triCount);
        return true;
    }

    // ─── M7: Export mesh with morph targets (blend shapes) as .glb ─────
    // baseVertices/baseIndices: neutral mesh
    // deltas: array of 6 BlendDelta (joy, anger, sadness, surprise, fear, disgust)
    // deltaNames: optional display names (defaults to delta.name)
    static bool exportGLBWithMorphTargets(const std::string& path,
                                           const std::vector<Vertex>& baseVertices,
                                           const std::vector<uint32_t>& baseIndices,
                                           const BlendDelta* deltas,
                                           int deltaCount,
                                           const glm::vec4& baseColor = glm::vec4(0.8f, 0.7f, 0.6f, 1.0f)) {
        if (baseVertices.empty() || baseIndices.empty()) {
            fprintf(stderr, "glTF morph export: empty mesh\n");
            return false;
        }

        uint32_t vertexCount = (uint32_t)baseVertices.size();
        uint32_t indexCount = (uint32_t)baseIndices.size();

        // ── Build binary buffer ──────────────────────────────────────
        // Layout:
        //   [0] base positions + normals (6 floats × vertexCount)
        //   [1] base indices (uint32 × indexCount)
        //   [2..2+N-1] morph target position deltas (3 floats × vertexCount each)

        uint32_t baseVertexByteLength = vertexCount * 6 * sizeof(float);  // pos+normal
        uint32_t baseIndexByteLength = indexCount * sizeof(uint32_t);

        uint32_t baseVertexOffset = 0;
        uint32_t baseIndexOffset = baseVertexByteLength;

        // Compute morph target offsets
        std::vector<uint32_t> morphOffsets(deltaCount);
        std::vector<uint32_t> morphByteLengths(deltaCount);
        uint32_t curMorphOffset = baseIndexOffset + baseIndexByteLength;
        for (int d = 0; d < deltaCount; d++) {
            morphByteLengths[d] = vertexCount * 3 * sizeof(float); // position only
            morphOffsets[d] = curMorphOffset;
            curMorphOffset += morphByteLengths[d];
        }

        uint32_t totalBinSize = curMorphOffset;

        // Write binary data
        std::vector<uint8_t> binData(totalBinSize, 0);

        // Base vertices: interleaved pos(3f)+normal(3f)
        float* baseBuf = reinterpret_cast<float*>(binData.data() + baseVertexOffset);
        for (uint32_t i = 0; i < vertexCount; i++) {
            const auto& v = baseVertices[i];
            baseBuf[i * 6 + 0] = v.x;
            baseBuf[i * 6 + 1] = v.y;
            baseBuf[i * 6 + 2] = v.z;
            baseBuf[i * 6 + 3] = v.nx;
            baseBuf[i * 6 + 4] = v.ny;
            baseBuf[i * 6 + 5] = v.nz;
        }

        // Base indices
        uint32_t* idxBuf = reinterpret_cast<uint32_t*>(binData.data() + baseIndexOffset);
        memcpy(idxBuf, baseIndices.data(), baseIndexByteLength);

        // Morph target deltas
        for (int d = 0; d < deltaCount; d++) {
            float* morphBuf = reinterpret_cast<float*>(binData.data() + morphOffsets[d]);
            for (uint32_t i = 0; i < vertexCount && i < (uint32_t)deltas[d].offsets.size(); i++) {
                morphBuf[i * 3 + 0] = deltas[d].offsets[i].x;
                morphBuf[i * 3 + 1] = deltas[d].offsets[i].y;
                morphBuf[i * 3 + 2] = deltas[d].offsets[i].z;
            }
        }

        // ── Build JSON ───────────────────────────────────────────────
        uint32_t triCount = indexCount / 3;

        // Bounding box for base positions
        float mnx = 1e9f, mny = 1e9f, mnz = 1e9f;
        float mxx = -1e9f, mxy = -1e9f, mxz = -1e9f;
        for (auto& v : baseVertices) {
            if (v.x < mnx) mnx = v.x; if (v.y < mny) mny = v.y; if (v.z < mnz) mnz = v.z;
            if (v.x > mxx) mxx = v.x; if (v.y > mxy) mxy = v.y; if (v.z > mxz) mxz = v.z;
        }

        std::ostringstream json;
        json << "{\n";
        json << "  \"asset\":{\"version\":\"2.0\",\"generator\":\"ImageMaker M7\"},\n";
        json << "  \"scene\":0,\n";
        json << "  \"scenes\":[{\"nodes\":[0]}],\n";
        json << "  \"nodes\":[{\"mesh\":0,\"name\":\"Face\"}],\n";

        // Mesh with morph targets
        json << "  \"meshes\":[{\n";
        json << "    \"primitives\":[{\n";
        json << "      \"attributes\":{\"POSITION\":0,\"NORMAL\":1},\n";
        json << "      \"indices\":2,\n";
        json << "      \"material\":0,\n";
        json << "      \"targets\":[\n";

        for (int d = 0; d < deltaCount; d++) {
            json << "        {\"POSITION\":" << (3 + d) << "}";
            if (d + 1 < deltaCount) json << ",";
            json << "\n";
        }
        json << "      ]\n";
        json << "    }],\n";
        json << "    \"weights\":[";
        for (int d = 0; d < deltaCount; d++) {
            json << "0.0";
            if (d + 1 < deltaCount) json << ",";
        }
        json << "],\n";
        json << "    \"name\":\"ImageMaker_Face\"\n";
        json << "  }],\n";

        // Material
        json << "  \"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":["
             << baseColor.r << "," << baseColor.g << "," << baseColor.b << "," << baseColor.a
             << "]}}],\n";

        // Accessors
        json << "  \"accessors\":[\n";

        // 0: POSITION (base)
        char mbuf[256];
        snprintf(mbuf, sizeof(mbuf),
            "    {\"bufferView\":0,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
            "\"max\":[%.6f,%.6f,%.6f],\"min\":[%.6f,%.6f,%.6f]},",
            vertexCount, mxx, mxy, mxz, mnx, mny, mnz);
        json << mbuf << "\n";

        // 1: NORMAL
        json << "    {\"bufferView\":1,\"componentType\":5126,\"count\":"
             << vertexCount << ",\"type\":\"VEC3\"},\n";

        // 2: INDICES
        json << "    {\"bufferView\":2,\"componentType\":5125,\"count\":"
             << indexCount << ",\"type\":\"SCALAR\"},\n";

        // 3..3+N-1: morph target position deltas
        for (int d = 0; d < deltaCount; d++) {
            // Compute bounding box for this delta
            float dmnx = 1e9f, dmny = 1e9f, dmnz = 1e9f;
            float dmxx = -1e9f, dmxy = -1e9f, dmxz = -1e9f;
            for (uint32_t i = 0; i < vertexCount && i < (uint32_t)deltas[d].offsets.size(); i++) {
                float dx = deltas[d].offsets[i].x;
                float dy = deltas[d].offsets[i].y;
                float dz = deltas[d].offsets[i].z;
                if (dx < dmnx) dmnx = dx; if (dy < dmny) dmny = dy; if (dz < dmnz) dmnz = dz;
                if (dx > dmxx) dmxx = dx; if (dy > dmxy) dmxy = dy; if (dz > dmxz) dmxz = dz;
            }
            snprintf(mbuf, sizeof(mbuf),
                "    {\"bufferView\":%d,\"componentType\":5126,\"count\":%u,\"type\":\"VEC3\","
                "\"max\":[%.6f,%.6f,%.6f],\"min\":[%.6f,%.6f,%.6f]}",
                3 + d, vertexCount, dmxx, dmxy, dmxz, dmnx, dmny, dmnz);
            json << mbuf;
            if (d + 1 < deltaCount) json << ",";
            json << "\n";
        }
        json << "  ],\n";

        // Buffer views
        json << "  \"bufferViews\":[\n";
        // 0: base POSITION (offset 0, stride 24)
        json << "    {\"buffer\":0,\"byteOffset\":" << baseVertexOffset
             << ",\"byteLength\":" << baseVertexByteLength << ",\"byteStride\":24},\n";
        // 1: base NORMAL (offset 12, stride 24)
        json << "    {\"buffer\":0,\"byteOffset\":" << (baseVertexOffset + 12)
             << ",\"byteLength\":" << baseVertexByteLength << ",\"byteStride\":24},\n";
        // 2: base INDICES
        json << "    {\"buffer\":0,\"byteOffset\":" << baseIndexOffset
             << ",\"byteLength\":" << baseIndexByteLength << "},\n";
        // 3..3+N-1: morph target position deltas
        for (int d = 0; d < deltaCount; d++) {
            json << "    {\"buffer\":0,\"byteOffset\":" << morphOffsets[d]
                 << ",\"byteLength\":" << morphByteLengths[d] << "}";
            if (d + 1 < deltaCount) json << ",";
            json << "\n";
        }
        json << "  ],\n";

        // Buffer
        json << "  \"buffers\":[{\"byteLength\":" << totalBinSize << "}]\n";
        json << "}\n";

        std::string jsonStr = json.str();

        // ── Write .glb ────────────────────────────────────────────────
        while (jsonStr.size() % 4 != 0) jsonStr += ' ';

        std::ofstream out(path, std::ios::binary);
        if (!out) {
            fprintf(stderr, "glTF morph export: cannot open %s\n", path.c_str());
            return false;
        }

        uint32_t magic = 0x46546C67;
        uint32_t version = 2;
        uint32_t jsonChunkType = 0x4E4F534A;
        uint32_t binChunkType = 0x004E4942;

        uint32_t headerSize = 12;
        uint32_t jsonChunkHeader = 8;
        uint32_t jsonPaddedLen = (uint32_t)jsonStr.size();
        uint32_t binChunkHeader = 8;
        uint32_t totalLength = headerSize + jsonChunkHeader + jsonPaddedLen + binChunkHeader + totalBinSize;

        writeU32(out, magic);
        writeU32(out, version);
        writeU32(out, totalLength);
        writeU32(out, jsonPaddedLen);
        writeU32(out, jsonChunkType);
        out.write(jsonStr.data(), jsonStr.size());
        writeU32(out, totalBinSize);
        writeU32(out, binChunkType);
        out.write(reinterpret_cast<const char*>(binData.data()), totalBinSize);
        out.close();

        printf("glTF (morph targets) exported: %s\n", path.c_str());
        printf("  Vertices: %u, Triangles: %u, Morph targets: %d\n",
               vertexCount, triCount, deltaCount);
        printf("  File size: %u bytes\n", totalLength);
        for (int d = 0; d < deltaCount; d++) {
            printf("  [%d] %s\n", d, deltas[d].name.c_str());
        }
        return true;
    }

private:
    static void writeU32(std::ofstream& out, uint32_t val) {
        out.write(reinterpret_cast<const char*>(&val), 4);
    }

    static std::string getMaxPos(const std::vector<UVVertex>& verts) {
        float mx = -1e9f, my = -1e9f, mz = -1e9f;
        for (auto& v : verts) {
            if (v.x > mx) mx = v.x;
            if (v.y > my) my = v.y;
            if (v.z > mz) mz = v.z;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", mx, my, mz);
        return buf;
    }

    static std::string getMinPos(const std::vector<UVVertex>& verts) {
        float mnx = 1e9f, mny = 1e9f, mnz = 1e9f;
        for (auto& v : verts) {
            if (v.x < mnx) mnx = v.x;
            if (v.y < mny) mny = v.y;
            if (v.z < mnz) mnz = v.z;
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", mnx, mny, mnz);
        return buf;
    }
};
