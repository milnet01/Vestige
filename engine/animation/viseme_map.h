/// @file viseme_map.h
/// @brief Viseme definitions and ARKit blend shape mapping for lip sync.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief Mouth shapes based on the Rhubarb Lip Sync / Preston Blair standard.
///
/// Each viseme represents a distinct mouth pose that covers one or more phonemes.
/// Letters A-H plus X (rest) follow the Rhubarb convention used in animation
/// since the 1950s (Hanna-Barbera / Preston Blair).
enum class Viseme : uint8_t
{
    X,     ///< Rest / silence — relaxed closed mouth.
    A,     ///< Closed, slight lip pressure — P, B, M (bilabial).
    B,     ///< Slightly open, clenched teeth — K, S, T and EE vowel.
    C,     ///< Open mouth — EH, AE vowels (transition shape).
    D,     ///< Wide open mouth — AA (as in "father").
    E,     ///< Slightly rounded — AO (as in "off"), ER (as in "bird").
    F,     ///< Puckered lips — UW (as in "you"), OW (as in "show"), W.
    G,     ///< Upper teeth on lower lip — F, V (labiodental).
    H,     ///< Tongue raised behind teeth — long L sounds.
    COUNT
};

/// @brief Returns the display name for a Viseme (e.g. "A (P/B/M)").
const char* visemeName(Viseme viseme);

/// @brief A single blend shape entry mapping a viseme to an ARKit weight.
struct VisemeEntry
{
    const char* shapeName;  ///< ARKit blend shape name.
    float weight;           ///< Target weight [0, 1].
};

/// @brief Defines a viseme as a sparse set of ARKit blend shape weights.
struct VisemeShape
{
    Viseme viseme;                      ///< Which viseme this represents.
    const char* name;                   ///< Display name.
    std::vector<VisemeEntry> entries;   ///< Non-zero blend shape weights.
};

/// @brief Static access to viseme-to-ARKit-blend-shape mappings.
class VisemeMap
{
public:
    /// @brief Gets the shape definition for a viseme.
    static const VisemeShape& get(Viseme viseme);

    /// @brief Converts a Rhubarb shape character ('A'-'H', 'X') to a Viseme enum.
    /// Returns Viseme::X for unrecognized characters.
    static Viseme fromRhubarbChar(char c);

    /// @brief Converts a Viseme to its Rhubarb shape character.
    static char toRhubarbChar(Viseme viseme);

    /// @brief Blends two visemes' weights into an output map.
    ///
    /// @param a      First viseme.
    /// @param b      Second viseme.
    /// @param t      Blend factor [0, 1]. At 0 = pure a, at 1 = pure b.
    /// @param out    Output map of shape name to blended weight.
    static void blendWeights(Viseme a, Viseme b, float t,
                             std::unordered_map<std::string, float>& out);

    /// @brief Returns the total number of viseme shapes.
    static int getCount();
};

} // namespace Vestige
