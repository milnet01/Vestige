// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_mesh_recipe.cpp
/// @brief 3D_E-0967 INV-5 and INV-6: a saved mesh recipe round-trips, and a
///        recipe outside the spec's load limits (§ 4.5) is refused.
///
/// Each limit case breaks one check on an otherwise valid recipe and expects
/// nullopt; its twin sets the same field at the bound (or, for a strict bound,
/// the nearest value that passes) and expects a recipe. The twin shows the
/// refusal came from that check and not another.

#include "utils/mesh_recipe.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace Vestige;
using nlohmann::json;

namespace
{

// One valid saved recipe per generator, in the spec's § 4.4 form.
json wallJson()
{
    return {{"generator", "wall"}, {"width", 3.0}, {"height", 3.0}, {"thickness", 0.3},
            {"openings", json::array({{{"x", 1.0}, {"y", 0.0}, {"width", 1.0}, {"height", 2.1}}})}};
}
json floorJson()
{
    return {{"generator", "floor"}, {"width", 4.0}, {"depth", 4.0}, {"thickness", 0.15}};
}
json roofJson()
{
    return {{"generator", "roof"}, {"type", "gable"}, {"width", 4.0}, {"depth", 4.0},
            {"peakHeight", 1.5}, {"overhang", 0.3}};
}
json straightJson()
{
    return {{"generator", "stairs_straight"}, {"totalHeight", 3.0}, {"riseHeight", 0.18},
            {"treadDepth", 0.28}, {"width", 1.0}};
}
json spiralJson()
{
    return {{"generator", "stairs_spiral"}, {"totalHeight", 3.0}, {"riseHeight", 0.18},
            {"innerRadius", 0.3}, {"outerRadius", 1.5}, {"totalAngle", 360.0}};
}
json pathJson()
{
    return {{"generator", "path"}, {"points", json::array({json::array({0.0, 0.0, 0.0}),
                                                           json::array({5.0, 0.0, 2.0})})},
            {"width", 2.0}, {"samplesPerSegment", 10}};
}

std::vector<json> allValid()
{
    return {wallJson(), floorJson(), roofJson(), straightJson(), spiralJson(), pathJson()};
}

// A field reached by a JSON pointer, so an opening's fields are covered too.
struct Field
{
    json base;
    json::json_pointer ptr;
};

// Every length, radius and height: must be > 0 and <= 1000.
std::vector<Field> lengthFields()
{
    return {
        {wallJson(), json::json_pointer("/width")},
        {wallJson(), json::json_pointer("/height")},
        {wallJson(), json::json_pointer("/thickness")},
        {wallJson(), json::json_pointer("/openings/0/width")},
        {wallJson(), json::json_pointer("/openings/0/height")},
        {floorJson(), json::json_pointer("/width")},
        {floorJson(), json::json_pointer("/depth")},
        {floorJson(), json::json_pointer("/thickness")},
        {roofJson(), json::json_pointer("/width")},
        {roofJson(), json::json_pointer("/depth")},
        {straightJson(), json::json_pointer("/totalHeight")},
        {straightJson(), json::json_pointer("/riseHeight")},
        {straightJson(), json::json_pointer("/treadDepth")},
        {straightJson(), json::json_pointer("/width")},
        {spiralJson(), json::json_pointer("/totalHeight")},
        {spiralJson(), json::json_pointer("/riseHeight")},
        {spiralJson(), json::json_pointer("/innerRadius")},
        {spiralJson(), json::json_pointer("/outerRadius")},
        {pathJson(), json::json_pointer("/width")},
    };
}

// Overhang, peak height and the opening offsets: >= 0 and <= 1000.
std::vector<Field> offsetFields()
{
    return {
        {roofJson(), json::json_pointer("/peakHeight")},
        {roofJson(), json::json_pointer("/overhang")},
        {wallJson(), json::json_pointer("/openings/0/x")},
        {wallJson(), json::json_pointer("/openings/0/y")},
    };
}

json with(json j, const json::json_pointer& ptr, const json& value)
{
    j[ptr] = value;
    return j;
}

bool accepted(const json& j)
{
    return MeshRecipe::fromJson(j).has_value();
}

float justAbove(float v)
{
    return std::nextafter(v, std::numeric_limits<float>::infinity());
}
float justBelow(float v)
{
    return std::nextafter(v, -std::numeric_limits<float>::infinity());
}

// Each case checks one fixture changed at one place. Stairs fixtures move the
// other field as well where needed, so the step count stays legal.
void expectRefusedWithTwin(const json& refused, const json& twin)
{
    EXPECT_FALSE(accepted(refused)) << "should refuse: " << refused.dump();
    EXPECT_TRUE(accepted(twin)) << "should accept: " << twin.dump();
}

}  // namespace

TEST(MeshRecipe, EveryValidFixtureIsAccepted)
{
    for (const json& j : allValid())
    {
        EXPECT_TRUE(accepted(j)) << j.dump();
    }
}

TEST(MeshRecipe, UnknownGeneratorIsRefused)
{
    EXPECT_FALSE(accepted(with(wallJson(), json::json_pointer("/generator"), "tower")));
}

TEST(MeshRecipe, UnknownRoofTypeIsRefused)
{
    EXPECT_FALSE(accepted(with(roofJson(), json::json_pointer("/type"), "dome")));
}

TEST(MeshRecipe, NonFiniteFloatIsRefused)
{
    for (const Field& f : lengthFields())
    {
        SCOPED_TRACE(f.ptr.to_string());
        EXPECT_FALSE(accepted(with(f.base, f.ptr, std::numeric_limits<double>::quiet_NaN())));
        EXPECT_FALSE(accepted(with(f.base, f.ptr, std::numeric_limits<double>::infinity())));
    }
    EXPECT_FALSE(accepted(with(pathJson(), json::json_pointer("/points/1/2"),
                               std::numeric_limits<double>::quiet_NaN())));
}

TEST(MeshRecipe, LengthMustBeAboveZero)
{
    for (const Field& f : lengthFields())
    {
        SCOPED_TRACE(f.ptr.to_string());
        json twin = with(f.base, f.ptr, justAbove(0.0f));
        if (f.ptr.to_string() == "/riseHeight")
        {
            twin = with(twin, json::json_pointer("/totalHeight"), justAbove(0.0f));
        }
        if (f.ptr.to_string() == "/outerRadius")
        {
            // The inner radius must stay below it.
            twin = with(twin, json::json_pointer("/innerRadius"), justAbove(0.0f));
            twin = with(twin, f.ptr, justAbove(justAbove(0.0f)));
        }
        expectRefusedWithTwin(with(f.base, f.ptr, 0.0), twin);
    }
}

TEST(MeshRecipe, LengthMustBeAtMost1000)
{
    for (const Field& f : lengthFields())
    {
        SCOPED_TRACE(f.ptr.to_string());
        json refused = with(f.base, f.ptr, justAbove(1000.0f));
        json twin = with(f.base, f.ptr, 1000.0);
        const std::string at = f.ptr.to_string();
        if (at == "/totalHeight")
        {
            // Keep the step count legal: 1000 / 1 = 1000 steps.
            refused = with(refused, json::json_pointer("/riseHeight"), 1.0);
            twin = with(twin, json::json_pointer("/riseHeight"), 1.0);
        }
        if (at == "/riseHeight")
        {
            refused = with(refused, json::json_pointer("/totalHeight"), 1000.0);
            twin = with(twin, json::json_pointer("/totalHeight"), 1000.0);
        }
        if (at == "/innerRadius")
        {
            refused = with(refused, json::json_pointer("/outerRadius"), 1000.0);
            twin = with(twin, json::json_pointer("/outerRadius"), 1000.0);
            twin = with(twin, json::json_pointer("/innerRadius"), justBelow(1000.0f));
        }
        expectRefusedWithTwin(refused, twin);
    }
}

TEST(MeshRecipe, OffsetMustNotBeNegative)
{
    for (const Field& f : offsetFields())
    {
        SCOPED_TRACE(f.ptr.to_string());
        expectRefusedWithTwin(with(f.base, f.ptr, justBelow(0.0f)), with(f.base, f.ptr, 0.0));
    }
}

TEST(MeshRecipe, OffsetMustBeAtMost1000)
{
    for (const Field& f : offsetFields())
    {
        SCOPED_TRACE(f.ptr.to_string());
        expectRefusedWithTwin(with(f.base, f.ptr, justAbove(1000.0f)),
                              with(f.base, f.ptr, 1000.0));
    }
}

TEST(MeshRecipe, InnerRadiusMustBeBelowOuter)
{
    const json::json_pointer inner("/innerRadius");
    expectRefusedWithTwin(with(spiralJson(), inner, 1.5), with(spiralJson(), inner, justBelow(1.5f)));
}

TEST(MeshRecipe, TotalAngleMustBeAboveZero)
{
    const json::json_pointer angle("/totalAngle");
    expectRefusedWithTwin(with(spiralJson(), angle, 0.0),
                          with(spiralJson(), angle, justAbove(0.0f)));
}

TEST(MeshRecipe, TotalAngleMustBeAtMost3600)
{
    const json::json_pointer angle("/totalAngle");
    expectRefusedWithTwin(with(spiralJson(), angle, justAbove(3600.0f)),
                          with(spiralJson(), angle, 3600.0));
}

TEST(MeshRecipe, StepCountMustBeAtMost1024)
{
    // 128 / 0.125 is exactly 1024 in float.
    const json::json_pointer total("/totalHeight");
    const json::json_pointer rise("/riseHeight");
    for (const json& base : {straightJson(), spiralJson()})
    {
        SCOPED_TRACE(base["generator"].get<std::string>());
        const json at = with(with(base, total, 128.0), rise, 0.125);
        expectRefusedWithTwin(with(at, total, justAbove(128.0f)), at);
        expectRefusedWithTwin(with(at, rise, justBelow(0.125f)), at);
    }
}

TEST(MeshRecipe, OpeningsMustBeAtMost64)
{
    const json one = wallJson()["openings"][0];
    json at = wallJson();
    at["openings"] = json::array();
    for (size_t i = 0; i < MeshRecipe::MAX_OPENINGS; ++i)
    {
        at["openings"].push_back(one);
    }
    json over = at;
    over["openings"].push_back(one);
    expectRefusedWithTwin(over, at);
}

TEST(MeshRecipe, PathNeedsAtLeastTwoPoints)
{
    json one = pathJson();
    one["points"].erase(1);
    expectRefusedWithTwin(one, pathJson());
}

TEST(MeshRecipe, PathHoldsAtMost4096Points)
{
    json at = pathJson();
    at["points"] = json::array();
    for (int i = 0; i < 4096; ++i)
    {
        at["points"].push_back(json::array({static_cast<double>(i), 0.0, 0.0}));
    }
    json over = at;
    over["points"].push_back(json::array({4096.0, 0.0, 0.0}));
    expectRefusedWithTwin(over, at);
}

TEST(MeshRecipe, SamplesPerSegmentMustBeAtLeastOne)
{
    const json::json_pointer s("/samplesPerSegment");
    expectRefusedWithTwin(with(pathJson(), s, 0), with(pathJson(), s, 1));
}

TEST(MeshRecipe, SamplesPerSegmentMustBeAtMost64)
{
    const json::json_pointer s("/samplesPerSegment");
    expectRefusedWithTwin(with(pathJson(), s, 65), with(pathJson(), s, 64));
}

TEST(MeshRecipe, MissingOrMistypedFieldIsRefused)
{
    for (const json& base : allValid())
    {
        for (const auto& [key, value] : base.items())
        {
            if (key == "openings")
            {
                continue;  // the one optional field
            }
            SCOPED_TRACE(base["generator"].get<std::string>() + "." + key);
            json missing = base;
            missing.erase(key);
            EXPECT_FALSE(accepted(missing));
            json mistyped = base;
            mistyped[key] = json::object();
            EXPECT_FALSE(accepted(mistyped));
        }
    }
}

TEST(MeshRecipe, MissingOpeningsMeansNone)
{
    json j = wallJson();
    j.erase("openings");
    const auto r = MeshRecipe::fromJson(j);
    ASSERT_TRUE(r.has_value());
    EXPECT_TRUE(r->openings.empty());
}

namespace
{

// Every field, so a field written under one name and read under another (or
// not written at all) shows as a difference.
void expectSameRecipe(const MeshRecipe& a, const MeshRecipe& b)
{
    EXPECT_EQ(a.generator, b.generator);
    EXPECT_FLOAT_EQ(a.width, b.width);
    EXPECT_FLOAT_EQ(a.height, b.height);
    EXPECT_FLOAT_EQ(a.thickness, b.thickness);
    EXPECT_FLOAT_EQ(a.depth, b.depth);
    ASSERT_EQ(a.openings.size(), b.openings.size());
    for (size_t i = 0; i < a.openings.size(); ++i)
    {
        EXPECT_FLOAT_EQ(a.openings[i].xOffset, b.openings[i].xOffset);
        EXPECT_FLOAT_EQ(a.openings[i].yOffset, b.openings[i].yOffset);
        EXPECT_FLOAT_EQ(a.openings[i].width, b.openings[i].width);
        EXPECT_FLOAT_EQ(a.openings[i].height, b.openings[i].height);
    }
    EXPECT_EQ(a.roofType, b.roofType);
    EXPECT_FLOAT_EQ(a.peakHeight, b.peakHeight);
    EXPECT_FLOAT_EQ(a.overhang, b.overhang);
    EXPECT_FLOAT_EQ(a.totalHeight, b.totalHeight);
    EXPECT_FLOAT_EQ(a.riseHeight, b.riseHeight);
    EXPECT_FLOAT_EQ(a.treadDepth, b.treadDepth);
    EXPECT_FLOAT_EQ(a.innerRadius, b.innerRadius);
    EXPECT_FLOAT_EQ(a.outerRadius, b.outerRadius);
    EXPECT_FLOAT_EQ(a.totalAngle, b.totalAngle);
    EXPECT_EQ(a.points, b.points);
    EXPECT_EQ(a.samplesPerSegment, b.samplesPerSegment);
}

}  // namespace

TEST(MeshRecipe, RoundTripsEveryGenerator)
{
    using G = MeshRecipe::Generator;
    std::vector<MeshRecipe> recipes;

    MeshRecipe wall;
    wall.generator = G::WALL;
    wall.width = 3.5f; wall.height = 2.75f; wall.thickness = 0.25f;
    wall.openings = {{0.5f, 0.0f, 0.9f, 2.1f}, {2.0f, 1.0f, 0.75f, 1.25f}};
    recipes.push_back(wall);

    MeshRecipe floor;
    floor.generator = G::FLOOR;
    floor.width = 4.5f; floor.depth = 3.25f; floor.thickness = 0.125f;
    recipes.push_back(floor);

    for (RoofType type : {RoofType::FLAT, RoofType::GABLE, RoofType::SHED})
    {
        MeshRecipe roof;
        roof.generator = G::ROOF;
        roof.roofType = type;
        roof.width = 5.5f; roof.depth = 6.25f; roof.peakHeight = 1.75f; roof.overhang = 0.375f;
        recipes.push_back(roof);
    }

    MeshRecipe straight;
    straight.generator = G::STAIRS_STRAIGHT;
    straight.totalHeight = 2.5f; straight.riseHeight = 0.2f; straight.treadDepth = 0.3f;
    straight.width = 1.25f;
    recipes.push_back(straight);

    MeshRecipe spiral;
    spiral.generator = G::STAIRS_SPIRAL;
    spiral.totalHeight = 3.5f; spiral.riseHeight = 0.175f; spiral.innerRadius = 0.25f;
    spiral.outerRadius = 1.75f; spiral.totalAngle = 450.0f;
    recipes.push_back(spiral);

    MeshRecipe path;
    path.generator = G::PATH;
    path.points = {{0.0f, 0.5f, 0.0f}, {4.0f, 0.25f, 1.0f}, {8.0f, 0.0f, -2.0f}};
    path.width = 1.5f; path.samplesPerSegment = 7;
    recipes.push_back(path);

    for (const MeshRecipe& r : recipes)
    {
        const json j = r.toJson();
        SCOPED_TRACE(j.dump());
        const auto back = MeshRecipe::fromJson(j);
        ASSERT_TRUE(back.has_value());
        expectSameRecipe(r, *back);
    }
}
