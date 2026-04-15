// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file jolt_helpers.h
/// @brief Coordinate conversion helpers between GLM and Jolt math types.
#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Math/Vec3.h>
#include <Jolt/Math/Quat.h>
#include <Jolt/Math/Mat44.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Vestige
{

inline JPH::Vec3 toJolt(const glm::vec3& v)
{
    return JPH::Vec3(v.x, v.y, v.z);
}

inline glm::vec3 toGlm(const JPH::Vec3& v)
{
    return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
}

inline JPH::Quat toJolt(const glm::quat& q)
{
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

inline glm::quat toGlm(const JPH::Quat& q)
{
    return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
}

inline JPH::Mat44 toJolt(const glm::mat4& m)
{
    // GLM is column-major, Jolt Mat44 takes 4 column vectors
    return JPH::Mat44(
        JPH::Vec4(m[0][0], m[0][1], m[0][2], m[0][3]),
        JPH::Vec4(m[1][0], m[1][1], m[1][2], m[1][3]),
        JPH::Vec4(m[2][0], m[2][1], m[2][2], m[2][3]),
        JPH::Vec4(m[3][0], m[3][1], m[3][2], m[3][3])
    );
}

inline glm::mat4 toGlm(const JPH::Mat44& m)
{
    glm::mat4 result;
    for (int col = 0; col < 4; ++col)
    {
        JPH::Vec4 c = m.GetColumn4(col);
        result[col][0] = c.GetX();
        result[col][1] = c.GetY();
        result[col][2] = c.GetZ();
        result[col][3] = c.GetW();
    }
    return result;
}

} // namespace Vestige
