// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file update_installer.cpp
#include "update/update_installer.h"

#include "utils/asset_locator.h"
#include "utils/path_sandbox.h"

#include <miniz.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#ifndef VESTIGE_PACKAGE
#define VESTIGE_PACKAGE ""
#endif

namespace Vestige::Update
{

namespace
{

/// The running executable's absolute path, or empty if it cannot be read.
std::string selfExecutablePath()
{
#ifdef _WIN32
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n == 0 || n >= std::size(buffer))
    {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer, n)).string();
#else
    std::error_code ec;
    const auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::string{} : p.string();
#endif
}

/// True when @a path is @a dir or lies beneath it, compared lexically after
/// normalising both.
bool isUnder(std::string_view path, std::string_view dir)
{
    namespace fs = std::filesystem;
    const fs::path p = fs::path(std::string(path)).lexically_normal();
    const fs::path d = fs::path(std::string(dir)).lexically_normal();
    auto pi = p.begin();
    for (auto di = d.begin(); di != d.end(); ++di, ++pi)
    {
        if (di->empty())
        {
            continue;  // a trailing separator on the directory
        }
        if (pi == p.end() || *pi != *di)
        {
            return false;
        }
    }
    return true;
}

}  // namespace

InstallKind detectInstall(std::string_view packageStamp, const char* appimageEnv,
                          const char* appdirEnv, std::string_view selfExePath)
{
    if (packageStamp == "windows-zip")
    {
        return InstallKind::WindowsZip;
    }
    if (packageStamp != "linux")
    {
        return InstallKind::None;
    }
    const bool runtimeSet = appimageEnv && *appimageEnv && appdirEnv && *appdirEnv;
    if (runtimeSet && !selfExePath.empty() && isUnder(selfExePath, appdirEnv))
    {
        return InstallKind::AppImage;
    }
    return InstallKind::Tarball;
}

InstallKind detectInstall()
{
    return detectInstall(VESTIGE_PACKAGE, std::getenv("APPIMAGE"), std::getenv("APPDIR"),
                         selfExecutablePath());
}

// ---------------------------------------------------------------- arguments

namespace
{
std::vector<std::string>& launchArguments()
{
    static std::vector<std::string> args;
    return args;
}
}  // namespace

void setLaunchArguments(int argc, char** argv)
{
    launchArguments().assign(argv, argv + argc);
}

std::vector<std::string> userArguments(const std::vector<std::string>& argv,
                                       const char* appdirEnv)
{
    std::vector<std::string> out;
    size_t i = 1;  // argv[0] is the program
    if (appdirEnv && *appdirEnv && argv.size() >= 3 && argv[1] == "--assets"
        && isUnder(argv[2], appdirEnv))
    {
        i = 3;
    }
    for (; i < argv.size(); ++i)
    {
        out.push_back(argv[i]);
    }
    return out;
}

InstallContext currentInstallContext()
{
    InstallContext ctx;
    if (const char* appimage = std::getenv("APPIMAGE"))
    {
        ctx.appimagePath = appimage;
    }
    ctx.installDir = executableDir();
    ctx.relaunchArgs = userArguments(launchArguments(), std::getenv("APPDIR"));
    return ctx;
}

// ---------------------------------------------------------------- zip

std::string extractZip(const std::vector<std::uint8_t>& zip, const std::filesystem::path& dest)
{
    namespace fs = std::filesystem;
    mz_zip_archive archive{};
    if (!mz_zip_reader_init_mem(&archive, zip.data(), zip.size(), 0))
    {
        return "the download is not a readable zip";
    }
    std::string error;
    const mz_uint count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint i = 0; i < count && error.empty(); ++i)
    {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&archive, i, &stat))
        {
            error = "the zip could not be read";
            break;
        }
        // Zip-slip guard: the entry must resolve inside dest.
        const std::string target = PathSandbox::resolveUriIntoBase(dest, stat.m_filename);
        if (target.empty())
        {
            error = std::string("the zip holds an unsafe path: ") + stat.m_filename;
            break;
        }
        std::error_code ec;
        if (mz_zip_reader_is_file_a_directory(&archive, i))
        {
            fs::create_directories(target, ec);
            continue;
        }
        fs::create_directories(fs::path(target).parent_path(), ec);
        size_t size = 0;
        void* data = mz_zip_reader_extract_to_heap(&archive, i, &size, 0);
        if (!data)
        {
            error = std::string("could not extract ") + stat.m_filename;
            break;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        mz_free(data);
        if (!out)
        {
            error = "could not write " + target;
        }
    }
    mz_zip_reader_end(&archive);
    return error;
}

// ---------------------------------------------------------------- AppImage

namespace
{

class AppImageInstaller final : public IInstaller
{
public:
    explicit AppImageInstaller(InstallContext ctx) : m_ctx(std::move(ctx)) {}

    std::string apply(const std::vector<std::uint8_t>& verified,
                      const std::string& version) override
    {
        namespace fs = std::filesystem;
        if (m_ctx.appimagePath.empty())
        {
            return "the AppImage's own path is unknown";
        }
        // Same directory, so the rename below stays on one filesystem and is
        // atomic: the old file is whole until the new one replaces it.
        const fs::path temp = m_ctx.appimagePath.parent_path()
            / (".vestige-update-" + version + ".part");
        {
            std::ofstream out(temp, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                return "the AppImage's folder is not writable";
            }
            out.write(reinterpret_cast<const char*>(verified.data()),
                      static_cast<std::streamsize>(verified.size()));
            out.flush();
            if (!out)
            {
                std::error_code ignore;
                fs::remove(temp, ignore);
                return "could not write the update";
            }
        }
        std::error_code ec;
        fs::permissions(temp,
                        fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec
                            | fs::perms::others_read | fs::perms::others_exec,
                        ec);
        if (!ec)
        {
            fs::rename(temp, m_ctx.appimagePath, ec);
        }
        if (ec)
        {
            std::error_code ignore;
            fs::remove(temp, ignore);
            return "could not replace the AppImage: " + ec.message();
        }
        return {};
    }

    std::string relaunch() override
    {
#ifdef _WIN32
        return "an AppImage cannot run on Windows";
#else
        // The new AppImage's runtime sets these for itself; the old runtime's
        // values would point it at our mount.
        for (const char* name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD"})
        {
            unsetenv(name);
        }
        const std::string path = m_ctx.appimagePath.string();
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(path.c_str()));
        for (auto& a : m_ctx.relaunchArgs)
        {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);
        execv(path.c_str(), argv.data());
        return "could not start the new version";  // execv only returns on failure
#endif
    }

private:
    InstallContext m_ctx;
};

// ---------------------------------------------------------------- Windows zip

/// The swap helper. Paths arrive as parameters, never pasted into the text.
constexpr const char* kSwapScript = R"PS(
param([string]$Install, [string]$Staged, [string]$StagingRoot)
$exe = Join-Path $Install 'vestige.exe'
$old = "$Install.old"
$deadline = (Get-Date).AddSeconds(60)
while (Get-Process | Where-Object { $_.Path -eq $exe }) {
    if ((Get-Date) -gt $deadline) { Remove-Item -Recurse -Force $StagingRoot -ErrorAction SilentlyContinue; exit 1 }
    Start-Sleep -Milliseconds 250
}
function Restore {
    if (Test-Path $old) {
        if (Test-Path $Install) { Remove-Item -Recurse -Force $Install -ErrorAction SilentlyContinue }
        Rename-Item $old (Split-Path $Install -Leaf)
    }
    Remove-Item -Recurse -Force $StagingRoot -ErrorAction SilentlyContinue
    Start-Process -FilePath $exe
}
try {
    if (Test-Path $old) { Remove-Item -Recurse -Force $old }
    Rename-Item $Install (Split-Path $old -Leaf) -ErrorAction Stop
    Move-Item $Staged $Install -ErrorAction Stop
    Remove-Item -Recurse -Force $StagingRoot -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $exe -PassThru -ErrorAction Stop
    if ($p.WaitForExit(20000) -and $p.ExitCode -ne 0) { Restore }
} catch { Restore }
)PS";

class WindowsZipInstaller final : public IInstaller
{
public:
    explicit WindowsZipInstaller(InstallContext ctx) : m_ctx(std::move(ctx)) {}

    std::string apply(const std::vector<std::uint8_t>& verified,
                      const std::string& version) override
    {
        namespace fs = std::filesystem;
        const fs::path install = m_ctx.installDir;
        if (install.empty() || !install.has_parent_path())
        {
            return "the install folder is unknown";
        }
        m_stagingRoot = install.parent_path() / (install.filename().string() + ".update-" + version);
        std::error_code ec;
        fs::remove_all(m_stagingRoot, ec);
        fs::create_directories(m_stagingRoot, ec);
        if (ec)
        {
            return "the folder holding Vestige is not writable, so it cannot update in place";
        }
        std::string error = extractZip(verified, m_stagingRoot);
        if (error.empty())
        {
            // The zip holds one top-level folder, vestige-<ver>-windows-x86_64/.
            int folders = 0;
            for (const auto& entry : fs::directory_iterator(m_stagingRoot, ec))
            {
                if (entry.is_directory())
                {
                    ++folders;
                    m_staged = entry.path();
                }
            }
            if (folders != 1 || !fs::is_regular_file(m_staged / "vestige.exe", ec))
            {
                error = "the download does not hold vestige.exe where expected";
            }
        }
        if (!error.empty())
        {
            std::error_code ignore;
            fs::remove_all(m_stagingRoot, ignore);
            m_staged.clear();
        }
        return error;
    }

    std::string relaunch() override
    {
#ifdef _WIN32
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path script = fs::temp_directory_path(ec) / "vestige-update-swap.ps1";
        {
            std::ofstream out(script, std::ios::trunc);
            out << kSwapScript;
            if (!out)
            {
                return "could not write the update helper";
            }
        }
        wchar_t systemDir[MAX_PATH];
        const UINT n = GetSystemDirectoryW(systemDir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH)
        {
            return "could not find Windows PowerShell";
        }
        const std::wstring powershell =
            std::wstring(systemDir, n) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
        auto quote = [](const std::wstring& s) { return L"\"" + s + L"\""; };
        std::wstring cmd = quote(powershell)
            + L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File " + quote(script.wstring())
            + L" -Install " + quote(m_ctx.installDir.wstring())
            + L" -Staged " + quote(m_staged.wstring())
            + L" -StagingRoot " + quote(m_stagingRoot.wstring());
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                            DETACHED_PROCESS | CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        {
            return "could not start the update helper";
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return {};
#else
        (void)kSwapScript;
        return "the Windows update helper only runs on Windows";
#endif
    }

private:
    InstallContext m_ctx;
    std::filesystem::path m_stagingRoot;
    std::filesystem::path m_staged;
};

}  // namespace

std::unique_ptr<IInstaller> installerFor(InstallKind kind, InstallContext context)
{
    switch (kind)
    {
        case InstallKind::AppImage:
            return std::make_unique<AppImageInstaller>(std::move(context));
        case InstallKind::WindowsZip:
            return std::make_unique<WindowsZipInstaller>(std::move(context));
        case InstallKind::None:
        case InstallKind::Tarball:
            break;
    }
    return nullptr;
}

bool openInBrowser(const std::string& url)
{
    if (url.rfind("https://", 0) != 0)
    {
        return false;
    }
#ifdef _WIN32
    const std::wstring wide(url.begin(), url.end());  // the URLs opened here are ASCII
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32;
#else
    // Spawned with an argument vector, never through a shell.
    const pid_t pid = fork();
    if (pid == 0)
    {
        execlp("xdg-open", "xdg-open", url.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    return pid > 0;
#endif
}

void cleanupAfterUpdate(InstallKind kind, const std::filesystem::path& installDir)
{
    if (kind != InstallKind::WindowsZip || installDir.empty())
    {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(installDir.string() + ".old", ec);
}

}  // namespace Vestige::Update
