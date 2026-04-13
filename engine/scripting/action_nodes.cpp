/// @file action_nodes.cpp
/// @brief Side-effectful action nodes — modify entities, physics, audio, etc.
///
/// Action nodes have an "Exec" input and a "Then" output. Their execute
/// function applies the side effect and then triggers "Then" for chaining.
///
/// Entity inputs use the convention: entity ID of 0 means "owner entity"
/// (the entity the ScriptComponent is attached to). Non-zero IDs are looked
/// up in the active scene — if not found, the action is a warning + skip,
/// but execution still continues to the "Then" pin to avoid breaking chains.
#include "scripting/action_nodes.h"
#include "scripting/node_type_registry.h"
#include "scripting/script_context.h"
#include "scripting/script_events.h"
#include "core/engine.h"
#include "scene/entity.h"
#include "scene/light_component.h"
#include "systems/audio_system.h"
#include "physics/rigid_body.h"

#include <glm/gtc/quaternion.hpp>

namespace Vestige
{

namespace
{

/// @brief Shared helper: read input pin as entity ID, resolve through context.
Entity* readEntityInput(ScriptContext& ctx, const ScriptNodeInstance& node,
                        const std::string& pinName)
{
    uint32_t id = ctx.readInputAs<uint32_t>(node, pinName);
    return ctx.resolveEntity(id);
}

/// @brief Register a vec3-valued transform setter node. Covers the
/// SetPosition/SetRotation/SetScale pattern where the only thing that
/// varies is the display name, data-pin name, default value, and which
/// Transform member the vec3 is assigned to (audit L3).
void registerTransformVec3Setter(
    NodeTypeRegistry& registry,
    const char* typeName,
    const char* displayName,
    const char* tooltip,
    const char* pinName,
    glm::vec3 defaultValue,
    glm::vec3 Transform::*member)
{
    registry.registerNode({
        typeName, displayName, "Transform", tooltip,
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, pinName, ScriptDataType::VEC3, ScriptValue(defaultValue)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "", false, false,
        [pinName, member](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto value = ctx.readInputAs<glm::vec3>(node, pinName);
            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                e->transform.*member = value;
            }
            ctx.triggerOutput(node, "Then");
        }
    });
}

} // namespace

void registerActionNodeTypes(NodeTypeRegistry& registry)
{
    // -----------------------------------------------------------------------
    // PlaySound — fire-and-forget sound at a world position.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "PlaySound",
        "Play Sound",
        "Audio",
        "Plays a sound at a world position (fire-and-forget)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "clipPath", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "position", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
            {PinKind::DATA, "volume", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto clip = ctx.readInputAs<std::string>(node, "clipPath");
            auto pos = ctx.readInputAs<glm::vec3>(node, "position");
            auto vol = ctx.readInputAs<float>(node, "volume");

            if (!clip.empty() && reinterpret_cast<uintptr_t>(&ctx.engine()) != 0)
            {
                auto* audioSys = ctx.engine().getSystemRegistry()
                                    .getSystem<AudioSystem>();
                if (audioSys && audioSys->isAvailable())
                {
                    audioSys->getAudioEngine().playSound(clip, pos, vol);
                }
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SpawnEntity — creates a new empty entity at a world position.
    // A fuller version (loading from a prefab asset) is reserved for the
    // prefab-integration phase.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SpawnEntity",
        "Spawn Entity",
        "Scene",
        "Creates a new empty entity in the scene at a world position",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "name", ScriptDataType::STRING, ScriptValue(std::string("Spawned"))},
            {PinKind::DATA, "position", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {
            {PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
        },
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "name");
            auto pos = ctx.readInputAs<glm::vec3>(node, "position");

            ScriptValue spawnedId = ScriptValue::entityId(0);
            if (auto* scene = ctx.activeScene())
            {
                Entity* e = scene->createEntity(name);
                if (e)
                {
                    e->transform.position = pos;
                    spawnedId = ScriptValue::entityId(e->getId());
                }
            }
            ctx.setOutput(node, "entity", spawnedId);
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // DestroyEntity — removes an entity (and its descendants) from the scene.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "DestroyEntity",
        "Destroy Entity",
        "Scene",
        "Removes an entity (and all descendants) from the scene",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            uint32_t id = ctx.readInputAs<uint32_t>(node, "entity");
            if (id == 0)
            {
                id = ctx.instance().entityId();
            }
            if (auto* scene = ctx.activeScene())
            {
                scene->removeEntity(id);
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SetPosition / SetRotation / SetScale — entity transform edits.
    // All three follow an identical pattern; deduplicated through
    // registerTransformVec3Setter (audit L3).
    // -----------------------------------------------------------------------
    registerTransformVec3Setter(registry, "SetPosition", "Set Position",
        "Sets an entity's local position",
        "position", glm::vec3(0.0f), &Transform::position);
    registerTransformVec3Setter(registry, "SetRotation", "Set Rotation",
        "Sets an entity's local rotation (Euler degrees)",
        "rotation", glm::vec3(0.0f), &Transform::rotation);
    registerTransformVec3Setter(registry, "SetScale", "Set Scale",
        "Sets an entity's local scale",
        "scale", glm::vec3(1.0f), &Transform::scale);

    // -----------------------------------------------------------------------
    // ApplyForce / ApplyImpulse — physics body actions.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "ApplyForce",
        "Apply Force",
        "Physics",
        "Applies a continuous force (Newtons) to an entity's rigid body",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "force", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto force = ctx.readInputAs<glm::vec3>(node, "force");
            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                if (auto* rb = e->getComponent<RigidBody>())
                {
                    rb->addForce(force);
                }
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    registry.registerNode({
        "ApplyImpulse",
        "Apply Impulse",
        "Physics",
        "Applies an instant impulse (velocity change) to an entity's rigid body",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "impulse", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto impulse = ctx.readInputAs<glm::vec3>(node, "impulse");
            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                if (auto* rb = e->getComponent<RigidBody>())
                {
                    rb->addImpulse(impulse);
                }
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // PlayAnimation — (stub) triggers a named animation clip on an entity.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "PlayAnimation",
        "Play Animation",
        "Animation",
        "(Stub) Plays a named animation clip — animator hookup pending",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "clipName", ScriptDataType::STRING, ScriptValue(std::string(""))},
            {PinKind::DATA, "blendTime", ScriptDataType::FLOAT, ScriptValue(0.2f)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            // Animator integration reserved for Phase 9E-4. For now this is
            // a pass-through that preserves the execution chain.
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SpawnParticles — (stub) emits a preset of particles at a position.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SpawnParticles",
        "Spawn Particles",
        "VFX",
        "(Stub) Spawns a particle burst at a position — preset hookup pending",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "preset", ScriptDataType::STRING, ScriptValue(std::string("spark"))},
            {PinKind::DATA, "position", ScriptDataType::VEC3, ScriptValue(glm::vec3(0.0f))},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            // Particle preset/library hookup reserved for Phase 9E-4.
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SetMaterial — (stub) swaps a MeshRenderer's material by path.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SetMaterial",
        "Set Material",
        "Rendering",
        "(Stub) Replaces an entity's material — material library hookup pending",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "materialPath", ScriptDataType::STRING, ScriptValue(std::string(""))},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            // Material loading requires MaterialLibrary integration, which
            // is reserved for Phase 9E-4.
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SetVisibility — show/hide an entity.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SetVisibility",
        "Set Visibility",
        "Rendering",
        "Shows or hides an entity (entity still updates, just not rendered)",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "visible", ScriptDataType::BOOL, ScriptValue(true)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            bool visible = ctx.readInputAs<bool>(node, "visible");
            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                e->setVisible(visible);
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // SetLightColor / SetLightIntensity — edit attached light components.
    // Checks point, spot, and directional light components in that order.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "SetLightColor",
        "Set Light Color",
        "Lighting",
        "Sets the color of an entity's point/spot/directional light component",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "color", ScriptDataType::COLOR, ScriptValue(glm::vec4(1.0f))},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto col4 = ctx.readInputAs<glm::vec4>(node, "color");
            glm::vec3 col(col4.r, col4.g, col4.b);

            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                // The Light structs encode color as diffuse/specular magnitudes.
                // Here "color" sets both; existing brightness (diffuse length)
                // is preserved by scaling the new color.
                if (auto* pl = e->getComponent<PointLightComponent>())
                {
                    float len = glm::length(pl->light.diffuse);
                    if (len < 1e-6f) len = 1.0f;
                    pl->light.diffuse = col * len;
                    pl->light.specular = col * len;
                }
                else if (auto* sl = e->getComponent<SpotLightComponent>())
                {
                    float len = glm::length(sl->light.diffuse);
                    if (len < 1e-6f) len = 1.0f;
                    sl->light.diffuse = col * len;
                    sl->light.specular = col * len;
                }
                else if (auto* dl = e->getComponent<DirectionalLightComponent>())
                {
                    float len = glm::length(dl->light.diffuse);
                    if (len < 1e-6f) len = 1.0f;
                    dl->light.diffuse = col * len;
                    dl->light.specular = col * len;
                }
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    registry.registerNode({
        "SetLightIntensity",
        "Set Light Intensity",
        "Lighting",
        "Sets the intensity of an entity's point/spot/directional light component",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "entity", ScriptDataType::ENTITY, ScriptValue::entityId(0)},
            {PinKind::DATA, "intensity", ScriptDataType::FLOAT, ScriptValue(1.0f)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            float intensity = ctx.readInputAs<float>(node, "intensity");
            auto scaleLight = [intensity](glm::vec3& diffuse, glm::vec3& specular)
            {
                // Preserve hue: normalize current color direction, scale to intensity.
                float len = glm::length(diffuse);
                glm::vec3 dir = (len > 1e-6f) ? (diffuse / len) : glm::vec3(1.0f);
                diffuse = dir * intensity;
                specular = dir * intensity;
            };

            if (Entity* e = readEntityInput(ctx, node, "entity"))
            {
                if (auto* pl = e->getComponent<PointLightComponent>())
                {
                    scaleLight(pl->light.diffuse, pl->light.specular);
                }
                else if (auto* sl = e->getComponent<SpotLightComponent>())
                {
                    scaleLight(sl->light.diffuse, sl->light.specular);
                }
                else if (auto* dl = e->getComponent<DirectionalLightComponent>())
                {
                    scaleLight(dl->light.diffuse, dl->light.specular);
                }
            }
            ctx.triggerOutput(node, "Then");
        }
    });

    // -----------------------------------------------------------------------
    // PublishEvent — fire a ScriptCustomEvent via the engine EventBus so
    // OnCustomEvent nodes in the same or other scripts receive it.
    // -----------------------------------------------------------------------
    registry.registerNode({
        "PublishEvent",
        "Publish Event",
        "Events",
        "Publishes a named custom event with an optional payload value",
        {
            {PinKind::EXECUTION, "Exec", ScriptDataType::BOOL, {}},
            {PinKind::DATA, "name", ScriptDataType::STRING, ScriptValue(std::string("CustomEvent"))},
            {PinKind::DATA, "payload", ScriptDataType::ANY, ScriptValue(0.0f)},
        },
        {{PinKind::EXECUTION, "Then", ScriptDataType::BOOL, {}}},
        "",
        false, false,
        [](ScriptContext& ctx, const ScriptNodeInstance& node)
        {
            auto name = ctx.readInputAs<std::string>(node, "name");
            auto payload = ctx.readInput(node, "payload");

            if (reinterpret_cast<uintptr_t>(&ctx.engine()) != 0)
            {
                ScriptCustomEvent event(name, payload);
                ctx.engine().getEventBus().publish(event);
            }
            ctx.triggerOutput(node, "Then");
        }
    });
}

} // namespace Vestige
