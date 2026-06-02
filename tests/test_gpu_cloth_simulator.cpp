// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file test_gpu_cloth_simulator.cpp
/// @brief CPU-side tests for the Phase 9B GpuClothSimulator skeleton.
///
/// Most tests here exercise no-GL-context paths: default state, polymorphic
/// construction, member setters / getters that don't touch GL. The
/// GL-resident behaviour (SSBO allocation, GL error checks, no-op simulate)
/// is verified by running the engine itself.
///
/// Note: as of the C-full shader-parity work
/// (`tests/gl_test_fixture.h`), the test binary now establishes a hidden
/// GLFW + OpenGL 4.5 core context at process start. Tests that previously
/// asserted "no GL = no crash" semantics for `isSupported()` were rewritten
/// to assert the contract that's actually testable now (`isSupported()`
/// returns true under a live 4.5-core context). The graceful no-context
/// path is exercised by GpuClothSimulator construction in builds where
/// the GL environment fails to initialise — `wasInitialized()` is false
/// in those runs and the assertion flips accordingly.

#include <gtest/gtest.h>

#include "gl_test_fixture.h"
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

TEST(GpuClothSimulator, IsSupportedMatchesGlContextAvailability)
{
    // Updated for the C-full shader-parity work (gl_test_fixture.h): the
    // test binary now creates an OpenGL 4.5 core context at process start
    // when the host display permits it. The contract the original test
    // pinned ("no-crash on isSupported() probe without a context") flips
    // depending on whether GLTestEnvironment succeeded — both branches
    // remain non-crashing, which is the underlying invariant.
    if (Vestige::Test::GLTestEnvironment::wasInitialized())
    {
        EXPECT_TRUE(GpuClothSimulator::isSupported())
            << "GL 4.5 core context is live; the SSBO/compute extensions "
               "cloth needs are part of core 4.3+ so this must be true";
    }
    else
    {
        EXPECT_FALSE(GpuClothSimulator::isSupported())
            << "no GL context: probe must gracefully return false, not "
               "crash on glGetIntegerv";
    }
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
    // The four setters below are pure member writes; reaching this line
    // is the entire contract being pinned — if any setter were renamed
    // or removed the test wouldn't compile. No further assertion needed.
    sim.setShaderPath("/dev/null/shaders");   // No load attempted yet
    sim.setDragCoefficient(0.5f);
    sim.setWindVelocity(glm::vec3(1.0f, 0.0f, 0.0f));
    sim.setDamping(0.02f);
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

// Phase 10.9 Sh4a — triangle SSBO + colour-range upload.
TEST(GpuClothSimulator, BindTrianglesEnumPinned)
{
    // Triangle SSBO binding 9 is the contract with cloth_wind_drag.comp.glsl.
    EXPECT_EQ(static_cast<GLuint>(GpuClothSimulator::BIND_TRIANGLES), 9u);
}

TEST(GpuClothSimulator, TriangleColourCountZeroBeforeInit)
{
    GpuClothSimulator sim;
    EXPECT_EQ(sim.getTriangleColourCount(), 0u);
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

// Phase 10.9 Slice 18 Ts3: the LRA tests moved to their canonical home
// at `tests/test_cloth_constraint_graph.cpp` — `generateLraConstraints`
// is a `cloth_constraint_graph.h` helper, not a `GpuClothSimulator`
// member. The misplaced suite name `ClothConstraintGraph` inside the
// GPU sim test file caused readers to grep here for graph tests.

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

// -- Phase 10.9 Cl5: reset() / captureRestPositions() rest-snapshot semantics --
//
// Pre-Cl5 the GPU `reset()` re-uploaded the *current* `m_positionMirror`
// instead of an immutable rest snapshot. Two paths mutate the mirror:
// (1) `setPinPosition(i, p)` writes `p` into `m_positionMirror[i]` directly,
// (2) `getPositions()` triggers a SSBO readback that overwrites the mirror
// with the post-simulate state. Either makes `reset()` snap to the latest
// mutation rather than the original grid — exactly the bug the ROADMAP entry
// pins ("Pinned particles currently snap to last-pinned-position, not initial
// grid"). Mirrors the CPU contract at `cloth_simulator.cpp:206 / 720 / 689`:
// `m_initialPositions` is the rest snapshot, captured at init and refreshable
// via `captureRestPositions()`. The GPU stub `captureRestPositions() = {}`
// (header line 95) silently drops the call.

namespace
{

ClothConfig clothCl5Config(uint32_t w = 4, uint32_t h = 4)
{
    ClothConfig cfg;
    cfg.width            = w;
    cfg.height           = h;
    cfg.spacing          = 1.0f;
    cfg.particleMass     = 1.0f;
    cfg.substeps         = 5;
    cfg.stretchCompliance = 0.0f;
    cfg.shearCompliance   = 0.0001f;
    cfg.bendCompliance    = 0.01f;
    cfg.damping           = 0.01f;
    return cfg;
}

}  // namespace

class GpuClothResetSemantics : public ::Vestige::Test::GLTestFixture {};

TEST_F(GpuClothResetSemantics, ResetRestoresInitialGridIgnoringSetPinPosition_Cl5)
{
    // Reproduce the headline ROADMAP bug: pin a particle, move it via
    // setPinPosition, reset → expect the particle back at its original grid
    // location. Pre-Cl5 reset() reads from the mirror that setPinPosition
    // mutated, so the particle stays at the moved-to coordinate.
    GpuClothSimulator sim;
    sim.initialize(clothCl5Config());
    ASSERT_TRUE(sim.isInitialized());

    const glm::vec3* posBefore = sim.getPositions();
    ASSERT_NE(posBefore, nullptr);
    const glm::vec3 originalGridPos = posBefore[0];
    // Sanity: 4x4 grid centred at origin, spacing 1 → particle 0 at (-1.5,0,-1.5).
    ASSERT_FLOAT_EQ(originalGridPos.x, -1.5f);
    ASSERT_FLOAT_EQ(originalGridPos.y,  0.0f);
    ASSERT_FLOAT_EQ(originalGridPos.z, -1.5f);

    ASSERT_TRUE(sim.pinParticle(0, originalGridPos));
    sim.setPinPosition(0, glm::vec3(0.0f, 5.0f, 0.0f));

    sim.reset();

    const glm::vec3* posAfter = sim.getPositions();
    ASSERT_NE(posAfter, nullptr);
    EXPECT_FLOAT_EQ(posAfter[0].x, originalGridPos.x);
    EXPECT_FLOAT_EQ(posAfter[0].y, originalGridPos.y);
    EXPECT_FLOAT_EQ(posAfter[0].z, originalGridPos.z);
}

TEST_F(GpuClothResetSemantics, CaptureRestPositionsRefreshesSnapshotForReset_Cl5)
{
    // After captureRestPositions(), reset() must restore to the captured
    // pose, not the originally-built grid. Pre-Cl5 the override is `{}` so
    // captureRestPositions has no effect: reset still reads from whatever
    // the mirror happens to hold, which is the most recent setPinPosition.
    GpuClothSimulator sim;
    sim.initialize(clothCl5Config());
    ASSERT_TRUE(sim.isInitialized());

    const glm::vec3 capturedPin(2.0f, 1.0f, 3.0f);
    const glm::vec3 laterPin  (9.0f, 9.0f, 9.0f);

    ASSERT_TRUE(sim.pinParticle(0, capturedPin));
    sim.captureRestPositions();   // Pre-Cl5: no-op stub.
    sim.setPinPosition(0, laterPin);

    sim.reset();

    const glm::vec3* posAfter = sim.getPositions();
    ASSERT_NE(posAfter, nullptr);
    EXPECT_FLOAT_EQ(posAfter[0].x, capturedPin.x);
    EXPECT_FLOAT_EQ(posAfter[0].y, capturedPin.y);
    EXPECT_FLOAT_EQ(posAfter[0].z, capturedPin.z);
}

// /test-audit 2026-05-17 Ts19-CG2: reset() before initialize() is a
// documented no-op (the implementation guards on `m_initialized`). Pin
// the contract so a regression that drops the guard (and ends up
// dispatching compute shaders against unallocated buffers) is caught at
// test time, not at first user click on a fresh editor scene.
TEST_F(GpuClothResetSemantics, ResetBeforeInitIsNoOp)
{
    GpuClothSimulator sim;
    ASSERT_FALSE(sim.isInitialized());

    EXPECT_NO_THROW(sim.reset());

    // Still uninitialised — reset() must not have flipped the flag.
    EXPECT_FALSE(sim.isInitialized());
    EXPECT_EQ(sim.getPositions(), nullptr);
}

TEST_F(GpuClothResetSemantics, ResetThroughBackendInterface_Cl5)
{
    // Same contract as the headline test, exercised through the
    // `IClothSolverBackend*` polymorphic surface — the inspector / cloth
    // component drives backends through this interface, so the rest-pose
    // semantics must hold there too.
    std::unique_ptr<IClothSolverBackend> backend = std::make_unique<GpuClothSimulator>();
    backend->initialize(clothCl5Config());
    ASSERT_TRUE(backend->isInitialized());

    const glm::vec3* posBefore = backend->getPositions();
    ASSERT_NE(posBefore, nullptr);
    const glm::vec3 originalGridPos = posBefore[0];

    ASSERT_TRUE(backend->pinParticle(0, originalGridPos));
    backend->setPinPosition(0, glm::vec3(7.0f, 7.0f, 7.0f));

    backend->reset();

    const glm::vec3* posAfter = backend->getPositions();
    ASSERT_NE(posAfter, nullptr);
    EXPECT_FLOAT_EQ(posAfter[0].x, originalGridPos.x);
    EXPECT_FLOAT_EQ(posAfter[0].y, originalGridPos.y);
    EXPECT_FLOAT_EQ(posAfter[0].z, originalGridPos.z);
}

// ---------------------------------------------------------------------------
// Phase 10.9 Sh4a — per-triangle aerodynamic drag parity test
// ---------------------------------------------------------------------------
//
// Isolates `cloth_wind_drag.comp.glsl`: builds a small vertical-plane cloth,
// colours its triangles with the same `colourTriangleConstraints` the runtime
// uses, uploads the three SSBOs the shader reads (positions w/ invMass,
// velocities, triangles), dispatches the drag shader once per colour, then
// compares the read-back velocities to a CPU reference that replays the
// APPROXIMATE `ClothSimulator::applyWind` math in index order.
//
// Parity is asserted to 1e-5 m/s — ~10 ULPs of FP32 at velocities of order
// 1 m/s, which covers the FP-commutativity-only summation-order differences
// between the CPU's index-order walk and the GPU's colour-bucket-order walk
// (the design doc's §Caveat: cross-run GPU determinism, NOT bit-equality
// with the CPU).

namespace
{

// CPU reference: one APPROXIMATE per-triangle drag pass over `indices`,
// starting from `vel` (mutated in place). Mirrors cloth_simulator.cpp:2008-2052.
void cpuApplyWindApproximate(const std::vector<uint32_t>& indices,
                             const std::vector<glm::vec3>& pos,
                             const std::vector<float>& invMass,
                             const glm::vec3& windVel, float drag, float dt,
                             std::vector<glm::vec3>& vel)
{
    const float dtOver3 = dt / 3.0f;
    const size_t triCount = indices.size() / 3;
    for (size_t ti = 0; ti < triCount; ++ti)
    {
        const uint32_t i0 = indices[ti * 3];
        const uint32_t i1 = indices[ti * 3 + 1];
        const uint32_t i2 = indices[ti * 3 + 2];

        const glm::vec3 vAvg = (vel[i0] + vel[i1] + vel[i2]) / 3.0f;
        const glm::vec3 vRel = windVel - vAvg;

        const glm::vec3 e1 = pos[i1] - pos[i0];
        const glm::vec3 e2 = pos[i2] - pos[i0];
        const glm::vec3 cr = glm::cross(e1, e2);
        const float area2 = glm::length(cr);
        if (area2 < 1e-7f) continue;

        const glm::vec3 normal = cr / area2;
        const float area = area2 * 0.5f;
        const float vDotN = glm::dot(vRel, normal);
        const glm::vec3 force = normal * (0.5f * drag * area * vDotN);
        const glm::vec3 pv = force * dtOver3;

        if (invMass[i0] > 0.0f) vel[i0] += pv * invMass[i0];
        if (invMass[i1] > 0.0f) vel[i1] += pv * invMass[i1];
        if (invMass[i2] > 0.0f) vel[i2] += pv * invMass[i2];
    }
}

}  // namespace

class WindDragParityTest : public ::Vestige::Test::GLTestFixture {};

TEST_F(WindDragParityTest, Sh4a_SingleSubstep_PerpendicularWind_MatchesCpuReference)
{
    // Vertical 3x3 cloth in the YZ plane (face normal along X); +X wind blows
    // perpendicular to the face — the strongest-drag configuration, and the
    // one whose response shows up in velocity.x (matching the design's
    // sign(velocity[centre].x) == sign(windDir.x) check). A cloth in the XY
    // plane would feel zero drag from +X wind (wind parallel to the face,
    // dot(vRel, normal) == 0).
    constexpr uint32_t W = 3, H = 3;
    const glm::vec3 WIND{10.0f, 0.0f, 0.0f};
    constexpr float DRAG = 1.0f;
    constexpr float DT   = 0.016f;

    std::vector<glm::vec3> pos(W * H);
    for (uint32_t z = 0; z < H; ++z)
        for (uint32_t x = 0; x < W; ++x)
            pos[z * W + x] = glm::vec3(0.0f, float(z), float(x));

    std::vector<float> invMass(W * H, 1.0f);  // all free

    // NW-SE triangulation, matching the runtime grid index buffer.
    std::vector<uint32_t> indices;
    for (uint32_t z = 0; z + 1 < H; ++z)
        for (uint32_t x = 0; x + 1 < W; ++x)
        {
            const uint32_t i0 = z * W + x, i1 = z * W + (x + 1);
            const uint32_t i2 = (z + 1) * W + x, i3 = (z + 1) * W + (x + 1);
            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
        }

    // Colour the triangles exactly as the runtime does.
    std::vector<GpuTriangle> tris;
    const auto ranges = colourTriangleConstraints(indices, W * H, tris);
    ASSERT_FALSE(ranges.empty());

    // CPU reference walks the triangles in the SAME colour-bucket order the
    // GPU dispatches them, so it models the GPU's update ordering rather than
    // the original ClothSimulator's index-order Gauss-Seidel. This matters
    // because each triangle's vAvg reads the current vertex velocities: index
    // order and colour order see different intermediate states (vRel = wind -
    // vAvg, so even a small vAvg difference scales by |wind|). Within a colour
    // the triangles are vertex-disjoint, so sequential and parallel give
    // identical results — making the colour-ordered CPU walk an exact oracle
    // for the GPU's per-colour dispatch. (GPU-vs-ClothSimulator full-loop
    // parity, across the differing orders, is Cl1's job at a 1e-3 tolerance.)
    std::vector<uint32_t> colourOrderedIndices;
    colourOrderedIndices.reserve(tris.size() * 3);
    for (const GpuTriangle& t : tris)
    {
        colourOrderedIndices.push_back(t.i0);
        colourOrderedIndices.push_back(t.i1);
        colourOrderedIndices.push_back(t.i2);
    }
    std::vector<glm::vec3> cpuVel(W * H, glm::vec3(0.0f));
    cpuApplyWindApproximate(colourOrderedIndices, pos, invMass, WIND, DRAG, DT, cpuVel);

    const uint32_t centreIdx = 1 * W + 1;
    ASSERT_GT(cpuVel[centreIdx].x, 0.0f)
        << "CPU reference sanity: centre particle must gain +X velocity";

    // Load the shader under test.
    Shader drag;
    const std::string dragPath = std::string(VESTIGE_SHADER_DIR) + "/cloth_wind_drag.comp.glsl";
    ASSERT_TRUE(drag.loadComputeShader(dragPath)) << "Failed to load " << dragPath;

    // Upload SSBOs: positions (xyz + invMass in w), velocities (zero), triangles.
    std::vector<glm::vec4> posBuf(W * H);
    std::vector<glm::vec4> velBuf(W * H, glm::vec4(0.0f));
    for (uint32_t i = 0; i < W * H; ++i)
        posBuf[i] = glm::vec4(pos[i], invMass[i]);

    GLuint posSSBO = 0, velSSBO = 0, triSSBO = 0;
    glCreateBuffers(1, &posSSBO);
    glCreateBuffers(1, &velSSBO);
    glCreateBuffers(1, &triSSBO);
    glNamedBufferStorage(posSSBO, GLsizeiptr(posBuf.size() * sizeof(glm::vec4)), posBuf.data(), 0);
    glNamedBufferStorage(velSSBO, GLsizeiptr(velBuf.size() * sizeof(glm::vec4)), velBuf.data(), GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT);
    glNamedBufferStorage(triSSBO, GLsizeiptr(tris.size() * sizeof(GpuTriangle)), tris.data(), 0);

    drag.use();
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GpuClothSimulator::BIND_POSITIONS,  posSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GpuClothSimulator::BIND_VELOCITIES, velSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, GpuClothSimulator::BIND_TRIANGLES,  triSSBO);
    drag.setVec3 ("u_windVelocity", WIND);
    drag.setFloat("u_dragCoeff",    DRAG);
    drag.setFloat("u_deltaTime",    DT);

    // One dispatch per colour — disjoint vertex writes within a colour.
    for (const auto& r : ranges)
    {
        if (r.count == 0) continue;
        drag.setUInt("u_firstTri", r.offset);
        drag.setUInt("u_triCount", r.count);
        glDispatchCompute((r.count + 63u) / 64u, 1, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    }

    std::vector<glm::vec4> gpuVel(W * H, glm::vec4(0.0f));
    glGetNamedBufferSubData(velSSBO, 0, GLsizeiptr(gpuVel.size() * sizeof(glm::vec4)), gpuVel.data());

    glDeleteBuffers(1, &posSSBO);
    glDeleteBuffers(1, &velSSBO);
    glDeleteBuffers(1, &triSSBO);

    constexpr float TOL = 1e-5f;
    for (uint32_t i = 0; i < W * H; ++i)
    {
        EXPECT_NEAR(gpuVel[i].x, cpuVel[i].x, TOL) << "velocity.x mismatch at particle " << i;
        EXPECT_NEAR(gpuVel[i].y, cpuVel[i].y, TOL) << "velocity.y mismatch at particle " << i;
        EXPECT_NEAR(gpuVel[i].z, cpuVel[i].z, TOL) << "velocity.z mismatch at particle " << i;
    }
    EXPECT_GT(gpuVel[centreIdx].x, 0.0f)
        << "Centre particle must gain +X velocity from +X wind (sign check)";
}
