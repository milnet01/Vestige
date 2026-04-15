// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file async_texture_loader.h
/// @brief Background thread texture decoding with main-thread GPU upload.
#pragma once

#include "renderer/texture.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace Vestige
{

/// @brief Decodes textures on a background thread, uploads to GPU on the main thread.
class AsyncTextureLoader
{
public:
    AsyncTextureLoader();
    ~AsyncTextureLoader();

    // Non-copyable
    AsyncTextureLoader(const AsyncTextureLoader&) = delete;
    AsyncTextureLoader& operator=(const AsyncTextureLoader&) = delete;

    /// @brief Requests a texture to be loaded asynchronously.
    /// @param filePath Path to the image file.
    /// @param target Texture to upload into once decoded.
    /// @param linear If true, load as linear data (normal/height maps).
    void requestLoad(const std::string& filePath, std::shared_ptr<Texture> target, bool linear = false);

    /// @brief Processes completed decode jobs by uploading to GPU.
    /// Must be called on the main thread (GL context).
    /// @param maxUploadsPerFrame Limits GPU stalls per frame.
    void processUploads(int maxUploadsPerFrame = 2);

    /// @brief Returns the number of textures still pending (queued + decoding).
    size_t getPendingCount() const;

    /// @brief Blocks until all pending textures finish decoding (does NOT upload).
    void waitForAll();

    /// @brief Shuts down the worker thread.
    void shutdown();

private:
    struct DecodeJob
    {
        std::string filePath;
        std::shared_ptr<Texture> target;
        bool linear = false;
        // Decoded result (filled by worker)
        unsigned char* pixelData = nullptr;
        int width = 0;
        int height = 0;
        int channels = 0;
    };

    void workerLoop();

    std::thread m_workerThread;
    std::mutex m_pendingMutex;
    std::condition_variable m_pendingCV;
    std::queue<DecodeJob> m_pendingJobs;

    std::mutex m_completedMutex;
    std::queue<DecodeJob> m_completedJobs;

    std::atomic<bool> m_running;
    std::atomic<size_t> m_inFlightCount;
};

} // namespace Vestige
