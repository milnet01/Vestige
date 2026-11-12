// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file machine_load.cpp
/// @brief MachineLoadSampler and the parsers behind it.
#include "profiler/machine_load.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // windows.h must not define min and max over std's
#endif
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace Vestige
{

namespace
{

/// Reads an unsigned decimal at @p pos, skipping spaces and tabs first.
bool readUnsigned(const std::string& text, std::size_t& pos, unsigned long long& value)
{
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t'))
    {
        ++pos;
    }
    if (pos >= text.size() || text[pos] < '0' || text[pos] > '9')
    {
        return false;
    }
    value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
    {
        value = value * 10 + static_cast<unsigned long long>(text[pos] - '0');
        ++pos;
    }
    return true;
}

#ifdef __linux__
/// The peaks are the highest of samples this long (FLED-0001 § 4.2: "about
/// one second"). The whole-GPU figure is averaged from this many readings
/// inside each sample, because one reading jumps.
constexpr int GPU_READINGS_PER_SAMPLE = 5;
constexpr auto GPU_READING_GAP = std::chrono::milliseconds(200);

/// `start()` keeps the highest of this many GPU busy readings.
constexpr int START_GPU_READINGS = 5;
constexpr auto START_GPU_READING_GAP = std::chrono::milliseconds(100);

std::string readTextFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

/// The first DRM card that reports a busy percent: amdgpu's sysfs files.
std::string findGpuDevicePath()
{
    std::error_code error;
    std::filesystem::directory_iterator cards("/sys/class/drm", error);
    if (error)
    {
        return {};
    }
    std::string best;
    for (const auto& entry : cards)
    {
        const std::string device = entry.path().string() + "/device";
        if (std::filesystem::exists(device + "/gpu_busy_percent", error)
            && (best.empty() || device < best))
        {
            best = device;
        }
    }
    return best;
}

/// GPU engine time of every DRM client this process holds, in nanoseconds.
/// False when no file descriptor reports any: then our own work cannot be
/// left out, and the format says to give no peak.
bool readOwnGpuEngineNs(unsigned long long& totalNs)
{
    std::error_code error;
    std::filesystem::directory_iterator files("/proc/self/fdinfo", error);
    if (error)
    {
        return false;
    }
    // Two descriptors of one client report the same figures: count it once.
    std::map<unsigned long long, unsigned long long> byClient;
    for (const auto& entry : files)
    {
        unsigned long long clientId = 0;
        unsigned long long engineNs = 0;
        if (parseDrmEngineNs(readTextFile(entry.path().string()), clientId, engineNs))
        {
            byClient[clientId] = std::max(byClient[clientId], engineNs);
        }
    }
    totalNs = 0;
    for (const auto& client : byClient)
    {
        totalNs += client.second;
    }
    return !byClient.empty();
}
#elif defined(_WIN32)
/// The peaks are the highest of samples this long (FLED-0001 § 4.2: "about
/// one second"). Windows' counters are rates between two collections, so one
/// sample is one collection a second.
constexpr auto SAMPLE_GAP = std::chrono::seconds(1);
/// `start()` and `finish()` measure over a window this long.
constexpr auto EDGE_WINDOW = std::chrono::milliseconds(500);

unsigned long long fileTimeTicks(const FILETIME& time)
{
    return (static_cast<unsigned long long>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

/// Machine-wide and own CPU time, both in 100 ns ticks. Windows counts idle
/// time inside kernel time.
bool readCpuTimes(CpuTicks& machine, unsigned long long& own)
{
    FILETIME idle, kernel, user;
    FILETIME created, exited, ownKernel, ownUser;
    if (!GetSystemTimes(&idle, &kernel, &user)
        || !GetProcessTimes(GetCurrentProcess(), &created, &exited, &ownKernel, &ownUser))
    {
        return false;
    }
    machine.total = fileTimeTicks(kernel) + fileTimeTicks(user);
    machine.busy = machine.total - fileTimeTicks(idle);
    own = fileTimeTicks(ownKernel) + fileTimeTicks(ownUser);
    return true;
}

/// The share of the machine's CPU time that was busy between two readings,
/// this process included: what stands in for the load average on Windows.
double busyCpuShare(const CpuTicks& before, const CpuTicks& after)
{
    return otherCpuShare(before, after, 0, 0);
}

/// Windows' "GPU Engine" counter: every process's use of every engine.
class GpuEngineCounter
{
public:
    GpuEngineCounter()
    {
        if (PdhOpenQueryW(nullptr, 0, &m_query) != ERROR_SUCCESS)
        {
            m_query = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(m_query, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                                  &m_counter) != ERROR_SUCCESS)
        {
            PdhCloseQuery(m_query);
            m_query = nullptr;
            return;
        }
        PdhCollectQueryData(m_query);  // a rate needs a first collection to count from
    }

    ~GpuEngineCounter()
    {
        if (m_query != nullptr)
        {
            PdhCloseQuery(m_query);
        }
    }

    GpuEngineCounter(const GpuEngineCounter&) = delete;
    GpuEngineCounter& operator=(const GpuEngineCounter&) = delete;

    /// Every row since the last collection. False when the counter cannot be
    /// read (before Windows 10 1709, or no graphics driver reporting).
    bool collect(std::vector<GpuEngineUse>& rows)
    {
        rows.clear();
        if (m_query == nullptr || PdhCollectQueryData(m_query) != ERROR_SUCCESS)
        {
            return false;
        }
        DWORD bytes = 0;
        DWORD count = 0;
        if (PdhGetFormattedCounterArrayW(m_counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr)
            != static_cast<PDH_STATUS>(PDH_MORE_DATA))
        {
            return false;
        }
        // The size is in bytes; 8-byte storage keeps the items aligned.
        std::vector<unsigned long long> storage(bytes / sizeof(unsigned long long) + 1);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(storage.data());
        if (PdhGetFormattedCounterArrayW(m_counter, PDH_FMT_DOUBLE, &bytes, &count, items)
            != ERROR_SUCCESS)
        {
            return false;
        }
        for (DWORD i = 0; i < count; ++i)
        {
            std::string name;  // instance names are ASCII
            for (const wchar_t* c = items[i].szName; *c != L'\0'; ++c)
            {
                name.push_back(static_cast<char>(*c));
            }
            GpuEngineUse row;
            if (parseGpuEngineInstance(name, row.pid, row.engine))
            {
                row.percent = items[i].FmtValue.doubleValue;
                rows.push_back(std::move(row));
            }
        }
        return true;
    }

private:
    PDH_HQUERY m_query = nullptr;
    PDH_HCOUNTER m_counter = nullptr;
};

/// CPU share and whole-GPU busy percent over EDGE_WINDOW, this process
/// included. Blocks for that long.
void readEdge(double& cpuShare, double& gpuBusy)
{
    GpuEngineCounter gpu;
    CpuTicks before, after;
    unsigned long long own = 0;
    const bool haveCpu = readCpuTimes(before, own);
    std::this_thread::sleep_for(EDGE_WINDOW);
    if (haveCpu && readCpuTimes(after, own))
    {
        cpuShare = busyCpuShare(before, after);
    }
    std::vector<GpuEngineUse> rows;
    if (gpu.collect(rows))
    {
        gpuBusy = busiestGpuEnginePercent(rows, 0);
    }
}
#endif

} // namespace

double parseLoadAverageShare(const std::string& loadavg, unsigned logicalCpus)
{
    if (logicalCpus == 0)
    {
        return -1.0;
    }
    char* end = nullptr;
    const double oneMinute = std::strtod(loadavg.c_str(), &end);
    if (end == loadavg.c_str() || !(oneMinute >= 0.0))
    {
        return -1.0;
    }
    return oneMinute / static_cast<double>(logicalCpus);
}

bool parseProcStatCpu(const std::string& stat, CpuTicks& out)
{
    // The first line: "cpu  user nice system idle iowait irq softirq steal ...".
    // guest time is already inside user, so only the first eight are summed.
    if (stat.compare(0, 4, "cpu ") != 0)
    {
        return false;
    }
    std::size_t pos = 4;
    unsigned long long fields[8] = {};
    for (unsigned long long& field : fields)
    {
        if (!readUnsigned(stat, pos, field))
        {
            return false;
        }
    }
    out.total = 0;
    for (unsigned long long field : fields)
    {
        out.total += field;
    }
    out.busy = out.total - fields[3] - fields[4];
    return true;
}

bool parseSelfStatTicks(const std::string& selfStat, unsigned long long& ticks)
{
    const std::size_t nameEnd = selfStat.rfind(')');
    if (nameEnd == std::string::npos)
    {
        return false;
    }
    // After ") ": state is field 3, utime field 14, stime field 15.
    std::istringstream rest(selfStat.substr(nameEnd + 1));
    std::string field;
    unsigned long long utime = 0;
    unsigned long long stime = 0;
    for (int number = 3; number <= 15; ++number)
    {
        if (!(rest >> field))
        {
            return false;
        }
        if (number == 14 || number == 15)
        {
            std::size_t pos = 0;
            unsigned long long value = 0;
            if (!readUnsigned(field, pos, value) || pos != field.size())
            {
                return false;
            }
            (number == 14 ? utime : stime) = value;
        }
    }
    ticks = utime + stime;
    return true;
}

double parseGpuClockMhz(const std::string& ppDpmSclk)
{
    // "0: 500Mhz \n1: 2665Mhz *\n": the starred level is the current one.
    std::istringstream lines(ppDpmSclk);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.find('*') == std::string::npos)
        {
            continue;
        }
        std::size_t pos = line.find(':');
        unsigned long long mhz = 0;
        if (pos == std::string::npos || !readUnsigned(line, ++pos, mhz))
        {
            return -1.0;
        }
        return static_cast<double>(mhz);
    }
    return -1.0;
}

double parseGpuBusyPercent(const std::string& text)
{
    std::size_t pos = 0;
    unsigned long long percent = 0;
    if (!readUnsigned(text, pos, percent) || percent > 100)
    {
        return -1.0;
    }
    return static_cast<double>(percent);
}

bool parseDrmEngineNs(const std::string& fdinfo, unsigned long long& clientId,
                      unsigned long long& engineNs)
{
    static const std::string CLIENT_KEY = "drm-client-id:";
    static const std::string ENGINE_KEY = "drm-engine-";
    bool haveClient = false;
    bool haveEngine = false;
    unsigned long long total = 0;
    std::istringstream lines(fdinfo);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.compare(0, CLIENT_KEY.size(), CLIENT_KEY) == 0)
        {
            std::size_t pos = CLIENT_KEY.size();
            haveClient = readUnsigned(line, pos, clientId);
        }
        else if (line.compare(0, ENGINE_KEY.size(), ENGINE_KEY) == 0)
        {
            // "drm-engine-gfx:\t123 ns". "drm-engine-capacity-gfx: 1" has no
            // unit and is not a time.
            std::size_t pos = line.find(':');
            unsigned long long ns = 0;
            if (pos != std::string::npos && readUnsigned(line, ++pos, ns)
                && line.compare(pos, 3, " ns") == 0)
            {
                total += ns;
                haveEngine = true;
            }
        }
    }
    if (!haveClient || !haveEngine)
    {
        return false;
    }
    engineNs = total;
    return true;
}

double otherCpuShare(const CpuTicks& before, const CpuTicks& after,
                     unsigned long long ownTicksBefore, unsigned long long ownTicksAfter)
{
    if (after.total <= before.total)
    {
        return -1.0;
    }
    const double total = static_cast<double>(after.total - before.total);
    const double busy = static_cast<double>(after.busy) - static_cast<double>(before.busy);
    const double own = static_cast<double>(ownTicksAfter) - static_cast<double>(ownTicksBefore);
    // Our own ticks and the machine's are read a moment apart, so the
    // difference can stray just outside the range.
    return std::clamp((busy - own) / total, 0.0, 1.0);
}

double otherGpuBusyPercent(double wholeBusyPercent, unsigned long long ownEngineNs,
                           unsigned long long windowNs)
{
    if (windowNs == 0 || wholeBusyPercent < 0.0)
    {
        return -1.0;
    }
    const double ownPercent =
        100.0 * static_cast<double>(ownEngineNs) / static_cast<double>(windowNs);
    return std::clamp(wholeBusyPercent - ownPercent, 0.0, 100.0);
}

bool parseGpuEngineInstance(const std::string& instance, unsigned long& pid,
                            std::string& engine)
{
    static const std::string PID_KEY = "pid_";
    if (instance.compare(0, PID_KEY.size(), PID_KEY) != 0)
    {
        return false;
    }
    std::size_t pos = PID_KEY.size();
    unsigned long long value = 0;
    // The engine's name follows the id after one underscore, and is not empty.
    if (!readUnsigned(instance, pos, value) || pos + 1 >= instance.size()
        || instance[pos] != '_')
    {
        return false;
    }
    pid = static_cast<unsigned long>(value);
    engine = instance.substr(pos + 1);
    return true;
}

double busiestGpuEnginePercent(const std::vector<GpuEngineUse>& rows, unsigned long skipPid)
{
    if (rows.empty())
    {
        return -1.0;
    }
    std::map<std::string, double> perEngine;
    for (const GpuEngineUse& row : rows)
    {
        if (skipPid == 0 || row.pid != skipPid)
        {
            perEngine[row.engine] += row.percent;
        }
    }
    double busiest = 0.0;
    for (const auto& [engine, percent] : perEngine)
    {
        busiest = std::max(busiest, percent);
    }
    return std::clamp(busiest, 0.0, 100.0);
}

MachineLoadSampler::~MachineLoadSampler()
{
    finish();
}

void MachineLoadSampler::start()
{
#ifdef __linux__
    m_load.loadStart = parseLoadAverageShare(readTextFile("/proc/loadavg"),
                                             std::thread::hardware_concurrency());
    m_gpuDevicePath = findGpuDevicePath();
    if (m_gpuDevicePath.empty())
    {
        return;
    }
    m_load.gpuClockStartMhz = parseGpuClockMhz(readTextFile(m_gpuDevicePath + "/pp_dpm_sclk"));
    for (int reading = 0; reading < START_GPU_READINGS; ++reading)
    {
        if (reading > 0)
        {
            std::this_thread::sleep_for(START_GPU_READING_GAP);
        }
        m_load.gpuBusyStart =
            std::max(m_load.gpuBusyStart,
                     parseGpuBusyPercent(readTextFile(m_gpuDevicePath + "/gpu_busy_percent")));
    }
#elif defined(_WIN32)
    readEdge(m_load.loadStart, m_load.gpuBusyStart);
#endif
}

void MachineLoadSampler::beginRun()
{
#if defined(__linux__) || defined(_WIN32)
    if (m_thread.joinable() || m_finished)
    {
        return;
    }
    m_stop = false;
    m_thread = std::thread([this]() { samplePeaks(); });
#endif
}

MachineLoad MachineLoadSampler::finish()
{
    if (m_finished)
    {
        return m_load;
    }
    m_finished = true;
    if (m_thread.joinable())
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_wake.notify_all();
        m_thread.join();
    }
#ifdef __linux__
    m_load.loadEnd = parseLoadAverageShare(readTextFile("/proc/loadavg"),
                                           std::thread::hardware_concurrency());
    if (!m_gpuDevicePath.empty())
    {
        m_load.gpuBusyEnd =
            parseGpuBusyPercent(readTextFile(m_gpuDevicePath + "/gpu_busy_percent"));
        m_load.gpuClockEndMhz =
            parseGpuClockMhz(readTextFile(m_gpuDevicePath + "/pp_dpm_sclk"));
    }
#elif defined(_WIN32)
    readEdge(m_load.loadEnd, m_load.gpuBusyEnd);
#endif
    return m_load;
}

void MachineLoadSampler::samplePeaks()
{
#ifdef __linux__
    using Clock = std::chrono::steady_clock;

    CpuTicks cpuBefore;
    unsigned long long ownTicksBefore = 0;
    bool haveCpu = parseProcStatCpu(readTextFile("/proc/stat"), cpuBefore)
                   && parseSelfStatTicks(readTextFile("/proc/self/stat"), ownTicksBefore);
    unsigned long long gpuNsBefore = 0;
    bool haveGpu = !m_gpuDevicePath.empty() && readOwnGpuEngineNs(gpuNsBefore);
    Clock::time_point windowStart = Clock::now();

    double busySum = 0.0;
    int busyReadings = 0;
    int readingsThisSample = 0;

    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_wake.wait_for(lock, GPU_READING_GAP, [this]() { return m_stop; }))
    {
        lock.unlock();

        if (haveGpu)
        {
            const double busy =
                parseGpuBusyPercent(readTextFile(m_gpuDevicePath + "/gpu_busy_percent"));
            if (busy >= 0.0)
            {
                busySum += busy;
                ++busyReadings;
            }
        }

        if (++readingsThisSample >= GPU_READINGS_PER_SAMPLE)
        {
            CpuTicks cpuNow;
            unsigned long long ownTicksNow = 0;
            if (haveCpu && parseProcStatCpu(readTextFile("/proc/stat"), cpuNow)
                && parseSelfStatTicks(readTextFile("/proc/self/stat"), ownTicksNow))
            {
                m_load.loadPeak =
                    std::max(m_load.loadPeak,
                             otherCpuShare(cpuBefore, cpuNow, ownTicksBefore, ownTicksNow));
                cpuBefore = cpuNow;
                ownTicksBefore = ownTicksNow;
            }

            const Clock::time_point now = Clock::now();
            unsigned long long gpuNsNow = 0;
            if (haveGpu && busyReadings > 0 && readOwnGpuEngineNs(gpuNsNow))
            {
                const auto windowNs = static_cast<unsigned long long>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - windowStart)
                        .count());
                const unsigned long long ownNs =
                    gpuNsNow > gpuNsBefore ? gpuNsNow - gpuNsBefore : 0;
                m_load.gpuBusyPeak =
                    std::max(m_load.gpuBusyPeak,
                             otherGpuBusyPercent(busySum / busyReadings, ownNs, windowNs));
                gpuNsBefore = gpuNsNow;
            }
            windowStart = now;
            busySum = 0.0;
            busyReadings = 0;
            readingsThisSample = 0;
        }

        lock.lock();
    }
#elif defined(_WIN32)
    GpuEngineCounter gpu;
    CpuTicks cpuBefore;
    unsigned long long ownBefore = 0;
    bool haveCpu = readCpuTimes(cpuBefore, ownBefore);
    const unsigned long ownPid = GetCurrentProcessId();

    std::unique_lock<std::mutex> lock(m_mutex);
    while (!m_wake.wait_for(lock, SAMPLE_GAP, [this]() { return m_stop; }))
    {
        lock.unlock();

        CpuTicks cpuNow;
        unsigned long long ownNow = 0;
        if (haveCpu && readCpuTimes(cpuNow, ownNow))
        {
            m_load.loadPeak = std::max(
                m_load.loadPeak, otherCpuShare(cpuBefore, cpuNow, ownBefore, ownNow));
            cpuBefore = cpuNow;
            ownBefore = ownNow;
        }

        std::vector<GpuEngineUse> rows;
        if (gpu.collect(rows))
        {
            m_load.gpuBusyPeak =
                std::max(m_load.gpuBusyPeak, busiestGpuEnginePercent(rows, ownPid));
        }

        lock.lock();
    }
#endif
}

} // namespace Vestige
