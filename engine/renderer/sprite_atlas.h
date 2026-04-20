// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file sprite_atlas.h
/// @brief SpriteAtlas — TexturePacker JSON-Array atlas loader (Phase 9F-1).
///
/// Loads a packed-texture atlas produced by TexturePacker (or any tool that
/// emits the same JSON-Array schema: free-texture-packer, Aseprite's
/// `--sheet` export, rTexPacker). The atlas exposes named frames, each with
/// its UV rectangle inside the source texture and the original pixel size
/// before packing.
///
/// The atlas is a data-only container — no GL calls. The GL texture is
/// owned separately (typically by ResourceManager or a Texture object); the
/// atlas stores only its id so the SpriteRenderer can bind it at draw time.
#pragma once

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vestige
{

/// @brief A single frame entry inside a SpriteAtlas.
///
/// The UV rect is pre-normalised (0..1) for the atlas texture. Source size
/// is the frame's original pixel extent — useful for per-frame pivots and
/// for sprite components that want to preserve the packed art's aspect
/// ratio regardless of how the packer trimmed transparent borders.
struct SpriteAtlasFrame
{
    std::string name;
    glm::vec4   uv;           ///< (u0, v0, u1, v1), v0 top, v1 bottom.
    glm::vec2   sourceSize;   ///< Pixels before packing/trimming.
    glm::vec2   pivot;        ///< Default pivot in 0..1 of source size.
};

/// @brief Loaded sprite atlas.
class SpriteAtlas
{
public:
    /// @brief Loads an atlas from a TexturePacker JSON-Array file.
    ///
    /// The texture associated with the atlas is not loaded here — callers
    /// pass the already-loaded GL texture id via @ref setTextureId. The
    /// atlas records the `meta.image` name from the JSON for introspection
    /// so editor tooling can correlate an atlas with its source image.
    ///
    /// @param jsonPath Path to the TexturePacker JSON file.
    /// @return A populated atlas on success, nullptr on malformed input or
    ///         missing file. The first parse error is logged via
    ///         `Logger::error` so failures surface in the console.
    static std::shared_ptr<SpriteAtlas> loadFromJson(const std::string& jsonPath);

    /// @brief Looks up a frame by name.
    /// @param name Frame name (matches the `filename` field in JSON).
    /// @return Pointer to the frame, or nullptr if not found.
    const SpriteAtlasFrame* find(const std::string& name) const;

    /// @brief Returns all frame names in declaration order.
    std::vector<std::string> frameNames() const;

    /// @brief Returns the number of frames.
    std::size_t frameCount() const { return m_frames.size(); }

    /// @brief Sets the GL texture id the atlas should bind at draw time.
    void setTextureId(GLuint id) { m_textureId = id; }

    /// @brief Returns the bound GL texture id (0 if none).
    GLuint textureId() const { return m_textureId; }

    /// @brief Returns the pixel size of the atlas texture as declared in JSON.
    glm::vec2 atlasSize() const { return m_atlasSize; }

    /// @brief Returns the image name recorded in the JSON `meta` block.
    const std::string& imageName() const { return m_imageName; }

    /// @brief Returns the source JSON path (useful for editor hot-reload).
    const std::string& sourcePath() const { return m_sourcePath; }

private:
    std::vector<SpriteAtlasFrame> m_frames;
    std::unordered_map<std::string, std::size_t> m_index;
    glm::vec2 m_atlasSize = glm::vec2(0.0f);
    std::string m_imageName;
    std::string m_sourcePath;
    GLuint m_textureId = 0;
};

} // namespace Vestige
