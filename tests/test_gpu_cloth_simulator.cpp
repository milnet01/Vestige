// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_cloth_simulator.cpp
/// @brief CPU-side tests for the Phase 9B GpuClothSimulator skeleton.
///
/// These tests exercise the no-GL-context path: default state, polymorphic
/// construction, isSupported() probe behaviour. The GL-resident behaviour
/// (SSBO allocation, GL error checks, no-op simulate) is verified by
/// running the engine itself — no test in this suite establishes a GL
/// context (`grep glfwInit tests/`), and adding one purely for cloth tests
/// would duplicate the manual-launch verification path.

#include <gtest/gtest.h>

#include "physics/cloth_solver_backend.h"
#include "physics/gpu_cloth_simulator.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace Vestige;

namespace
{

/// Phase 10.9 Sh2/Sh3 — read a cloth shader file from the source tree.
/// `VESTIGE_SHADER_DIR` is wired by tests/CMakeLists.txt to
/// `${CMAKE_SOURCE_DIR}/assets/shaders` so the test runs from any cwd.
std::string readShaderSource(const char* basename)
{
    std::filesystem::path p = std::filesystem::path(VESTIGE_SHADER_DIR) / basename;
    std::ifstream in(p);
    EXPECT_TRUE(in.good()) << "Could not open shader file: " << p.string();
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Slice the plane-collider for-loop body out of `cloth_collision.comp.glsl`
/// so the Sh2 parity assertion only inspects that block (the sphere /
/// ground branches legitimately use `collisionMargin`).
std::string collisionShaderPlaneBlock()
{
    const std::string src = readShaderSource("cloth_collision.comp.glsl");
    const auto begin = src.find("// Plane colliders");
    const auto end   = src.find("positions[id].xyz", begin);
    EXPECT_NE(begin, std::string::npos) << "plane-collider block marker missing";
    EXPECT_NE(end,   std::string::npos);
    return src.substr(begin, end - begin);
}

} // namespace

TEST(GpuClothSimulator, DefaultStateMatchesUninitialised)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.isInitialized());
    EXPECT_EQ(sim.getParticleCount(), 0u);
    EXPECT_EQ(sim.getGridWidth(),  0u);
    EXPECT_EQ(sim.getGridHeight(), 0u);
    EXPECT_EQ(sim.getPositions(),  nullptr);
    EXPECT_EQ(sim.getNormals(),    nullptr);
    EXPECT_TRUE(sim.getIndices().empty());
    EXPECT_TRUE(sim.getTexCoords().empty());
}

TEST(GpuClothSimulator, IsSupportedReturnsFalseWithoutGlContext)
{
    // The unit-test binary doesn't establish a GL context. The probe must
    // gracefully report "not supported" rather than crash on glGetIntegerv.
    EXPECT_FALSE(GpuClothSimulator::isSupported());
}

TEST(GpuClothSimulator, IsAnIClothSolverBackend)
{
    // Compile-time check: GpuClothSimulator slots into the same interface
    // as ClothSimulator so ClothComponent can swap backends in a later step.
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<GpuClothSimulator>();
    EXPECT_NE(backend.get(), nullptr);
    EXPECT_FALSE(backend->isInitialized());
    EXPECT_EQ(backend->getParticleCount(), 0u);
}

TEST(GpuClothSimulator, BufferBindingsMatchDesignDoc)
{
    // The SSBO binding indices form part of the cross-component GLSL
    // contract (the future cloth_*.comp.glsl shaders bind to these
    // explicit numbers). Pin them so a reordering doesn't silently
    // shift the binding layout out from under the shaders.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_POSITIONS),      0u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_PREV_POSITIONS), 1u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_VELOCITIES),     2u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_NORMALS),        6u);
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_INDICES),        7u);
}

TEST(GpuClothSimulator, SsboHandlesAreZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getPositionsSSBO(),     0u);
    EXPECT_EQ(sim.getPrevPositionsSSBO(), 0u);
    EXPECT_EQ(sim.getVelocitiesSSBO(),    0u);
    EXPECT_EQ(sim.getNormalsSSBO(),       0u);
    EXPECT_EQ(sim.getIndicesSSBO(),       0u);
}

// -- Step 3 surface --

TEST(GpuClothSimulator, HasShadersDefaultsFalse)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.hasShaders());
}

TEST(GpuClothSimulator, ParameterSettersCompileAndAccept)
{
    // Smoke-check the per-frame parameter setters added in Step 3 — the
    // shaders consume these uniforms but the C++ accessors are pure
    // member writes so they're testable without a GL context.
    GpuClothSimulator sim;
    sim.setShaderPath("/dev/null/shaders");   // No load attempted yet
    sim.setDragCoefficient(0.5f);
    sim.setWindVelocity(glm::vec3(1.0f, 0.0f, 0.0f));
    sim.setDamping(0.02f);
    SUCCEED();
}

// -- Step 4 surface --

TEST(GpuClothSimulator, ConstraintCountIsZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getConstraintCount(), 0u);
    EXPECT_EQ(sim.getColourCount(),     0u);
    EXPECT_EQ(sim.getConstraintsSSBO(), 0u);
}

TEST(GpuClothSimulator, SetSubstepsClampsToOne)
{
    GpuClothSimulator sim;
    sim.setSubsteps(0);
    EXPECT_EQ(sim.getSubsteps(), 1) << "substep count must be clamped to >= 1";
    sim.setSubsteps(-3);
    EXPECT_EQ(sim.getSubsteps(), 1);
    sim.setSubsteps(20);
    EXPECT_EQ(sim.getSubsteps(), 20);
}

// Phase 10.9 Cl7: GPU backend must share the CPU simulate() upper bound.
// Pre-Cl7, GPU had no upper cap (only a `if (substeps < 1) substeps = 1`
// lower clamp); CPU silently clamped to 64 inside the per-frame loop.
TEST(GpuClothSimulator, SetSubstepsClampsToMaxSubsteps_Cl7)
{
    GpuClothSimulator sim;
    sim.setSubsteps(MAX_SUBSTEPS + 1000);
    EXPECT_EQ(sim.getSubsteps(), MAX_SUBSTEPS)
        << "GPU substep count must be clamped to MAX_SUBSTEPS to match CPU simulate()";
    sim.setSubsteps(MAX_SUBSTEPS);
    EXPECT_EQ(sim.getSubsteps(), MAX_SUBSTEPS);
}

// Phase 10.9 Cl4 — dihedral compliance is now a live runtime knob on the
// GPU backend (mirrors the CPU surface). The SSBO re-upload requires GL
// context; the C++ getter / clamp / mirror-update paths are testable
// headlessly. Visual GPU parity is verified at engine launch.
TEST(GpuClothSimulator, DefaultDihedralComplianceMatchesCpuDefault_Cl4)
{
    GpuClothSimulator sim;
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.01f);
}

TEST(GpuClothSimulator, SetDihedralComplianceUpdatesGetter_Cl4)
{
    GpuClothSimulator sim;
    sim.setDihedralBendCompliance(0.05f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.05f);
    sim.setDihedralBendCompliance(0.0f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.0f);
}

TEST(GpuClothSimulator, SetDihedralComplianceClampsNegativeToZero_Cl4)
{
    // Mirrors ClothSimulator::setDihedralBendCompliance — negative input
    // would flip the sign of the XPBD restoring force; clamp instead.
    GpuClothSimulator sim;
    sim.setDihedralBendCompliance(-0.5f);
    EXPECT_FLOAT_EQ(sim.getDihedralBendCompliance(), 0.0f);
}

TEST(GpuClothSimulator, BindConstraintsEnumPinned)
{
    // Constraints SSBO binding 4 is the contract with the cloth_constraints
    // compute shader. Pin it so a refactor can't silently shift it.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_CONSTRAINTS), 4u);
}

// -- Step 6 surface --

TEST(GpuClothSimulator, BindDihedralsEnumPinned)
{
    // Dihedral SSBO binding 5 is the contract with cloth_dihedral.comp.glsl.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_DIHEDRALS), 5u);
}

TEST(GpuClothSimulator, DihedralCountAndColoursZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getDihedralCount(),       0u);
    EXPECT_EQ(sim.getDihedralColourCount(), 0u);
    EXPECT_EQ(sim.getDihedralsSSBO(),       0u);
}

// -- Step 7 surface --

TEST(GpuClothSimulator, ColliderDefaults)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
    EXPECT_EQ(sim.getPlaneColliderCount(),  0u);
    EXPECT_FLOAT_EQ(sim.getCollisionMargin(), 0.015f);
    EXPECT_LT(sim.getGroundPlane(), -100.0f) << "ground default should be far below scene";
}

TEST(GpuClothSimulator, AddSphereColliderIncrementsCount)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f, 1.0f, 0.0f), 0.5f);
    EXPECT_EQ(sim.getSphereColliderCount(), 1u);
    sim.addSphereCollider(glm::vec3(2.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_EQ(sim.getSphereColliderCount(), 2u);
}

TEST(GpuClothSimulator, AddSphereColliderRejectsZeroRadius)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f), 0.0f);
    sim.addSphereCollider(glm::vec3(0.0f), -1.0f);
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
}

TEST(GpuClothSimulator, ClearSphereCollidersResetsCount)
{
    GpuClothSimulator sim;
    sim.addSphereCollider(glm::vec3(0.0f), 0.5f);
    sim.addSphereCollider(glm::vec3(1.0f), 0.5f);
    sim.clearSphereColliders();
    EXPECT_EQ(sim.getSphereColliderCount(), 0u);
}

TEST(GpuClothSimulator, AddPlaneColliderRejectsZeroNormal)
{
    GpuClothSimulator sim;
    EXPECT_FALSE(sim.addPlaneCollider(glm::vec3(0.0f), 1.0f));
    EXPECT_EQ(sim.getPlaneColliderCount(), 0u);
    EXPECT_TRUE(sim.addPlaneCollider(glm::vec3(0.0f, 1.0f, 0.0f), 0.0f));
    EXPECT_EQ(sim.getPlaneColliderCount(), 1u);
}

TEST(GpuClothSimulator, BindCollidersUboEnumPinned)
{
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_COLLIDERS_UBO), 3u);
}

// -- Step 9 surface (pins + LRA, CPU-side state only) --

// -- Phase 10.9 Sh3: Coulomb friction --
//
// CPU `ClothSimulator` exposes static + kinetic friction; pre-Sh3 the GPU
// backend ignored both (no friction in cloth_collision.comp.glsl, no setter
// in IClothSolverBackend, no fields in the Colliders UBO). Sliding particles
// behaved completely differently across backends. These tests pin the C++
// surface — the shader + UBO upload paths are verified at engine launch.

TEST(GpuClothSimulator, FrictionDefaultsMatchCpuBackend_Sh3)
{
    // Default 0.4 / 0.3 picked at the GpuClothSimulator member declaration
    // mirrors `ClothSimulator::m_staticFriction` / `m_kineticFriction`.
    GpuClothSimulator sim;
    EXPECT_FLOAT_EQ(sim.getStaticFriction(),  0.4f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.3f);
}

TEST(GpuClothSimulator, SetFrictionUpdatesGetters_Sh3)
{
    GpuClothSimulator sim;
    sim.setFriction(0.6f, 0.5f);
    EXPECT_FLOAT_EQ(sim.getStaticFriction(),  0.6f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.5f);
}

TEST(GpuClothSimulator, SetFrictionClampsNegativesToZero_Sh3)
{
    // Negative coefficient would invert the friction direction. Match the
    // CPU clamp at `cloth_simulator.cpp:888-889` so backends agree.
    GpuClothSimulator sim;
    sim.setFriction(-0.1f, -0.2f);
    EXPECT_FLOAT_EQ(sim.getStaticFriction(),  0.0f);
    EXPECT_FLOAT_EQ(sim.getKineticFriction(), 0.0f);
}

TEST(GpuClothSimulator, FrictionSetterIsBackendInterfaceMember_Sh3)
{
    // The interface gets the setter so callers can drive both backends with
    // a single `IClothSolverBackend*`. Pre-Sh3 the GPU backend type-checked
    // because the setter only existed on the concrete CPU class — the
    // ClothComponent inspector silently lost friction control on the GPU
    // backend. Compile-time interface check + value-equality through pointer.
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<GpuClothSimulator>();
    backend->setFriction(0.7f, 0.6f);
    EXPECT_FLOAT_EQ(backend->getStaticFriction(),  0.7f);
    EXPECT_FLOAT_EQ(backend->getKineticFriction(), 0.6f);
}

TEST(GpuClothSimulator, PinDefaultsAreZero)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getPinnedCount(), 0u);
    EXPECT_EQ(sim.getLraCount(),    0u);
    EXPECT_FALSE(sim.isParticlePinned(0));
}

TEST(GpuClothSimulator, BindLrasEnumPinned)
{
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_LRAS), 8u);
}

TEST(ClothConstraintGraph, GenerateLraEmptyForNoPins)
{
    std::vector<glm::vec3> positions(16, glm::vec3(0.0f));
    std::vector<uint32_t>  pins;
    std::vector<GpuLraConstraint> lras;
    generateLraConstraints(positions, pins, lras);
    EXPECT_TRUE(lras.empty());
}

TEST(ClothConstraintGraph, GenerateLraTethersEveryFreeParticle)
{
    // 4 particles in a line; pin index 0; expect 3 LRA tethers all referencing pin 0.
    std::vector<glm::vec3> positions = {
        {0,0,0}, {1,0,0}, {2,0,0}, {3,0,0}
    };
    std::vector<uint32_t> pins = {0};
    std::vector<GpuLraConstraint> lras;
    generateLraConstraints(positions, pins, lras);

    ASSERT_EQ(lras.size(), 3u);
    for (const auto& l : lras)
    {
        EXPECT_EQ(l.pinIndex, 0u);
        EXPECT_NE(l.particleIndex, 0u);
    }
    EXPECT_FLOAT_EQ(lras[0].maxDistance, 1.0f);
    EXPECT_FLOAT_EQ(lras[1].maxDistance, 2.0f);
    EXPECT_FLOAT_EQ(lras[2].maxDistance, 3.0f);
}

// -- Phase 10.9 Sh2: cloth_collision plane-collider parity --
//
// CPU `ClothSimulator::applyCollisions` at `cloth_simulator.cpp:1163-1190`
// pushes plane penetration with `dist < 0` and corrects by `-n * dist` (no
// margin). The CPU comment explicitly says "no margin for planes — injects
// energy, causes drift". Pre-Sh2 the GPU shader added `collisionMargin` to
// the plane penetration (`pen = collisionMargin - signedDist`) which made
// panels drift on flat tabletops. There is no GL test context, so the
// shader source IS the spec for the parity contract — we read the file.

TEST(ClothCollisionShader, PlaneBlockHasNoMarginInPenetration_Sh2)
{
    const std::string planeBlock = collisionShaderPlaneBlock();

    // The classic pre-Sh2 form is `pen = collisionMargin - signedDist`.
    // Reject any reference to `collisionMargin` inside the plane block —
    // sphere + ground branches still use it; planes do not.
    EXPECT_EQ(planeBlock.find("collisionMargin"), std::string::npos)
        << "plane-collider block must not reference collisionMargin "
           "(parity with cloth_simulator.cpp:1163-1166: no margin for planes)";

    // Confirm the canonical post-Sh2 form is present (pin against accidental
    // re-introduction of margin under a different variable name).
    EXPECT_NE(planeBlock.find("signedDist < 0.0"), std::string::npos);
}

// -- Phase 10.9 Sh3: cloth_collision Coulomb friction parity --
//
// CPU `ClothSimulator::applyCollisions` calls `applyFriction(velocity, n)`
// at every contact (ground / sphere / box / cylinder / plane). Pre-Sh3 the
// GPU shader had no friction term, so sliding particles behaved differently
// across backends. Each contact branch must call the helper.

TEST(ClothCollisionShader, AppliesFrictionAtGroundSphereAndPlane_Sh3)
{
    const std::string src = readShaderSource("cloth_collision.comp.glsl");

    EXPECT_NE(src.find("void applyFriction"), std::string::npos)
        << "cloth_collision.comp.glsl must define applyFriction(v, n)";

    // Count how many sites call the helper. Three legitimate branches on
    // the GPU side: ground, sphere loop, plane loop. (Cylinder / box are
    // CPU-only — see `GpuClothSimulator::addCylinderCollider` log-and-drop.)
    size_t callSites = 0;
    size_t pos = 0;
    while ((pos = src.find("applyFriction(v,", pos)) != std::string::npos)
    {
        ++callSites;
        pos += 1;
    }
    EXPECT_EQ(callSites, 3u)
        << "expected applyFriction(v, …) at ground + sphere + plane sites; "
           "found " << callSites;
}

TEST(ClothCollisionShader, CollidersUboExposesFrictionCoefficients_Sh3)
{
    // The std140 UBO contract is shared across the C++ packing in
    // `gpu_cloth_simulator.cpp::uploadCollidersIfDirty` and the shader
    // `Colliders` block. Pin the field names so a packing reorder doesn't
    // silently shift the layout.
    const std::string src = readShaderSource("cloth_collision.comp.glsl");
    EXPECT_NE(src.find("float staticFriction"),  std::string::npos);
    EXPECT_NE(src.find("float kineticFriction"), std::string::npos);
}
