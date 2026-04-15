// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file facial_presets.h
/// @brief Emotion presets and ARKit 52 blend shape name constants.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Vestige
{

/// @brief Emotion types for facial animation presets.
enum class Emotion : uint8_t
{
    NEUTRAL,
    HAPPY,
    SAD,
    ANGRY,
    SURPRISED,
    FEAR,
    DISGUST,
    PAIN,
    COUNT
};

/// @brief Returns the display name for an Emotion.
const char* emotionName(Emotion emotion);

/// @brief A single blend shape entry in an emotion preset.
struct EmotionEntry
{
    const char* shapeName;  ///< ARKit blend shape name.
    float weight;           ///< Weight value [0, 1].
};

/// @brief Defines a facial expression as a sparse set of blend shape weights.
struct EmotionPreset
{
    Emotion emotion;                    ///< Which emotion this represents.
    const char* name;                   ///< Display name.
    std::vector<EmotionEntry> entries;  ///< Non-zero blend shape weights.
};

/// @brief Static access to built-in emotion presets.
class FacialPresets
{
public:
    /// @brief Gets a preset by emotion type.
    static const EmotionPreset& get(Emotion emotion);

    /// @brief Gets all presets (indexed by Emotion enum value).
    static const std::vector<EmotionPreset>& getAll();
};

/// @brief ARKit 52 blend shape name constants.
///
/// These match the Apple ARKit BlendShapeLocation standard,
/// which is the de facto industry standard for facial blend shapes
/// (used by MetaHuman, Ready Player Me, VRChat, etc.).
namespace BlendShape
{
    // --- Eyes (14) ---
    constexpr const char* EYE_BLINK_LEFT      = "eyeBlinkLeft";
    constexpr const char* EYE_LOOK_DOWN_LEFT   = "eyeLookDownLeft";
    constexpr const char* EYE_LOOK_IN_LEFT     = "eyeLookInLeft";
    constexpr const char* EYE_LOOK_OUT_LEFT    = "eyeLookOutLeft";
    constexpr const char* EYE_LOOK_UP_LEFT     = "eyeLookUpLeft";
    constexpr const char* EYE_SQUINT_LEFT      = "eyeSquintLeft";
    constexpr const char* EYE_WIDE_LEFT        = "eyeWideLeft";
    constexpr const char* EYE_BLINK_RIGHT      = "eyeBlinkRight";
    constexpr const char* EYE_LOOK_DOWN_RIGHT  = "eyeLookDownRight";
    constexpr const char* EYE_LOOK_IN_RIGHT    = "eyeLookInRight";
    constexpr const char* EYE_LOOK_OUT_RIGHT   = "eyeLookOutRight";
    constexpr const char* EYE_LOOK_UP_RIGHT    = "eyeLookUpRight";
    constexpr const char* EYE_SQUINT_RIGHT     = "eyeSquintRight";
    constexpr const char* EYE_WIDE_RIGHT       = "eyeWideRight";

    // --- Jaw (4) ---
    constexpr const char* JAW_FORWARD = "jawForward";
    constexpr const char* JAW_LEFT    = "jawLeft";
    constexpr const char* JAW_RIGHT   = "jawRight";
    constexpr const char* JAW_OPEN    = "jawOpen";

    // --- Mouth (23) ---
    constexpr const char* MOUTH_CLOSE           = "mouthClose";
    constexpr const char* MOUTH_FUNNEL          = "mouthFunnel";
    constexpr const char* MOUTH_PUCKER          = "mouthPucker";
    constexpr const char* MOUTH_LEFT            = "mouthLeft";
    constexpr const char* MOUTH_RIGHT           = "mouthRight";
    constexpr const char* MOUTH_SMILE_LEFT      = "mouthSmileLeft";
    constexpr const char* MOUTH_SMILE_RIGHT     = "mouthSmileRight";
    constexpr const char* MOUTH_FROWN_LEFT      = "mouthFrownLeft";
    constexpr const char* MOUTH_FROWN_RIGHT     = "mouthFrownRight";
    constexpr const char* MOUTH_DIMPLE_LEFT     = "mouthDimpleLeft";
    constexpr const char* MOUTH_DIMPLE_RIGHT    = "mouthDimpleRight";
    constexpr const char* MOUTH_STRETCH_LEFT    = "mouthStretchLeft";
    constexpr const char* MOUTH_STRETCH_RIGHT   = "mouthStretchRight";
    constexpr const char* MOUTH_ROLL_LOWER      = "mouthRollLower";
    constexpr const char* MOUTH_ROLL_UPPER      = "mouthRollUpper";
    constexpr const char* MOUTH_SHRUG_LOWER     = "mouthShrugLower";
    constexpr const char* MOUTH_SHRUG_UPPER     = "mouthShrugUpper";
    constexpr const char* MOUTH_PRESS_LEFT      = "mouthPressLeft";
    constexpr const char* MOUTH_PRESS_RIGHT     = "mouthPressRight";
    constexpr const char* MOUTH_LOWER_DOWN_LEFT  = "mouthLowerDownLeft";
    constexpr const char* MOUTH_LOWER_DOWN_RIGHT = "mouthLowerDownRight";
    constexpr const char* MOUTH_UPPER_UP_LEFT    = "mouthUpperUpLeft";
    constexpr const char* MOUTH_UPPER_UP_RIGHT   = "mouthUpperUpRight";

    // --- Brows (5) ---
    constexpr const char* BROW_DOWN_LEFT     = "browDownLeft";
    constexpr const char* BROW_DOWN_RIGHT    = "browDownRight";
    constexpr const char* BROW_INNER_UP      = "browInnerUp";
    constexpr const char* BROW_OUTER_UP_LEFT  = "browOuterUpLeft";
    constexpr const char* BROW_OUTER_UP_RIGHT = "browOuterUpRight";

    // --- Cheeks, Nose, Tongue (6) ---
    constexpr const char* CHEEK_PUFF         = "cheekPuff";
    constexpr const char* CHEEK_SQUINT_LEFT  = "cheekSquintLeft";
    constexpr const char* CHEEK_SQUINT_RIGHT = "cheekSquintRight";
    constexpr const char* NOSE_SNEER_LEFT    = "noseSneerLeft";
    constexpr const char* NOSE_SNEER_RIGHT   = "noseSneerRight";
    constexpr const char* TONGUE_OUT         = "tongueOut";

} // namespace BlendShape

} // namespace Vestige
