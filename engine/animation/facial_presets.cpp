/// @file facial_presets.cpp
/// @brief Built-in emotion preset data based on FACS Action Unit research.

#include "animation/facial_presets.h"

namespace Vestige
{

const char* emotionName(Emotion emotion)
{
    switch (emotion)
    {
        case Emotion::NEUTRAL:   return "Neutral";
        case Emotion::HAPPY:     return "Happy";
        case Emotion::SAD:       return "Sad";
        case Emotion::ANGRY:     return "Angry";
        case Emotion::SURPRISED: return "Surprised";
        case Emotion::FEAR:      return "Fear";
        case Emotion::DISGUST:   return "Disgust";
        case Emotion::PAIN:      return "Pain";
        case Emotion::COUNT:     return "Unknown";
    }
    return "Unknown";
}

const EmotionPreset& FacialPresets::get(Emotion emotion)
{
    const auto& all = getAll();
    auto idx = static_cast<size_t>(emotion);
    if (idx >= all.size())
    {
        return all[0]; // fallback to neutral
    }
    return all[idx];
}

const std::vector<EmotionPreset>& FacialPresets::getAll()
{
    // Meyers singleton — constructed once, thread-safe in C++11+.
    // Weight values based on FACS Action Unit research (Ekman, EMFACS).
    static const std::vector<EmotionPreset> presets =
    {
        // NEUTRAL — all weights zero (rest pose)
        {
            Emotion::NEUTRAL, "neutral", {}
        },

        // HAPPY — AU6 (cheek raise) + AU12 (lip corner pull) = Duchenne smile
        {
            Emotion::HAPPY, "happy",
            {
                {BlendShape::MOUTH_SMILE_LEFT,   0.7f},
                {BlendShape::MOUTH_SMILE_RIGHT,  0.7f},
                {BlendShape::CHEEK_SQUINT_LEFT,  0.5f},
                {BlendShape::CHEEK_SQUINT_RIGHT, 0.5f},
                {BlendShape::EYE_SQUINT_LEFT,    0.3f},
                {BlendShape::EYE_SQUINT_RIGHT,   0.3f},
                {BlendShape::JAW_OPEN,           0.1f},
                {BlendShape::MOUTH_UPPER_UP_LEFT,  0.2f},
                {BlendShape::MOUTH_UPPER_UP_RIGHT, 0.2f},
            }
        },

        // SAD — AU1 (inner brow raise) + AU4 (brow lower) + AU15 (lip corner depress)
        {
            Emotion::SAD, "sad",
            {
                {BlendShape::BROW_INNER_UP,      0.7f},
                {BlendShape::BROW_DOWN_LEFT,     0.3f},
                {BlendShape::BROW_DOWN_RIGHT,    0.3f},
                {BlendShape::MOUTH_FROWN_LEFT,   0.6f},
                {BlendShape::MOUTH_FROWN_RIGHT,  0.6f},
                {BlendShape::MOUTH_SHRUG_LOWER,  0.3f},
                {BlendShape::EYE_SQUINT_LEFT,    0.2f},
                {BlendShape::EYE_SQUINT_RIGHT,   0.2f},
                {BlendShape::JAW_OPEN,           0.05f},
            }
        },

        // ANGRY — AU4 (brow lower) + AU5 (upper lid raise) + AU23/24 (lip press)
        {
            Emotion::ANGRY, "angry",
            {
                {BlendShape::BROW_DOWN_LEFT,     0.8f},
                {BlendShape::BROW_DOWN_RIGHT,    0.8f},
                {BlendShape::EYE_WIDE_LEFT,      0.3f},
                {BlendShape::EYE_WIDE_RIGHT,     0.3f},
                {BlendShape::EYE_SQUINT_LEFT,    0.4f},
                {BlendShape::EYE_SQUINT_RIGHT,   0.4f},
                {BlendShape::NOSE_SNEER_LEFT,    0.5f},
                {BlendShape::NOSE_SNEER_RIGHT,   0.5f},
                {BlendShape::MOUTH_PRESS_LEFT,   0.5f},
                {BlendShape::MOUTH_PRESS_RIGHT,  0.5f},
                {BlendShape::JAW_FORWARD,        0.2f},
            }
        },

        // SURPRISED — AU1+AU2 (brow raise) + AU5 (lid raise) + AU26 (jaw drop)
        {
            Emotion::SURPRISED, "surprised",
            {
                {BlendShape::BROW_INNER_UP,       0.8f},
                {BlendShape::BROW_OUTER_UP_LEFT,  0.8f},
                {BlendShape::BROW_OUTER_UP_RIGHT, 0.8f},
                {BlendShape::EYE_WIDE_LEFT,       0.9f},
                {BlendShape::EYE_WIDE_RIGHT,      0.9f},
                {BlendShape::JAW_OPEN,            0.6f},
                {BlendShape::MOUTH_STRETCH_LEFT,  0.3f},
                {BlendShape::MOUTH_STRETCH_RIGHT, 0.3f},
            }
        },

        // FEAR — AU1+AU2+AU4 (brow raise+lower) + AU5 (lid raise) + AU20 (lip stretch)
        {
            Emotion::FEAR, "fear",
            {
                {BlendShape::BROW_INNER_UP,           0.9f},
                {BlendShape::BROW_OUTER_UP_LEFT,      0.5f},
                {BlendShape::BROW_OUTER_UP_RIGHT,     0.5f},
                {BlendShape::BROW_DOWN_LEFT,          0.3f},
                {BlendShape::BROW_DOWN_RIGHT,         0.3f},
                {BlendShape::EYE_WIDE_LEFT,           0.8f},
                {BlendShape::EYE_WIDE_RIGHT,          0.8f},
                {BlendShape::EYE_SQUINT_LEFT,         0.2f},
                {BlendShape::EYE_SQUINT_RIGHT,        0.2f},
                {BlendShape::MOUTH_STRETCH_LEFT,      0.6f},
                {BlendShape::MOUTH_STRETCH_RIGHT,     0.6f},
                {BlendShape::JAW_OPEN,                0.3f},
                {BlendShape::MOUTH_LOWER_DOWN_LEFT,   0.3f},
                {BlendShape::MOUTH_LOWER_DOWN_RIGHT,  0.3f},
            }
        },

        // DISGUST — AU9 (nose wrinkle) + AU15 (lip corner depress) + AU17 (chin raise)
        {
            Emotion::DISGUST, "disgust",
            {
                {BlendShape::NOSE_SNEER_LEFT,        0.8f},
                {BlendShape::NOSE_SNEER_RIGHT,       0.8f},
                {BlendShape::MOUTH_UPPER_UP_LEFT,    0.5f},
                {BlendShape::MOUTH_UPPER_UP_RIGHT,   0.5f},
                {BlendShape::BROW_DOWN_LEFT,         0.4f},
                {BlendShape::BROW_DOWN_RIGHT,        0.4f},
                {BlendShape::CHEEK_SQUINT_LEFT,      0.3f},
                {BlendShape::CHEEK_SQUINT_RIGHT,     0.3f},
                {BlendShape::MOUTH_FROWN_LEFT,       0.3f},
                {BlendShape::MOUTH_FROWN_RIGHT,      0.3f},
                {BlendShape::MOUTH_SHRUG_LOWER,      0.2f},
            }
        },

        // PAIN — AU4 (brow lower) + AU6/7 (cheek raise + lid tightener) + AU9 (nose wrinkle) + AU43 (eye closure)
        {
            Emotion::PAIN, "pain",
            {
                {BlendShape::BROW_DOWN_LEFT,         0.6f},
                {BlendShape::BROW_DOWN_RIGHT,        0.6f},
                {BlendShape::BROW_INNER_UP,          0.4f},
                {BlendShape::EYE_SQUINT_LEFT,        0.7f},
                {BlendShape::EYE_SQUINT_RIGHT,       0.7f},
                {BlendShape::EYE_BLINK_LEFT,         0.3f},
                {BlendShape::EYE_BLINK_RIGHT,        0.3f},
                {BlendShape::NOSE_SNEER_LEFT,        0.4f},
                {BlendShape::NOSE_SNEER_RIGHT,       0.4f},
                {BlendShape::MOUTH_STRETCH_LEFT,     0.5f},
                {BlendShape::MOUTH_STRETCH_RIGHT,    0.5f},
                {BlendShape::JAW_OPEN,               0.2f},
                {BlendShape::MOUTH_UPPER_UP_LEFT,    0.3f},
                {BlendShape::MOUTH_UPPER_UP_RIGHT,   0.3f},
            }
        },
    };

    return presets;
}

} // namespace Vestige
