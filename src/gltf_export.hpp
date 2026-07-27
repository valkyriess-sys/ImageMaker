#pragma once
// ─── ImageMaker M4: glTF 2.0 Binary (.glb) Export ───────────────────
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

#include "mesh_postprocess.hpp"
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
                           const std::vector<uint32_t>& indices) {
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
        json << "\"asset\":{\"version\":\"2.0\",\"generator\":\"ImageMaker M4\"},";
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
        json << "\"indices\":3";
        json << "}],";
        json << "\"name\":\"ImageMaker_Mesh\"";
        json << "}],";
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
                                   const std::vector<uint32_t>& indices) {
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
        json << "\"asset\":{\"version\":\"2.0\",\"generator\":\"ImageMaker M4\"},";
        json << "\"scene\":0,";
        json << "\"scenes\":[{\"nodes\":[0]}],";
        json << "\"nodes\":[{\"mesh\":0}],";
        json << "\"meshes\":[{";
        json << "\"primitives\":[{";
        json << "\"attributes\":{";
        json << "\"POSITION\":0,";
        json << "\"NORMAL\":1";
        json << "},";
        json << "\"indices\":2";
        json << "}],";
        json << "\"name\":\"ImageMaker_Original\"";
        json << "}],";
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
