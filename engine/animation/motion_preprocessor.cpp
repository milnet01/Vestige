/// @file motion_preprocessor.cpp
/// @brief Offline motion database building pipeline implementation.

#include "animation/motion_preprocessor.h"
#include "core/logger.h"

namespace Vestige
{

void MotionPreprocessor::addClip(std::shared_ptr<AnimationClip> clip,
                                 uint32_t defaultTags)
{
    AnimClipEntry entry;
    entry.clip = std::move(clip);
    entry.defaultTags = defaultTags;
    m_clips.push_back(std::move(entry));
}

std::shared_ptr<MotionDatabase> MotionPreprocessor::build(
    const FeatureSchema& schema,
    const Skeleton& skeleton,
    const MotionPreprocessConfig& config)
{
    if (m_clips.empty())
    {
        Logger::warning("MotionPreprocessor::build — no clips added");
        return nullptr;
    }

    auto database = std::make_shared<MotionDatabase>();
    database->build(schema, m_clips, skeleton, config.sampleRate);

    if (config.enableMirroring && !config.mirrorBonePairs.empty())
    {
        database->addMirroredFrames(config.mirrorBonePairs);
    }

    Logger::info("MotionPreprocessor built database: "
                 + std::to_string(database->getFrameCount()) + " frames from "
                 + std::to_string(m_clips.size()) + " clips");

    return database;
}

int MotionPreprocessor::getClipCount() const
{
    return static_cast<int>(m_clips.size());
}

void MotionPreprocessor::clear()
{
    m_clips.clear();
}

} // namespace Vestige
