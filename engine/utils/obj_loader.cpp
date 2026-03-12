/// @file obj_loader.cpp
/// @brief OBJ file loader implementation.
#include "utils/obj_loader.h"
#include "core/logger.h"

#include <glm/glm.hpp>

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

/// @brief Parses a face vertex token like "1/2/3", "1//3", or "1".
static VertexKey parseFaceVertex(const std::string& token)
{
    VertexKey key = {-1, -1, -1};

    std::istringstream stream(token);
    std::string part;

    // Position index (always present)
    if (std::getline(stream, part, '/'))
    {
        if (!part.empty())
        {
            key.posIndex = std::stoi(part) - 1;  // OBJ is 1-indexed
        }
    }

    // Texture coordinate index (optional)
    if (std::getline(stream, part, '/'))
    {
        if (!part.empty())
        {
            key.texIndex = std::stoi(part) - 1;
        }
    }

    // Normal index (optional)
    if (std::getline(stream, part, '/'))
    {
        if (!part.empty())
        {
            key.normIndex = std::stoi(part) - 1;
        }
    }

    return key;
}

bool ObjLoader::load(const std::string& filePath,
                     std::vector<Vertex>& outVertices,
                     std::vector<uint32_t>& outIndices)
{
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

    while (std::getline(file, line))
    {
        lineNumber++;

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
                    parseFaceVertex(tokens[0]),
                    parseFaceVertex(tokens[i]),
                    parseFaceVertex(tokens[i + 1])
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
