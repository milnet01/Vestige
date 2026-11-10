// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file mesh_recipe.cpp
/// @brief MeshRecipe's saved form and its load-time limits (3D_E-0967).
#include "utils/mesh_recipe.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <string>

namespace Vestige
{

namespace
{

using nlohmann::json;
using Generator = MeshRecipe::Generator;

// Spec § 4.5: a scene file is untrusted, so these bound what a loaded recipe
// can make the builder allocate.
constexpr float kMaxLength = 1000.0f;          // metres
constexpr float kMaxTotalAngle = 3600.0f;      // degrees
constexpr double kMaxStepCount = 1024.0;
constexpr size_t kMinPathPoints = 2;
constexpr size_t kMaxPathPoints = 4096;
constexpr int kMaxSamplesPerSegment = 64;

struct Name
{
    Generator generator;
    const char* name;
};
constexpr Name kGeneratorNames[] = {
    {Generator::WALL, "wall"},
    {Generator::FLOOR, "floor"},
    {Generator::ROOF, "roof"},
    {Generator::STAIRS_STRAIGHT, "stairs_straight"},
    {Generator::STAIRS_SPIRAL, "stairs_spiral"},
    {Generator::PATH, "path"},
};

struct RoofName
{
    RoofType type;
    const char* name;
};
constexpr RoofName kRoofNames[] = {
    {RoofType::FLAT, "flat"},
    {RoofType::GABLE, "gable"},
    {RoofType::SHED, "shed"},
};

const char* generatorName(Generator g)
{
    for (const Name& n : kGeneratorNames)
    {
        if (n.generator == g)
        {
            return n.name;
        }
    }
    return "";
}

const char* roofName(RoofType t)
{
    for (const RoofName& n : kRoofNames)
    {
        if (n.type == t)
        {
            return n.name;
        }
    }
    return "";
}

// Reads one field as a float; false when it is missing, not a number, or not
// finite once narrowed to float.
bool readFloat(const json& j, const char* key, float& out)
{
    const auto it = j.find(key);
    if (it == j.end() || !it->is_number())
    {
        return false;
    }
    out = static_cast<float>(it->get<double>());
    return std::isfinite(out);
}

// A length, radius or height: greater than 0, at most 1000 m.
bool readLength(const json& j, const char* key, float& out)
{
    return readFloat(j, key, out) && out > 0.0f && out <= kMaxLength;
}

// An offset (overhang, peak height, an opening's position): 0 to 1000 m.
bool readOffset(const json& j, const char* key, float& out)
{
    return readFloat(j, key, out) && out >= 0.0f && out <= kMaxLength;
}

bool stepCountOk(float totalHeight, float riseHeight)
{
    return static_cast<double>(totalHeight) / static_cast<double>(riseHeight) <= kMaxStepCount;
}

bool readOpenings(const json& j, std::vector<WallOpening>& out)
{
    const auto it = j.find("openings");
    if (it == j.end())
    {
        return true;  // absent means none
    }
    if (!it->is_array() || it->size() > MeshRecipe::MAX_OPENINGS)
    {
        return false;
    }
    for (const json& o : *it)
    {
        WallOpening w;
        if (!o.is_object() || !readOffset(o, "x", w.xOffset) || !readOffset(o, "y", w.yOffset)
            || !readLength(o, "width", w.width) || !readLength(o, "height", w.height))
        {
            return false;
        }
        out.push_back(w);
    }
    return true;
}

bool readPoints(const json& j, std::vector<glm::vec3>& out)
{
    const auto it = j.find("points");
    if (it == j.end() || !it->is_array() || it->size() < kMinPathPoints
        || it->size() > kMaxPathPoints)
    {
        return false;
    }
    out.reserve(it->size());
    for (const json& p : *it)
    {
        if (!p.is_array() || p.size() != 3)
        {
            return false;
        }
        glm::vec3 v;
        for (int axis = 0; axis < 3; ++axis)
        {
            const json& c = p[static_cast<size_t>(axis)];
            if (!c.is_number())
            {
                return false;
            }
            v[axis] = static_cast<float>(c.get<double>());
            if (!std::isfinite(v[axis]))
            {
                return false;
            }
        }
        out.push_back(v);
    }
    return true;
}

}  // namespace

nlohmann::json MeshRecipe::toJson() const
{
    json j;
    j["generator"] = generatorName(generator);
    switch (generator)
    {
        case Generator::WALL:
        {
            j["width"] = width;
            j["height"] = height;
            j["thickness"] = thickness;
            json list = json::array();
            for (const WallOpening& o : openings)
            {
                list.push_back({{"x", o.xOffset}, {"y", o.yOffset},
                                {"width", o.width}, {"height", o.height}});
            }
            j["openings"] = list;
            break;
        }
        case Generator::FLOOR:
            j["width"] = width;
            j["depth"] = depth;
            j["thickness"] = thickness;
            break;
        case Generator::ROOF:
            j["type"] = roofName(roofType);
            j["width"] = width;
            j["depth"] = depth;
            j["peakHeight"] = peakHeight;
            j["overhang"] = overhang;
            break;
        case Generator::STAIRS_STRAIGHT:
            j["totalHeight"] = totalHeight;
            j["riseHeight"] = riseHeight;
            j["treadDepth"] = treadDepth;
            j["width"] = width;
            break;
        case Generator::STAIRS_SPIRAL:
            j["totalHeight"] = totalHeight;
            j["riseHeight"] = riseHeight;
            j["innerRadius"] = innerRadius;
            j["outerRadius"] = outerRadius;
            j["totalAngle"] = totalAngle;
            break;
        case Generator::PATH:
        {
            json list = json::array();
            for (const glm::vec3& p : points)
            {
                list.push_back({p.x, p.y, p.z});
            }
            j["points"] = list;
            j["width"] = width;
            j["samplesPerSegment"] = samplesPerSegment;
            break;
        }
    }
    return j;
}

std::optional<MeshRecipe> MeshRecipe::fromJson(const nlohmann::json& j)
{
    if (!j.is_object())
    {
        return std::nullopt;
    }
    const auto gen = j.find("generator");
    if (gen == j.end() || !gen->is_string())
    {
        return std::nullopt;
    }

    MeshRecipe r;
    bool known = false;
    for (const Name& n : kGeneratorNames)
    {
        if (*gen == n.name)
        {
            r.generator = n.generator;
            known = true;
        }
    }
    if (!known)
    {
        return std::nullopt;
    }

    bool ok = false;
    switch (r.generator)
    {
        case Generator::WALL:
            ok = readLength(j, "width", r.width) && readLength(j, "height", r.height)
              && readLength(j, "thickness", r.thickness) && readOpenings(j, r.openings);
            break;
        case Generator::FLOOR:
            ok = readLength(j, "width", r.width) && readLength(j, "depth", r.depth)
              && readLength(j, "thickness", r.thickness);
            break;
        case Generator::ROOF:
        {
            const auto type = j.find("type");
            bool typeKnown = false;
            if (type != j.end() && type->is_string())
            {
                for (const RoofName& n : kRoofNames)
                {
                    if (*type == n.name)
                    {
                        r.roofType = n.type;
                        typeKnown = true;
                    }
                }
            }
            ok = typeKnown && readLength(j, "width", r.width) && readLength(j, "depth", r.depth)
              && readOffset(j, "peakHeight", r.peakHeight)
              && readOffset(j, "overhang", r.overhang);
            break;
        }
        case Generator::STAIRS_STRAIGHT:
            ok = readLength(j, "totalHeight", r.totalHeight)
              && readLength(j, "riseHeight", r.riseHeight)
              && readLength(j, "treadDepth", r.treadDepth) && readLength(j, "width", r.width)
              && stepCountOk(r.totalHeight, r.riseHeight);
            break;
        case Generator::STAIRS_SPIRAL:
            ok = readLength(j, "totalHeight", r.totalHeight)
              && readLength(j, "riseHeight", r.riseHeight)
              && readLength(j, "innerRadius", r.innerRadius)
              && readLength(j, "outerRadius", r.outerRadius)
              && r.innerRadius < r.outerRadius
              && readFloat(j, "totalAngle", r.totalAngle) && r.totalAngle > 0.0f
              && r.totalAngle <= kMaxTotalAngle
              && stepCountOk(r.totalHeight, r.riseHeight);
            break;
        case Generator::PATH:
        {
            const auto s = j.find("samplesPerSegment");
            ok = readPoints(j, r.points) && readLength(j, "width", r.width)
              && s != j.end() && s->is_number_integer();
            if (ok)
            {
                const auto samples = s->get<long long>();
                ok = samples >= 1 && samples <= kMaxSamplesPerSegment;
                if (ok)
                {
                    r.samplesPerSegment = static_cast<int>(samples);
                }
            }
            break;
        }
    }
    if (!ok)
    {
        return std::nullopt;
    }
    return r;
}

} // namespace Vestige
