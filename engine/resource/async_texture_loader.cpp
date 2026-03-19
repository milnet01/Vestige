/// @file async_texture_loader.cpp
/// @brief AsyncTextureLoader implementation.
#include "resource/async_texture_loader.h"
#include "core/logger.h"

#include <chrono>
#include <stb_image.h>

namespace Vestige
{

AsyncTextureLoader::AsyncTextureLoader()
    : m_running(true)
    , m_inFlightCount(0)
{
    m_workerThread = std::thread(&AsyncTextureLoader::workerLoop, this);
    Logger::debug("Async texture loader started");
}

AsyncTextureLoader::~AsyncTextureLoader()
{
    shutdown();
}

void AsyncTextureLoader::requestLoad(const std::string& filePath,
                                       std::shared_ptr<Texture> target, bool linear)
{
    DecodeJob job;
    job.filePath = filePath;
    job.target = target;
    job.linear = linear;

    m_inFlightCount.fetch_add(1, std::memory_order_relaxed);

    {
        std::lock_guard<std::mutex> lock(m_pendingMutex);
        m_pendingJobs.push(std::move(job));
    }
    m_pendingCV.notify_one();
}

void AsyncTextureLoader::processUploads(int maxUploadsPerFrame)
{
    int uploaded = 0;
    while (uploaded < maxUploadsPerFrame)
    {
        DecodeJob job;
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            if (m_completedJobs.empty())
            {
                break;
            }
            job = std::move(m_completedJobs.front());
            m_completedJobs.pop();
        }

        if (job.pixelData && job.target)
        {
            // Upload to GPU on the main thread (raw data already flipped by stb_image)
            bool success = job.target->loadFromMemory(
                job.pixelData, job.width, job.height, job.channels, job.linear);
            if (!success)
            {
                Logger::warning("Async upload failed for: " + job.filePath);
            }
        }

        // Free the CPU-side pixel data
        if (job.pixelData)
        {
            stbi_image_free(job.pixelData);
        }

        m_inFlightCount.fetch_sub(1, std::memory_order_relaxed);
        uploaded++;
    }
}

size_t AsyncTextureLoader::getPendingCount() const
{
    return m_inFlightCount.load(std::memory_order_relaxed);
}

void AsyncTextureLoader::waitForAll()
{
    // Block until all jobs finish decoding (does not upload — caller does that).
    // Uses a short sleep instead of busy-spin to avoid wasting CPU.
    using namespace std::chrono_literals;
    while (m_inFlightCount.load(std::memory_order_relaxed) > 0)
    {
        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            std::lock_guard<std::mutex> lock2(m_pendingMutex);
            if (m_pendingJobs.empty() && m_completedJobs.size() == m_inFlightCount.load())
            {
                break;  // All decoded, waiting for upload
            }
        }
        std::this_thread::sleep_for(1ms);
    }
}

void AsyncTextureLoader::shutdown()
{
    if (!m_running.exchange(false))
    {
        return;  // Already shut down
    }

    m_pendingCV.notify_all();

    if (m_workerThread.joinable())
    {
        m_workerThread.join();
    }

    // Clean up any remaining completed jobs
    std::lock_guard<std::mutex> lock(m_completedMutex);
    while (!m_completedJobs.empty())
    {
        auto& job = m_completedJobs.front();
        if (job.pixelData)
        {
            stbi_image_free(job.pixelData);
        }
        m_completedJobs.pop();
    }

    Logger::debug("Async texture loader shut down");
}

void AsyncTextureLoader::workerLoop()
{
    while (m_running.load(std::memory_order_relaxed))
    {
        DecodeJob job;
        {
            std::unique_lock<std::mutex> lock(m_pendingMutex);
            m_pendingCV.wait(lock, [this]()
            {
                return !m_pendingJobs.empty() || !m_running.load(std::memory_order_relaxed);
            });

            if (!m_running.load(std::memory_order_relaxed) && m_pendingJobs.empty())
            {
                break;
            }

            if (m_pendingJobs.empty())
            {
                continue;
            }

            job = std::move(m_pendingJobs.front());
            m_pendingJobs.pop();
        }

        // Decode on the worker thread (CPU-only, no GL calls)
        // Use the thread-safe variant so this doesn't race with main-thread loads.
        // The loadFromMemory(raw) method in Texture flips the data, so we load without flip here.
        stbi_set_flip_vertically_on_load_thread(false);

        int width = 0;
        int height = 0;
        int channels = 0;
        unsigned char* data = stbi_load(job.filePath.c_str(), &width, &height, &channels, 0);

        if (!data)
        {
            Logger::warning("Async decode failed: " + job.filePath
                + " — " + stbi_failure_reason());
            m_inFlightCount.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }

        if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        {
            Logger::warning("Async decode: invalid dimensions for " + job.filePath);
            stbi_image_free(data);
            m_inFlightCount.fetch_sub(1, std::memory_order_relaxed);
            continue;
        }

        job.pixelData = data;
        job.width = width;
        job.height = height;
        job.channels = channels;

        {
            std::lock_guard<std::mutex> lock(m_completedMutex);
            m_completedJobs.push(std::move(job));
        }
    }

    // Thread-local flip state — no global restore needed
    stbi_set_flip_vertically_on_load_thread(true);
}

} // namespace Vestige
