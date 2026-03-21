/// @file particle_property_command.h
/// @brief Undoable command for particle emitter configuration changes.
#pragma once

#include "editor/commands/editor_command.h"
#include "scene/particle_emitter.h"
#include "scene/scene.h"
#include "scene/entity.h"

#include <string>

namespace Vestige
{

/// @brief Records a before/after snapshot of a ParticleEmitterConfig.
///
/// Stores the entire config struct for both old and new state. This is
/// simple and correct — the config is small (~200 bytes) and avoids
/// the complexity of per-field tracking.
class ParticlePropertyCommand : public EditorCommand
{
public:
    /// @brief Creates a command that captures the old config and applies the new one.
    /// @param scene Scene containing the entity.
    /// @param entityId Entity with the ParticleEmitterComponent.
    /// @param oldConfig Config state before the change.
    /// @param newConfig Config state after the change.
    /// @param propertyName Human-readable name of the property changed.
    ParticlePropertyCommand(Scene& scene, uint32_t entityId,
                            const ParticleEmitterConfig& oldConfig,
                            const ParticleEmitterConfig& newConfig,
                            const std::string& propertyName)
        : m_scene(scene)
        , m_entityId(entityId)
        , m_oldConfig(oldConfig)
        , m_newConfig(newConfig)
        , m_propertyName(propertyName)
    {
    }

    void execute() override
    {
        applyConfig(m_newConfig);
    }

    void undo() override
    {
        applyConfig(m_oldConfig);
    }

    std::string getDescription() const override
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        std::string name = entity ? entity->getName() : "entity";
        return "Change " + m_propertyName + " on '" + name + "'";
    }

    bool canMergeWith(const EditorCommand& other) const override
    {
        auto* otherCmd = dynamic_cast<const ParticlePropertyCommand*>(&other);
        if (!otherCmd)
        {
            return false;
        }
        return m_entityId == otherCmd->m_entityId
            && m_propertyName == otherCmd->m_propertyName;
    }

    void mergeWith(EditorCommand& other) override
    {
        auto* otherCmd = dynamic_cast<ParticlePropertyCommand*>(&other);
        if (otherCmd)
        {
            // Keep our old config, take their new config
            m_newConfig = otherCmd->m_newConfig;
        }
    }

private:
    void applyConfig(const ParticleEmitterConfig& config)
    {
        Entity* entity = m_scene.findEntityById(m_entityId);
        if (!entity)
        {
            return;
        }

        auto* emitter = entity->getComponent<ParticleEmitterComponent>();
        if (!emitter)
        {
            return;
        }

        emitter->getConfig() = config;
    }

    Scene& m_scene;
    uint32_t m_entityId;
    ParticleEmitterConfig m_oldConfig;
    ParticleEmitterConfig m_newConfig;
    std::string m_propertyName;
};

} // namespace Vestige
