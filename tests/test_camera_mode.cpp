// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_camera_mode.cpp
/// @brief Phase 10.8 Slice CM1 — base-type tests for the
///        CameraMode strategy component family.
///
/// This slice lands the pure interface + data types only. No
/// concrete modes exist yet (those are CM2–CM7). The tests
/// cover:
///
///   - `CameraViewOutput` equality semantics (required for
///     blend determinism + cheap test assertions).
///   - `blendCameraView` endpoints + mid-point interpolation
///     for all interpolated fields, plus the discrete
///     projection-type snap at t = 0.5.
///   - `clone()` round-trip on a minimal concrete mode
///     subclass, so the base `CameraMode` contract is
///     verified without waiting for CM2.
///
/// Design reference: docs/phases/phase_10_8_camera_modes_design.md §4.2
/// and §4.5.

#include <gtest/gtest.h>

#include "scene/camera_mode.h"

#include <glm/gtc/quaternion.hpp>

#include <memory>

using namespace Vestige;

namespace
{
constexpr float kEps = 1e-5f;

/// Minimal concrete mode used only by these tests. Once CM2 lands
/// `FirstPersonCameraMode`, we'll swap to it, but for CM1 the point
/// is just to prove the base class compiles + clones.
class StubCameraMode : public CameraMode
{
public:
    CameraViewOutput computeOutput(const CameraInputs& /*inputs*/,
                                   float /*deltaTime*/) override
    {
        CameraViewOutput out;
        out.position = glm::vec3(1.0f, 2.0f, 3.0f);
        out.fov      = 80.0f;
        return out;
    }

    CameraModeType type() const override { return CameraModeType::FirstPerson; }

    std::unique_ptr<Component> clone() const override
    {
        return std::make_unique<StubCameraMode>(*this);
    }
};
} // namespace

// -- CameraViewOutput equality -----------------------------------

TEST(CameraViewOutput, DefaultConstructorProducesUnitIdentity)
{
    CameraViewOutput v;
    EXPECT_EQ(v.position, glm::vec3(0.0f));
    EXPECT_NEAR(v.orientation.w, 1.0f, kEps);
    EXPECT_NEAR(v.fov, 75.0f, kEps);
    EXPECT_EQ(v.projection, ProjectionType::PERSPECTIVE);
}

TEST(CameraViewOutput, EqualityDetectsAllFieldChanges)
{
    CameraViewOutput a;
    CameraViewOutput b;
    EXPECT_EQ(a, b);

    // Mutate each field in turn — equality must break every time.
    b = a; b.position = glm::vec3(1, 0, 0);
    EXPECT_NE(a, b);
    b = a; b.orientation = glm::angleAxis(1.0f, glm::vec3(0, 1, 0));
    EXPECT_NE(a, b);
    b = a; b.fov = 60.0f;
    EXPECT_NE(a, b);
    b = a; b.orthoSize = 5.0f;
    EXPECT_NE(a, b);
    b = a; b.nearPlane = 0.5f;
    EXPECT_NE(a, b);
    b = a; b.farPlane = 500.0f;
    EXPECT_NE(a, b);
    b = a; b.projection = ProjectionType::ORTHOGRAPHIC;
    EXPECT_NE(a, b);
}

// -- blendCameraView ---------------------------------------------

TEST(BlendCameraView, EndpointsReturnSourceOrTarget)
{
    CameraViewOutput from;
    from.position = glm::vec3(0.0f);
    from.fov      = 60.0f;

    CameraViewOutput to;
    to.position = glm::vec3(10.0f, 0.0f, 0.0f);
    to.fov      = 90.0f;

    CameraViewOutput atZero = blendCameraView(from, to, 0.0f);
    EXPECT_EQ(atZero, from);

    CameraViewOutput atOne = blendCameraView(from, to, 1.0f);
    EXPECT_EQ(atOne, to);
}

TEST(BlendCameraView, MidpointInterpolatesContinuousFields)
{
    CameraViewOutput from;
    from.position  = glm::vec3(0.0f, 0.0f, 0.0f);
    from.fov       = 60.0f;
    from.orthoSize = 5.0f;
    from.nearPlane = 0.1f;
    from.farPlane  = 100.0f;

    CameraViewOutput to;
    to.position  = glm::vec3(10.0f, 0.0f, 0.0f);
    to.fov       = 90.0f;
    to.orthoSize = 15.0f;
    to.nearPlane = 1.0f;
    to.farPlane  = 500.0f;

    CameraViewOutput mid = blendCameraView(from, to, 0.5f);
    EXPECT_NEAR(mid.position.x,  5.0f, kEps);
    EXPECT_NEAR(mid.fov,         75.0f, kEps);
    EXPECT_NEAR(mid.orthoSize,   10.0f, kEps);
    EXPECT_NEAR(mid.nearPlane,   0.55f, kEps);
    EXPECT_NEAR(mid.farPlane,    300.0f, kEps);
}

TEST(BlendCameraView, OrientationSlerpsToMidpoint)
{
    CameraViewOutput from;
    from.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // identity

    CameraViewOutput to;
    // 90° rotation around Y.
    to.orientation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

    CameraViewOutput mid = blendCameraView(from, to, 0.5f);
    // Expected: 45° rotation around Y.
    const glm::quat expected = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
    EXPECT_NEAR(mid.orientation.x, expected.x, kEps);
    EXPECT_NEAR(mid.orientation.y, expected.y, kEps);
    EXPECT_NEAR(mid.orientation.z, expected.z, kEps);
    EXPECT_NEAR(mid.orientation.w, expected.w, kEps);
}

TEST(BlendCameraView, ProjectionTypeSnapsAtMidpoint)
{
    CameraViewOutput from; from.projection = ProjectionType::PERSPECTIVE;
    CameraViewOutput to;   to.projection   = ProjectionType::ORTHOGRAPHIC;

    EXPECT_EQ(blendCameraView(from, to, 0.25f).projection, ProjectionType::PERSPECTIVE);
    EXPECT_EQ(blendCameraView(from, to, 0.49f).projection, ProjectionType::PERSPECTIVE);
    EXPECT_EQ(blendCameraView(from, to, 0.50f).projection, ProjectionType::ORTHOGRAPHIC);
    EXPECT_EQ(blendCameraView(from, to, 0.75f).projection, ProjectionType::ORTHOGRAPHIC);
}

TEST(BlendCameraView, TClampsOutOfRangeInputs)
{
    CameraViewOutput from;
    from.fov = 60.0f;
    CameraViewOutput to;
    to.fov = 90.0f;

    // Negative t clamps to 0 → returns `from`.
    EXPECT_EQ(blendCameraView(from, to, -0.5f), from);
    // t > 1 clamps to 1 → returns `to`.
    EXPECT_EQ(blendCameraView(from, to, 2.0f), to);
}

// -- CameraMode base-class contract -------------------------------

TEST(CameraModeBase, StubSubclassCompilesAndClones)
{
    StubCameraMode stub;

    // computeOutput returns the authored values.
    CameraInputs inputs;
    CameraViewOutput out = stub.computeOutput(inputs, 0.016f);
    EXPECT_EQ(out.position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(out.fov, 80.0f, kEps);

    // type() identifies the concrete mode.
    EXPECT_EQ(stub.type(), CameraModeType::FirstPerson);

    // clone() round-trips into a new instance (deep copy).
    std::unique_ptr<Component> copy = stub.clone();
    ASSERT_NE(copy, nullptr);
    auto* clonedMode = dynamic_cast<StubCameraMode*>(copy.get());
    ASSERT_NE(clonedMode, nullptr);
    CameraViewOutput clonedOut = clonedMode->computeOutput(inputs, 0.016f);
    EXPECT_EQ(clonedOut, out);
}

TEST(CameraModeBase, CameraInputsDefaultsAreNullAndNeutral)
{
    CameraInputs inputs;
    EXPECT_EQ(inputs.target,   nullptr);
    EXPECT_EQ(inputs.actions,  nullptr);
    EXPECT_EQ(inputs.physics,  nullptr);
    EXPECT_EQ(inputs.mouseLookDelta, glm::vec2(0.0f));
    EXPECT_NEAR(inputs.aspectRatio, 16.0f / 9.0f, kEps);
}
