/// @file physics_debug.cpp
/// @brief Debug wireframe visualization for physics collision shapes.
#include "physics/physics_debug.h"
#include "physics/jolt_helpers.h"

#include <Jolt/Physics/Body/BodyManager.h>

namespace Vestige
{

namespace
{

/// @brief Draws a wireframe box at the given position with half-extents.
void drawWireBox(const glm::vec3& pos, const glm::vec3& halfExtents,
                  const glm::vec3& color)
{
    // 8 corners in world space (axis-aligned)
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i)
    {
        corners[i] = pos + glm::vec3(
            (i & 1) ? halfExtents.x : -halfExtents.x,
            (i & 2) ? halfExtents.y : -halfExtents.y,
            (i & 4) ? halfExtents.z : -halfExtents.z);
    }

    // 12 edges
    int edges[12][2] = {
        {0,1},{2,3},{4,5},{6,7},  // X edges
        {0,2},{1,3},{4,6},{5,7},  // Y edges
        {0,4},{1,5},{2,6},{3,7}   // Z edges
    };

    for (const auto& e : edges)
    {
        DebugDraw::line(corners[e[0]], corners[e[1]], color);
    }
}

} // anonymous namespace

void PhysicsDebugDraw::draw(const PhysicsWorld& world, DebugDraw& debugDraw,
                             const Camera& camera, float aspectRatio)
{
    if (!m_enabled || !world.isInitialized())
    {
        return;
    }

    const JPH::PhysicsSystem* system = world.getSystem();
    JPH::BodyIDVector bodyIds;
    system->GetBodies(bodyIds);

    const JPH::BodyInterface& bodyInterface = world.getBodyInterface();

    for (const auto& id : bodyIds)
    {
        if (!bodyInterface.IsActive(id) &&
            bodyInterface.GetMotionType(id) != JPH::EMotionType::Static)
        {
            continue;
        }

        glm::vec3 pos = toGlm(bodyInterface.GetPosition(id));

        // Color by motion type
        glm::vec3 color;
        switch (bodyInterface.GetMotionType(id))
        {
        case JPH::EMotionType::Static:    color = glm::vec3(0.0f, 0.8f, 0.0f); break;
        case JPH::EMotionType::Kinematic: color = glm::vec3(0.8f, 0.8f, 0.0f); break;
        case JPH::EMotionType::Dynamic:   color = glm::vec3(0.0f, 0.4f, 0.8f); break;
        }

        // Use shape bounding box for visualization
        JPH::RefConst<JPH::Shape> shape = bodyInterface.GetShape(id);
        if (shape)
        {
            JPH::AABox localBounds = shape->GetLocalBounds();
            glm::vec3 halfExtents = toGlm(localBounds.GetExtent());
            drawWireBox(pos, halfExtents, color);
        }
    }

    // Flush all debug lines
    glm::mat4 vp = camera.getProjectionMatrix(aspectRatio) * camera.getViewMatrix();
    debugDraw.flush(vp);
}

} // namespace Vestige
