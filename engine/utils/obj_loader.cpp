// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file obj_loader.cpp
/// @brief OBJ file loader implementation.
#include "utils/obj_loader.h"
#include "core/logger.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Vestige
{

/// @brief Hash function for vertex deduplication.
struct VertexKey
{
    int posIndex;
    int texIndex;
    int normIndex;

    bool operator==(const VertexKey& other) const
    {
        return posIndex == other.posIndex
            && texIndex == other.texIndex
            && normIndex == other.normIndex;
    }
};

struct VertexKeyHash
{
    size_t operator()(const VertexKey& key) const
    {
        size_t h1 = std::hash<int>()(key.posIndex);
        size_t h2 = std::hash<int>()(key.texIndex);
        size_t h3 = std::hash<int>()(key.normIndex);
        return h1 ^ (h2 << 16) ^ (h3 << 32);
    }
};

// Per-line read cap — rejects malformed / adversarial OBJs that would otherwise
// grow a single std::string unboundedly. CWE-400 Uncontrolled Resource Consumption.
static constexpr size_t kMaxLineBytes = 1024u * 1024u;  // 1 MiB

enum class LineStatus { Ok, Eof, TooLong };

static LineStatus readBoundedLine(std::istream& in, std::string& line, size_t maxBytes)
{
    line.clear();
    int ch = in.get();
    if (ch == std::char_traits<char>::eof())
    {
        return LineStatus::Eof;
    }
    do
    {
        if (ch == '\n')
        {
            return LineStatus::Ok;
        }
        if (line.size() >= maxBytes)
        {
            return LineStatus::TooLong;
        }
        line.push_back(static_cast<char>(ch));
    } while ((ch = in.get()) != std::char_traits<char>::eof());
    return LineStatus::Ok;  // final line without trailing newline
}

/// @brief Resolves an OBJ index string to a 0-based array index.
/// Per Wavefront OBJ spec Appendix B: positive values are 1-based; negative
/// values are relative to the current end of the list at face-parse time
/// (e.g. -1 is the most recent vertex). Zero and malformed input → -1 (invalid).
static int resolveObjIndex(const std::string& s, size_t listSize)
{
    if (s.empty())
    {
        return -1;
    }
    int raw = 0;
    try
    {
        raw = std::stoi(s);
    }
    catch (const std::exception&)
    {
        return -1;
    }
    if (raw > 0)
    {
        return raw - 1;
    }
    if (raw < 0)
    {
        return static_cast<int>(listSize) + raw;
    }
    return -1;
}

/// @brief Parses a face vertex token like "1/2/3", "1//3", "1", or "-3/-2/-1".
static VertexKey parseFaceVertex(const std::string& token,
                                 size_t numPositions,
                                 size_t numTexCoords,
                                 size_t numNormals)
{
    VertexKey key = {-1, -1, -1};

    std::istringstream stream(token);
    std::string part;

    if (std::getline(stream, part, '/'))
    {
        key.posIndex = resolveObjIndex(part, numPositions);
    }
    if (std::getline(stream, part, '/'))
    {
        key.texIndex = resolveObjIndex(part, numTexCoords);
    }
    if (std::getline(stream, part, '/'))
    {
        key.normIndex = resolveObjIndex(part, numNormals);
    }

    return key;
}

bool ObjLoader::load(const std::string& filePath,
                     std::vector<Vertex>& outVertices,
                     std::vector<uint32_t>& outIndices)
{
    // Validate file size (reject files > 256 MB to prevent OOM)
    {
        std::error_code ec;
        auto fileSize = std::filesystem::file_size(filePath, ec);
        if (!ec && fileSize > 256 * 1024 * 1024)
        {
            Logger::error("OBJ file too large (" + std::to_string(fileSize / (1024 * 1024))
                + " MB): " + filePath);
            return false;
        }
    }

    std::ifstream file(filePath);
    if (!file.is_open())
    {
        Logger::error("Failed to open OBJ file: " + filePath);
        return false;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texCoords;
    std::vector<glm::vec3> normals;

    // Map to deduplicate vertices with identical pos/tex/norm combinations
    std::unordered_map<VertexKey, uint32_t, VertexKeyHash> vertexMap;

    outVertices.clear();
    outIndices.clear();

    std::string line;
    int lineNumber = 0;

    for (;;)
    {
        LineStatus status = readBoundedLine(file, line, kMaxLineBytes);
        if (status == LineStatus::Eof)
        {
            break;
        }
        lineNumber++;
        if (status == LineStatus::TooLong)
        {
            Logger::error("OBJ line " + std::to_string(lineNumber)
                + " exceeds " + std::to_string(kMaxLineBytes)
                + "-byte cap: " + filePath);
            outVertices.clear();
            outIndices.clear();
            return false;
        }

        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream stream(line);
        std::string prefix;
        stream >> prefix;

        if (prefix == "v")
        {
            // Vertex position
            glm::vec3 pos;
            stream >> pos.x >> pos.y >> pos.z;
            positions.push_back(pos);
        }
        else if (prefix == "vt")
        {
            // Texture coordinate
            glm::vec2 tex;
            stream >> tex.x >> tex.y;
            texCoords.push_back(tex);
        }
        else if (prefix == "vn")
        {
            // Vertex normal
            glm::vec3 norm;
            stream >> norm.x >> norm.y >> norm.z;
            normals.push_back(norm);
        }
        else if (prefix == "f")
        {
            // Face — collect all vertex tokens (supports triangles and quads)
            std::vector<std::string> tokens;
            std::string token;
            while (stream >> token)
            {
                tokens.push_back(token);
            }

            if (tokens.size() < 3)
            {
                Logger::warning("OBJ line " + std::to_string(lineNumber)
                    + ": face with fewer than 3 vertices, skipping");
                continue;
            }

            // Triangulate: fan from first vertex (works for convex polygons)
            for (size_t i = 1; i + 1 < tokens.size(); i++)
            {
                VertexKey keys[3] = {
                    parseFaceVertex(tokens[0],     positions.size(), texCoords.size(), normals.size()),
                    parseFaceVertex(tokens[i],     positions.size(), texCoords.size(), normals.size()),
                    parseFaceVertex(tokens[i + 1], positions.size(), texCoords.size(), normals.size())
                };

                for (const auto& key : keys)
                {
                    auto it = vertexMap.find(key);
                    if (it != vertexMap.end())
                    {
                        outIndices.push_back(it->second);
                    }
                    else
                    {
                        Vertex vertex = {};
                        vertex.color = glm::vec3(0.8f);  // Default grey

                        if (key.posIndex >= 0 && key.posIndex < static_cast<int>(positions.size()))
                        {
                            vertex.position = positions.at(static_cast<size_t>(key.posIndex));
                        }

                        if (key.texIndex >= 0 && key.texIndex < static_cast<int>(texCoords.size()))
                        {
                            vertex.texCoord = texCoords.at(static_cast<size_t>(key.texIndex));
                        }

                        if (key.normIndex >= 0 && key.normIndex < static_cast<int>(normals.size()))
                        {
                            vertex.normal = normals.at(static_cast<size_t>(key.normIndex));
                        }

                        auto index = static_cast<uint32_t>(outVertices.size());
                        vertexMap[key] = index;
                        outVertices.push_back(vertex);
                        outIndices.push_back(index);
                    }
                }
            }
        }
        // Ignore: mtllib, usemtl, o, g, s (will handle in later phases)
    }

    if (outVertices.empty())
    {
        Logger::error("OBJ file contained no geometry: " + filePath);
        return false;
    }

    // Compute tangent/bitangent vectors for normal mapping
    calculateTangents(outVertices, outIndices);

    Logger::info("OBJ loaded: " + filePath + " ("
        + std::to_string(outVertices.size()) + " vertices, "
        + std::to_string(outIndices.size() / 3) + " triangles)");

    return true;
}

bool ObjLoader::loadMesh(const std::string& filePath, Mesh& outMesh)
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    if (!load(filePath, vertices, indices))
    {
        return false;
    }

    outMesh.upload(vertices, indices);
    return true;
}

} // namespace Vestige
