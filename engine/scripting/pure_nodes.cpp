/// @file pure_nodes.cpp
/// @brief Pure data nodes — lazy-evaluated math, vector, boolean, comparison
/// operations plus read-only entity queries.
///
/// Pure nodes have no execution pins. They're evaluated on demand when a
/// connected impure node reads their output. This means a pure node may
/// execute many times per frame if referenced inside loops — document this
/// behavior in the editor UI so designers understand the cost.
#include "scripting/pure_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"
#include "core/engine.h"
#include "scene/entity.h"
#include "scene/scene.h"
#include "physics/physics_world.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace Vestige
{

void registerPureNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // Entity queries
    // -----------------------------------------------------------------------
    registry.registerNode({
        "GetPosition",
        "Get Position",
        "Entity",
        "Gets an entity's local position",
        {{PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)}},
        {{PinKind::DATA, "position", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))}},
        "",
        true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            uint32_t id = ctx.readInputAs<uint32_t>(node, "entity");
            glm::vec3 pos(0.0f);
            if (Entity* e = ctx.resolveEntity(id))
            {
                pos = e->transform.position;
            }
            ctx.setOutput(node, "position", ScriptValue(pos));
        }
    });

    registry.registerNode({
        "GetRotation",
        "Get Rotation",
        "Entity",
        "Gets an entity's local rotation (Euler degrees)",
        {{PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)}},
        {{PinKind::DATA, "rotation", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))}},
        "",
        true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            uint32_t id = ctx.readInputAs<uint32_t>(node, "entity");
            glm::vec3 rot(0.0f);
            if (Entity* e = ctx.resolveEntity(id))
            {
                rot = e->transform.rotation;
            }
            ctx.setOutput(node, "rotation", ScriptValue(rot));
        }
    });

    registry.registerNode({
        "FindEntityByName",
        "Find Entity By Name",
        "Entity",
        "Finds an entity by name (first match in the active scene)",
        {{PinKind::DATA, "name", ScriptDataType::STRING, ScriptValue(std::string(""))}},
        {{PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)}},
        "",
        true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "name");
            ScriptValue result = ScriptValue::entityId(0);
            if (auto* scene = ctx.activeScene(); scene && !name.empty())
            {
                if (Entity* e = scene->findEntity(name))
                {
                    result = ScriptValue::entityId(e->getId());
                }
            }
            ctx.setOutput(node, "entity", result);
        }
    });

    // -----------------------------------------------------------------------
    // Math — arithmetic (operates on floats)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "MathAdd", "Math Add", "Math",
        "Adds two numbers (A + B)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a + b));
        }
    });

    registry.registerNode({
        "MathSub", "Math Subtract", "Math",
        "Subtracts B from A (A - B)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a - b));
        }
    });

    registry.registerNode({
        "MathMul", "Math Multiply", "Math",
        "Multiplies two numbers (A * B)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(1.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a * b));
        }
    });

    registry.registerNode({
        "MathDiv", "Math Divide", "Math",
        "Divides A by B (A / B, returns 0 if B is 0)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            float r = (std::abs(b) < 1e-9f) ? 0.0f : (a / b);
            ctx.setOutput(node, "Result", ScriptValue(r));
        }
    });

    registry.registerNode({
        "MathClamp", "Math Clamp", "Math",
        "Clamps a value to the range [Min, Max]",
        {
            {PinKind::DATA, "Value", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "Min", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "Max", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float v = ctx.readInputAs<float>(node, "Value");
            float mn = ctx.readInputAs<float>(node, "Min");
            float mx = ctx.readInputAs<float>(node, "Max");
            if (mx < mn) std::swap(mn, mx);
            float r = std::max(mn, std::min(v, mx));
            ctx.setOutput(node, "Result", ScriptValue(r));
        }
    });

    registry.registerNode({
        "MathLerp", "Math Lerp", "Math",
        "Linear interpolation between A and B by Alpha (clamped 0..1)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(1.0f)},
            {PinKind::DATA, "Alpha", ScriptDataType::FLOAT, ScriptValue(0.5f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            float t = std::max(0.0f, std::min(1.0f, ctx.readInputAs<float>(node, "Alpha")));
            ctx.setOutput(node, "Result", ScriptValue(a + (b - a) * t));
        }
    });

    // -----------------------------------------------------------------------
    // Vector math
    // -----------------------------------------------------------------------
    registry.registerNode({
        "GetDistance", "Get Distance", "Vector",
        "Euclidean distance between two vec3 points",
        {
            {PinKind::DATA, "A", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "B", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::DATA, "Distance", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto a = ctx.readInputAs<glm::vec3>(node, "A");
            auto b = ctx.readInputAs<glm::vec3>(node, "B");
            ctx.setOutput(node, "Distance", ScriptValue(glm::distance(a, b)));
        }
    });

    registry.registerNode({
        "VectorNormalize", "Normalize Vector", "Vector",
        "Returns a unit-length vec3 in the same direction (zero vector returns zero)",
        {{PinKind::DATA, "V", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))}},
        {{PinKind::DATA, "Result", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto v = ctx.readInputAs<glm::vec3>(node, "V");
            float len = glm::length(v);
            glm::vec3 r = (len > 1e-9f) ? v / len : glm::vec3(0.0f);
            ctx.setOutput(node, "Result", ScriptValue(r));
        }
    });

    registry.registerNode({
        "DotProduct", "Dot Product", "Vector",
        "Dot product of two vec3",
        {
            {PinKind::DATA, "A", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "B", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::DATA, "Result", ScriptDataType::FLOAT, ScriptValue(0.0f)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto a = ctx.readInputAs<glm::vec3>(node, "A");
            auto b = ctx.readInputAs<glm::vec3>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(glm::dot(a, b)));
        }
    });

    registry.registerNode({
        "CrossProduct", "Cross Product", "Vector",
        "Cross product of two vec3",
        {
            {PinKind::DATA, "A", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "B", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::DATA, "Result", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto a = ctx.readInputAs<glm::vec3>(node, "A");
            auto b = ctx.readInputAs<glm::vec3>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(glm::cross(a, b)));
        }
    });

    // -----------------------------------------------------------------------
    // Boolean logic
    // -----------------------------------------------------------------------
    registry.registerNode({
        "BoolAnd", "AND", "Logic",
        "Logical AND of two booleans",
        {
            {PinKind::DATA, "A", ScriptDataType::BOOL, ScriptValue(false)},
            {PinKind::DATA, "B", ScriptDataType::BOOL, ScriptValue(false)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            bool a = ctx.readInputAs<bool>(node, "A");
            bool b = ctx.readInputAs<bool>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a && b));
        }
    });

    registry.registerNode({
        "BoolOr", "OR", "Logic",
        "Logical OR of two booleans",
        {
            {PinKind::DATA, "A", ScriptDataType::BOOL, ScriptValue(false)},
            {PinKind::DATA, "B", ScriptDataType::BOOL, ScriptValue(false)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            bool a = ctx.readInputAs<bool>(node, "A");
            bool b = ctx.readInputAs<bool>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a || b));
        }
    });

    registry.registerNode({
        "BoolNot", "NOT", "Logic",
        "Logical NOT of a boolean",
        {{PinKind::DATA, "A", ScriptDataType::BOOL, ScriptValue(false)}},
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(true)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            bool a = ctx.readInputAs<bool>(node, "A");
            ctx.setOutput(node, "Result", ScriptValue(!a));
        }
    });

    // -----------------------------------------------------------------------
    // Comparisons (operate on floats for maximum compatibility)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "CompareEqual", "Compare Equal", "Logic",
        "Returns true if A == B (compared as floats)",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(std::abs(a - b) < 1e-6f));
        }
    });

    registry.registerNode({
        "CompareLess", "Compare Less", "Logic",
        "Returns true if A < B",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a < b));
        }
    });

    registry.registerNode({
        "CompareGreater", "Compare Greater", "Logic",
        "Returns true if A > B",
        {
            {PinKind::DATA, "A", ScriptDataType::FLOAT, ScriptValue(0.0f)},
            {PinKind::DATA, "B", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        {{PinKind::DATA, "Result", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float a = ctx.readInputAs<float>(node, "A");
            float b = ctx.readInputAs<float>(node, "B");
            ctx.setOutput(node, "Result", ScriptValue(a > b));
        }
    });

    // -----------------------------------------------------------------------
    // String / type conversion
    // -----------------------------------------------------------------------
    registry.registerNode({
        "ToString", "To String", "Utility",
        "Converts any value to a string representation",
        {{PinKind::DATA, "Value", ScriptDataType::ANY, ScriptValue(0.0f)}},
        {{PinKind::DATA, "Result", ScriptDataType::STRING, ScriptValue(std::string(""))}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            ScriptValue v = ctx.readInput(node, "Value");
            ctx.setOutput(node, "Result", ScriptValue(v.asString()));
        }
    });

    // -----------------------------------------------------------------------
    // Has Variable (pure)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "HasVariable", "Has Variable", "Variables",
        "Returns true if a graph-scope variable exists",
        {{PinKind::DATA, "Name", ScriptDataType::STRING, ScriptValue(std::string(""))}},
        {{PinKind::DATA, "Exists", ScriptDataType::BOOL, ScriptValue(false)}},
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "Name");
            bool exists = ctx.instance().graphBlackboard().has(name);
            ctx.setOutput(node, "Exists", ScriptValue(exists));
        }
    });

    // -----------------------------------------------------------------------
    // Raycast (pure, but touches physics)
    // -----------------------------------------------------------------------
    registry.registerNode({
        "Raycast", "Raycast", "Physics",
        "Casts a ray and reports the first hit",
        {
            {PinKind::DATA, "origin", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "direction", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f, 0.0f, -1.0f))},
            {PinKind::DATA, "maxDistance", ScriptDataType::FLOAT, ScriptValue(100.0f)},
        },
        {
            {PinKind::DATA, "hit", ScriptDataType::BOOL, ScriptValue(false)},
            {PinKind::DATA, "fraction", ScriptDataType::FLOAT, ScriptValue(0.0f)},
        },
        "", true, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto origin = ctx.readInputAs<glm::vec3>(node, "origin");
            auto dir = ctx.readInputAs<glm::vec3>(node, "direction");
            float maxDist = ctx.readInputAs<float>(node, "maxDistance");

            // Scale direction to max distance (Jolt uses unscaled direction length).
            float dl = glm::length(dir);
            if (dl > 1e-6f)
            {
                dir = (dir / dl) * maxDist;
            }

            bool hit = false;
            float fraction = 0.0f;
            if (reinterpret_cast<uintptr_t>(&ctx.engine()) != 0)
            {
                JPH::BodyID bodyId;
                hit = ctx.engine().getPhysicsWorld().rayCast(
                    origin, dir, bodyId, fraction);
            }
            ctx.setOutput(node, "hit", ScriptValue(hit));
            ctx.setOutput(node, "fraction", ScriptValue(fraction));
        }
    });
}

} // namespace Vestige
