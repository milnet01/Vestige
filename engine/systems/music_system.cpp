// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file music_system.cpp
/// @brief MusicSystem ISystem wrapper implementation (W8 part 2/2).

#include "systems/music_system.h"

#include "audio/audio_music_player.h"

namespace Vestige
{

MusicSystem::MusicSystem(AudioMusicPlayer& player)
    : m_player(player)
{
}

void MusicSystem::update(float deltaSeconds)
{
    m_player.applyIntensity(m_intensity, m_silence);
    m_player.update(deltaSeconds);
}

} // namespace Vestige
