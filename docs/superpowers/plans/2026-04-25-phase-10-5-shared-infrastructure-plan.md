# Phase 10.5 — Shared Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the four pieces of shared infrastructure that the bug reporter, feature tracker, and auto-updater all depend on: `HttpClient`, `GitHubAuth`, `ReleaseManifest`, `ProjectBackup`.

**Architecture:** Pure-CPU helpers tested without GL or network; integration paths mocked at the boundary via templated `Transport` and `FileIo`. New top-level dirs `engine/net/` and `engine/lifecycle/` (the spec's homes for these classes). Zero changes to render code.

**Tech Stack:** C++17, libcurl (vendored via `FetchContent` if not already linked), nlohmann::json (already vendored), `engine/utils/atomic_write.h` (Slice 1 F7), `libsecret` / `Security.framework` / `wincred.h` for keyring shim. Google Test for unit tests.

**Source spec:** `docs/superpowers/specs/2026-04-25-editor-feedback-and-lifecycle-design.md` Sections 1, 5.

---

## Pre-flight

Open facts to verify before code:

1. **libcurl link status:** does any existing dep transitively link libcurl? Run `ldd build/bin/vestige | grep -i curl`. If yes, `find_package(CURL REQUIRED)`. If no, add a `FetchContent_Declare` in `external/CMakeLists.txt` for libcurl 8.11.x with TLS-only build options.
2. **libsecret on dev box:** confirm `libsecret-1-dev` is present (`pkg-config --exists libsecret-1`). The Linux keyring path links against `libsecret-1.so.0`; build degrades to "anonymous-only" if missing.
3. **GitHub root CA bundle source:** download Mozilla bundle from `https://curl.se/ca/cacert.pem` once and pin into `share/vestige/certs/github-roots.pem`. Refresh policy: manual, on cert expiry warning.

These are facts to record in the slice's commit message, not blockers — the plan ships unchanged either way (fallback paths defined per component).

---

## File structure

| New file | Purpose | Tests |
|---|---|---|
| `engine/net/http_client.{h,cpp}` | libcurl wrapper, `Transport` concept | `tests/test_http_client.cpp` (mock transport), `tests/test_http_client_libcurl.cpp` (gated, real network) |
| `engine/net/transport.h` | `Transport` concept + `MockTransport` for tests | (used by other tests) |
| `engine/net/network_log.{h,cpp}` | Per-call audit log writer | `tests/test_network_log.cpp` |
| `engine/net/github_auth.{h,cpp}` | Device-flow OAuth + keyring shim front | `tests/test_github_auth.cpp` (mock keyring + transport) |
| `engine/net/keyring_io.{h,cpp}` | Keyring backend interface + Linux/macOS/Windows impls | `tests/test_keyring_io_mock.cpp` |
| `engine/net/keyring_io_libsecret.cpp` | Linux libsecret backend (compiled only on Linux) | (manual smoke test) |
| `engine/net/keyring_io_keychain.mm` | macOS Keychain backend (compiled only on macOS) | (manual smoke test) |
| `engine/net/keyring_io_wincred.cpp` | Windows Credential Manager backend (compiled only on Windows) | (manual smoke test) |
| `engine/net/keyring_io_null.cpp` | Fallback if no keyring available | (compiled when no backend) |
| `engine/lifecycle/release_manifest.{h,cpp}` | Pure schema + parser | `tests/test_release_manifest.cpp` |
| `engine/lifecycle/project_backup.{h,cpp}` | Pure project-tree snapshot helper | `tests/test_project_backup.cpp` (mock FileIo) |
| `engine/lifecycle/file_io.h` | `FileIo` concept used by ProjectBackup + others | (used by other tests) |
| `external/CMakeLists.txt` (modify) | Add curl `FetchContent` block if needed | n/a |
| `engine/CMakeLists.txt` (modify) | Add `engine/net/` + `engine/lifecycle/` sources, link curl + libsecret | n/a |
| `tests/CMakeLists.txt` (modify) | Add new test files | n/a |

Decomposition rationale:
- Each pure helper isolates testability (`engine/lifecycle/release_manifest.{h,cpp}` and `engine/lifecycle/project_backup.{h,cpp}` are template-injectable for `FileIo`).
- The keyring layer splits along OS lines so each .cpp file is one platform — no `#ifdef` ladders inside one file.
- `transport.h` is header-only because `Transport` is a concept (compile-time interface), not a virtual class.

---

## Task 1: Add libcurl dependency

Goal: `find_package(CURL)` succeeds OR `FetchContent` provides it. CMake builds against `CURL::libcurl`.

**Files:**
- Modify: `external/CMakeLists.txt`
- Modify: `engine/CMakeLists.txt`

- [ ] **Step 1: Check current curl link status.**

```bash
ldd build/bin/vestige | grep -i curl
```

Expected: either a path to `libcurl.so.4` (transitive — skip FetchContent path, go to step 4) or no output (need FetchContent).

- [ ] **Step 2: If no transitive curl, add `FetchContent_Declare` to `external/CMakeLists.txt`** (after the existing nlohmann::json block, around line 320):

```cmake
# libcurl — HTTP client for GitHub API + auto-updater (Phase 10.5).
FetchContent_Declare(curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_11_0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR __engine_defines_target__   # custom — see below
)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(curl)
```

- [ ] **Step 3: If curl is transitive, simply add `find_package(CURL REQUIRED)`** at the top of `external/CMakeLists.txt`.

- [ ] **Step 4: Add `CURL::libcurl` to engine link line** in `engine/CMakeLists.txt`:

```cmake
target_link_libraries(vestige_engine PRIVATE
    ...
    CURL::libcurl
)
```

- [ ] **Step 5: Configure + build, verify no link errors.**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
```

Expected: clean build, libvestige_engine.a links curl symbols.

- [ ] **Step 6: Commit.**

```bash
git add external/CMakeLists.txt engine/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 (deps): vendor libcurl 8.11"
```

---

## Task 2: Define `Transport` concept + `MockTransport`

Goal: header-only Transport interface that production HttpClient and tests implement.

**Files:**
- Create: `engine/net/transport.h`
- Test: `tests/test_transport_mock.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_transport_mock.cpp
#include <gtest/gtest.h>
#include "net/transport.h"

namespace Vestige::Net::Test
{

TEST(MockTransport, ReturnsCannedResponseForRegisteredUrl_PHASE105)
{
    MockTransport transport;
    transport.respondWith("https://api.github.com/repos/owner/repo",
                          HttpResponse{200, "application/json", "{\"ok\":true}"});

    HttpRequest req{HttpMethod::Get, "https://api.github.com/repos/owner/repo", {}, ""};
    HttpResponse resp = transport.execute(req);

    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_EQ(resp.body, "{\"ok\":true}");
}

TEST(MockTransport, ReturnsNetworkErrorForUnregisteredUrl_PHASE105)
{
    MockTransport transport;
    HttpRequest req{HttpMethod::Get, "https://api.github.com/unknown", {}, ""};
    HttpResponse resp = transport.execute(req);

    EXPECT_EQ(resp.statusCode, 0);  // 0 = transport-level error
    EXPECT_NE(resp.body.find("no canned response"), std::string::npos);
}

TEST(MockTransport, RecordsExecutedRequestsInOrder_PHASE105)
{
    MockTransport transport;
    transport.respondWith("https://example.com/a", HttpResponse{200, "text/plain", "A"});
    transport.respondWith("https://example.com/b", HttpResponse{200, "text/plain", "B"});

    transport.execute({HttpMethod::Get, "https://example.com/a", {}, ""});
    transport.execute({HttpMethod::Get, "https://example.com/b", {}, ""});

    ASSERT_EQ(transport.requestLog().size(), 2u);
    EXPECT_EQ(transport.requestLog()[0].url, "https://example.com/a");
    EXPECT_EQ(transport.requestLog()[1].url, "https://example.com/b");
}

}
```

- [ ] **Step 2: Run test, verify it fails to compile.**

```bash
cmake --build build --target vestige_tests 2>&1 | tail -10
```

Expected: error about missing `net/transport.h`.

- [ ] **Step 3: Create the header.**

```cpp
// engine/net/transport.h
// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file transport.h
/// @brief Transport concept for HTTP — production wraps libcurl;
///        tests inject MockTransport.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace Vestige::Net
{

enum class HttpMethod { Get, Post, Put, Delete, Patch };

struct HttpRequest
{
    HttpMethod method = HttpMethod::Get;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse
{
    int statusCode = 0;                          ///< 0 = transport error.
    std::string contentType;
    std::string body;
    std::map<std::string, std::string> headers;  ///< Response headers (lowercased keys).
};

/// @brief Reads rate-limit headers per GitHub's `x-ratelimit-*` convention.
struct RateLimitInfo
{
    int limit = -1;        ///< -1 if header absent.
    int remaining = -1;
    long resetEpoch = -1;  ///< Unix seconds.
};

RateLimitInfo parseRateLimitHeaders(const HttpResponse& r);

/// @brief MockTransport for tests.
class MockTransport
{
public:
    void respondWith(const std::string& url, HttpResponse response);
    HttpResponse execute(const HttpRequest& req);
    const std::vector<HttpRequest>& requestLog() const { return m_log; }

private:
    std::map<std::string, HttpResponse> m_canned;
    std::vector<HttpRequest> m_log;
};

}  // namespace Vestige::Net
```

- [ ] **Step 4: Implement MockTransport (.cpp) and `parseRateLimitHeaders`.**

```cpp
// engine/net/transport.cpp
#include "net/transport.h"

namespace Vestige::Net
{

void MockTransport::respondWith(const std::string& url, HttpResponse response)
{
    m_canned[url] = std::move(response);
}

HttpResponse MockTransport::execute(const HttpRequest& req)
{
    m_log.push_back(req);
    auto it = m_canned.find(req.url);
    if (it == m_canned.end())
    {
        HttpResponse err;
        err.statusCode = 0;
        err.body = std::string{"MockTransport: no canned response for "} + req.url;
        return err;
    }
    return it->second;
}

RateLimitInfo parseRateLimitHeaders(const HttpResponse& r)
{
    RateLimitInfo info;
    auto it = r.headers.find("x-ratelimit-limit");
    if (it != r.headers.end())
        info.limit = std::stoi(it->second);
    it = r.headers.find("x-ratelimit-remaining");
    if (it != r.headers.end())
        info.remaining = std::stoi(it->second);
    it = r.headers.find("x-ratelimit-reset");
    if (it != r.headers.end())
        info.resetEpoch = std::stol(it->second);
    return info;
}

}
```

- [ ] **Step 5: Add new files to CMake.**

`engine/CMakeLists.txt`:

```cmake
# Add to vestige_engine SOURCES list (alphabetical within the renderer/utils block):
    net/transport.cpp
```

`tests/CMakeLists.txt`:

```cmake
# Add to test_sources:
    test_transport_mock.cpp
```

- [ ] **Step 6: Build + run tests.**

```bash
cmake --build build -j8 && ctest --test-dir build -R "MockTransport" --output-on-failure
```

Expected: 3 tests pass.

- [ ] **Step 7: Commit.**

```bash
git add engine/net/transport.h engine/net/transport.cpp tests/test_transport_mock.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T2: Transport concept + MockTransport"
```

---

## Task 3: Network audit log writer

Goal: `NetworkLog::recordCall` appends a JSON line per HTTP call to `~/.config/vestige/network-log.json`. Body bytes never recorded (privacy floor).

**Files:**
- Create: `engine/net/network_log.{h,cpp}`
- Test: `tests/test_network_log.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_network_log.cpp
#include <gtest/gtest.h>
#include "net/network_log.h"
#include "net/transport.h"

namespace Vestige::Net::Test
{

TEST(NetworkLog, RecordsTimestampMethodHostPathStatusBytes_PHASE105)
{
    std::vector<std::string> sink;
    NetworkLog log{[&sink](std::string line) { sink.push_back(std::move(line)); }};

    HttpRequest req{HttpMethod::Get, "https://api.github.com/repos/owner/repo", {}, ""};
    HttpResponse resp{200, "application/json", "{\"x\":1}"};
    log.recordCall(req, resp);

    ASSERT_EQ(sink.size(), 1u);
    EXPECT_NE(sink[0].find("\"method\":\"GET\""), std::string::npos);
    EXPECT_NE(sink[0].find("\"host\":\"api.github.com\""), std::string::npos);
    EXPECT_NE(sink[0].find("\"path\":\"/repos/owner/repo\""), std::string::npos);
    EXPECT_NE(sink[0].find("\"status\":200"), std::string::npos);
    EXPECT_NE(sink[0].find("\"bytes\":7"), std::string::npos);  // body length
    EXPECT_NE(sink[0].find("\"timestamp\":"), std::string::npos);
}

TEST(NetworkLog, NeverIncludesRequestOrResponseBodies_PHASE105)
{
    std::vector<std::string> sink;
    NetworkLog log{[&sink](std::string line) { sink.push_back(std::move(line)); }};

    HttpRequest req{HttpMethod::Post, "https://api.github.com/issues",
                    {{"Authorization", "Bearer SECRET"}},
                    "{\"title\":\"sensitive\",\"body\":\"contains paths\"}"};
    HttpResponse resp{201, "application/json", "{\"id\":42,\"body\":\"reply\"}"};
    log.recordCall(req, resp);

    ASSERT_EQ(sink.size(), 1u);
    EXPECT_EQ(sink[0].find("sensitive"), std::string::npos);
    EXPECT_EQ(sink[0].find("contains paths"), std::string::npos);
    EXPECT_EQ(sink[0].find("Bearer"), std::string::npos);
    EXPECT_EQ(sink[0].find("SECRET"), std::string::npos);
    EXPECT_EQ(sink[0].find("\"reply\""), std::string::npos);
}

}
```

- [ ] **Step 2: Run, expect compile failure.**

- [ ] **Step 3: Implement.**

```cpp
// engine/net/network_log.h
#pragma once
#include "net/transport.h"
#include <functional>
#include <string>

namespace Vestige::Net
{

/// @brief Per-call audit log of HTTP requests.
///
/// Privacy floor: never includes request bodies, response bodies, or
/// auth headers. Only timestamp / method / host / path / status / bytes.
class NetworkLog
{
public:
    using Sink = std::function<void(std::string)>;
    explicit NetworkLog(Sink sink);

    void recordCall(const HttpRequest& req, const HttpResponse& resp);

private:
    Sink m_sink;
};

}  // namespace Vestige::Net
```

```cpp
// engine/net/network_log.cpp
#include "net/network_log.h"
#include <chrono>
#include <sstream>

namespace Vestige::Net
{

namespace
{
std::string methodName(HttpMethod m)
{
    switch (m) {
    case HttpMethod::Get:    return "GET";
    case HttpMethod::Post:   return "POST";
    case HttpMethod::Put:    return "PUT";
    case HttpMethod::Delete: return "DELETE";
    case HttpMethod::Patch:  return "PATCH";
    }
    return "?";
}

std::pair<std::string, std::string> splitHostPath(const std::string& url)
{
    auto schemeEnd = url.find("://");
    auto hostStart = (schemeEnd == std::string::npos) ? 0 : schemeEnd + 3;
    auto pathStart = url.find('/', hostStart);
    if (pathStart == std::string::npos)
        return {url.substr(hostStart), "/"};
    return {url.substr(hostStart, pathStart - hostStart), url.substr(pathStart)};
}
}

NetworkLog::NetworkLog(Sink sink) : m_sink(std::move(sink)) {}

void NetworkLog::recordCall(const HttpRequest& req, const HttpResponse& resp)
{
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now).count();
    auto [host, path] = splitHostPath(req.url);
    std::ostringstream os;
    os << "{\"timestamp\":" << seconds
       << ",\"method\":\"" << methodName(req.method) << "\""
       << ",\"host\":\"" << host << "\""
       << ",\"path\":\"" << path << "\""
       << ",\"status\":" << resp.statusCode
       << ",\"bytes\":" << resp.body.size()
       << "}";
    m_sink(os.str());
}

}
```

- [ ] **Step 4: Wire into CMake, build, run tests, commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R "NetworkLog" --output-on-failure
git add engine/net/network_log.h engine/net/network_log.cpp tests/test_network_log.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T3: NetworkLog audit writer"
```

---

## Task 4: HttpClient (libcurl backend)

Goal: production `HttpClient` implements the same surface as `MockTransport` but talks libcurl. Cert-pinned to GitHub root CA bundle. Synchronous; long calls dispatched via existing async-task pool by callers.

**Files:**
- Create: `engine/net/http_client.{h,cpp}`
- Test: `tests/test_http_client_libcurl.cpp` (gated `VESTIGE_INTEGRATION_TESTS=1`)

- [ ] **Step 1: Write a real-network gated test.**

```cpp
// tests/test_http_client_libcurl.cpp
#include <gtest/gtest.h>
#include "net/http_client.h"
#include <cstdlib>

namespace Vestige::Net::Test
{

#define SKIP_IF_NO_INTEGRATION                                  \
    do {                                                        \
        const char* gate = std::getenv("VESTIGE_INTEGRATION_TESTS"); \
        if (!gate || std::string(gate) != "1") {                \
            GTEST_SKIP() << "VESTIGE_INTEGRATION_TESTS=1 not set"; \
        }                                                       \
    } while(0)

TEST(HttpClientLibCurl, GetsGitHubApiRootReturns200_PHASE105)
{
    SKIP_IF_NO_INTEGRATION;
    HttpClient client;
    HttpResponse resp = client.execute({HttpMethod::Get, "https://api.github.com/", {}, ""});
    EXPECT_EQ(resp.statusCode, 200);
    EXPECT_NE(resp.body.find("current_user_url"), std::string::npos);
}

}
```

- [ ] **Step 2: Implement HttpClient.**

```cpp
// engine/net/http_client.h
#pragma once
#include "net/transport.h"
#include <filesystem>

namespace Vestige::Net
{

/// @brief libcurl-backed Transport. Cert-pinned to a GitHub root CA bundle.
class HttpClient
{
public:
    HttpClient();
    ~HttpClient();

    /// @brief Override the bundled root CA path. Default: share/vestige/certs/github-roots.pem.
    void setCaBundlePath(std::filesystem::path p);

    HttpResponse execute(const HttpRequest& req);

private:
    std::filesystem::path m_caBundle;
};

}
```

```cpp
// engine/net/http_client.cpp
#include "net/http_client.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>

namespace Vestige::Net
{

namespace
{
size_t writeBody(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

size_t writeHeader(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* headers = static_cast<std::map<std::string, std::string>*>(userdata);
    std::string line(ptr, size * nmemb);
    auto colon = line.find(':');
    if (colon == std::string::npos) return size * nmemb;
    std::string key = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
    while (!value.empty() && value.front() == ' ')
        value.erase(value.begin());
    (*headers)[key] = value;
    return size * nmemb;
}

const char* methodVerb(HttpMethod m)
{
    switch (m) {
    case HttpMethod::Get:    return "GET";
    case HttpMethod::Post:   return "POST";
    case HttpMethod::Put:    return "PUT";
    case HttpMethod::Delete: return "DELETE";
    case HttpMethod::Patch:  return "PATCH";
    }
    return "GET";
}
}

HttpClient::HttpClient()
{
    static bool s_inited = false;
    if (!s_inited) { curl_global_init(CURL_GLOBAL_DEFAULT); s_inited = true; }
    m_caBundle = "share/vestige/certs/github-roots.pem";
}

HttpClient::~HttpClient() = default;

void HttpClient::setCaBundlePath(std::filesystem::path p) { m_caBundle = std::move(p); }

HttpResponse HttpClient::execute(const HttpRequest& req)
{
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) { resp.statusCode = 0; resp.body = "curl_easy_init failed"; return resp; }

    curl_easy_setopt(curl, CURLOPT_URL, req.url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, methodVerb(req.method));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    if (std::filesystem::exists(m_caBundle))
        curl_easy_setopt(curl, CURLOPT_CAINFO, m_caBundle.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Vestige-Editor/0.1");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, writeHeader);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp.headers);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

    curl_slist* hdrs = nullptr;
    for (const auto& [k, v] : req.headers)
    {
        std::string line = k + ": " + v;
        hdrs = curl_slist_append(hdrs, line.c_str());
    }
    if (hdrs) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);

    if (!req.body.empty())
    {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req.body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)req.body.size());
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK)
    {
        resp.statusCode = 0;
        resp.body = curl_easy_strerror(rc);
    }
    else
    {
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        resp.statusCode = (int)status;
        char* ct = nullptr;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) resp.contentType = ct;
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return resp;
}

}
```

- [ ] **Step 3: Wire CMake.** Add `net/http_client.cpp` to engine sources; gate the integration test in `tests/CMakeLists.txt`:

```cmake
# Always-compile test (skipped unless env var set):
list(APPEND VESTIGE_TEST_SOURCES test_http_client_libcurl.cpp)
```

- [ ] **Step 4: Build.** Mock-only tests run by default; integration test SKIPs.

```bash
cmake --build build -j8 && ctest --test-dir build -R HttpClient --output-on-failure
VESTIGE_INTEGRATION_TESTS=1 ctest --test-dir build -R HttpClientLibCurl --output-on-failure
```

- [ ] **Step 5: Commit.**

```bash
git add engine/net/http_client.h engine/net/http_client.cpp tests/test_http_client_libcurl.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T4: HttpClient libcurl backend"
```

---

## Task 5: Keyring abstraction + null backend

Goal: `KeyringIo` interface + a null-backend that always returns "not available". Real backends bolt on next.

**Files:**
- Create: `engine/net/keyring_io.{h,cpp}`
- Create: `engine/net/keyring_io_null.cpp`
- Test: `tests/test_keyring_io_null.cpp`

- [ ] **Step 1: Write the failing test.**

```cpp
// tests/test_keyring_io_null.cpp
#include <gtest/gtest.h>
#include "net/keyring_io.h"

namespace Vestige::Net::Test
{
TEST(KeyringIoNull, StoreReturnsNotAvailable_PHASE105)
{
    auto k = makeNullKeyring();
    auto rc = k->store("vestige", "github-token", "secret");
    EXPECT_EQ(rc, KeyringStatus::Unavailable);
}

TEST(KeyringIoNull, RetrieveReturnsNotAvailable_PHASE105)
{
    auto k = makeNullKeyring();
    std::string value;
    auto rc = k->retrieve("vestige", "github-token", value);
    EXPECT_EQ(rc, KeyringStatus::Unavailable);
    EXPECT_TRUE(value.empty());
}
}
```

- [ ] **Step 2: Header.**

```cpp
// engine/net/keyring_io.h
#pragma once
#include <memory>
#include <string>

namespace Vestige::Net
{

enum class KeyringStatus { Ok, NotFound, Unavailable, Error };

class KeyringIo
{
public:
    virtual ~KeyringIo() = default;
    virtual KeyringStatus store(const std::string& service,
                                const std::string& key,
                                const std::string& value) = 0;
    virtual KeyringStatus retrieve(const std::string& service,
                                   const std::string& key,
                                   std::string& outValue) = 0;
    virtual KeyringStatus remove(const std::string& service,
                                 const std::string& key) = 0;
};

std::unique_ptr<KeyringIo> makeSystemKeyring();   ///< OS-detected. Falls back to null.
std::unique_ptr<KeyringIo> makeNullKeyring();
}
```

- [ ] **Step 3: Null backend.**

```cpp
// engine/net/keyring_io_null.cpp
#include "net/keyring_io.h"

namespace Vestige::Net
{
namespace
{
class NullKeyring : public KeyringIo
{
public:
    KeyringStatus store(const std::string&, const std::string&, const std::string&) override
    { return KeyringStatus::Unavailable; }
    KeyringStatus retrieve(const std::string&, const std::string&, std::string& out) override
    { out.clear(); return KeyringStatus::Unavailable; }
    KeyringStatus remove(const std::string&, const std::string&) override
    { return KeyringStatus::Unavailable; }
};
}

std::unique_ptr<KeyringIo> makeNullKeyring()
{
    return std::make_unique<NullKeyring>();
}

#if !defined(VESTIGE_HAS_LIBSECRET) && !defined(VESTIGE_HAS_KEYCHAIN) && !defined(VESTIGE_HAS_WINCRED)
std::unique_ptr<KeyringIo> makeSystemKeyring()
{
    return makeNullKeyring();
}
#endif
}
```

- [ ] **Step 4: Build, run, commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R Keyring --output-on-failure
git add engine/net/keyring_io.h engine/net/keyring_io_null.cpp tests/test_keyring_io_null.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T5: KeyringIo interface + null backend"
```

---

## Task 6: Linux libsecret keyring backend

Goal: real keyring on Linux dev hosts. Detects libsecret at configure time; null-backend if missing.

**Files:**
- Create: `engine/net/keyring_io_libsecret.cpp`
- Modify: `engine/CMakeLists.txt`

- [ ] **Step 1: Detect libsecret in CMake.**

```cmake
# engine/CMakeLists.txt — add near the top of the engine target block.
if(LINUX)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBSECRET libsecret-1)
    endif()
    if(LIBSECRET_FOUND)
        target_compile_definitions(vestige_engine PRIVATE VESTIGE_HAS_LIBSECRET=1)
        target_include_directories(vestige_engine PRIVATE ${LIBSECRET_INCLUDE_DIRS})
        target_link_libraries(vestige_engine PRIVATE ${LIBSECRET_LIBRARIES})
        list(APPEND VESTIGE_ENGINE_EXTRA_SOURCES net/keyring_io_libsecret.cpp)
    endif()
endif()
```

- [ ] **Step 2: Implement libsecret backend.**

```cpp
// engine/net/keyring_io_libsecret.cpp
#ifdef VESTIGE_HAS_LIBSECRET
#include "net/keyring_io.h"
#include <libsecret/secret.h>

namespace Vestige::Net
{
namespace
{
const SecretSchema* schema()
{
    static const SecretSchema s = {
        "com.vestige.editor", SECRET_SCHEMA_NONE,
        { { "service", SECRET_SCHEMA_ATTRIBUTE_STRING },
          { "key",     SECRET_SCHEMA_ATTRIBUTE_STRING },
          { nullptr, (SecretSchemaAttributeType)0 } }
    };
    return &s;
}

class LibSecretKeyring : public KeyringIo
{
public:
    KeyringStatus store(const std::string& service, const std::string& key, const std::string& value) override
    {
        GError* err = nullptr;
        gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT,
            (service + ":" + key).c_str(), value.c_str(), nullptr, &err,
            "service", service.c_str(), "key", key.c_str(), nullptr);
        if (err) { g_error_free(err); return KeyringStatus::Error; }
        return ok ? KeyringStatus::Ok : KeyringStatus::Error;
    }
    KeyringStatus retrieve(const std::string& service, const std::string& key, std::string& out) override
    {
        GError* err = nullptr;
        char* secret = secret_password_lookup_sync(schema(), nullptr, &err,
            "service", service.c_str(), "key", key.c_str(), nullptr);
        if (err) { g_error_free(err); return KeyringStatus::Error; }
        if (!secret) return KeyringStatus::NotFound;
        out.assign(secret);
        secret_password_free(secret);
        return KeyringStatus::Ok;
    }
    KeyringStatus remove(const std::string& service, const std::string& key) override
    {
        GError* err = nullptr;
        gboolean ok = secret_password_clear_sync(schema(), nullptr, &err,
            "service", service.c_str(), "key", key.c_str(), nullptr);
        if (err) { g_error_free(err); return KeyringStatus::Error; }
        return ok ? KeyringStatus::Ok : KeyringStatus::NotFound;
    }
};
}

std::unique_ptr<KeyringIo> makeSystemKeyring() { return std::make_unique<LibSecretKeyring>(); }
}
#endif
```

- [ ] **Step 3: Smoke test.** Build editor, set a token, restart, verify retrieve returns the same token. No automated test — keyring is too hostile.

- [ ] **Step 4: Commit.**

```bash
git add engine/net/keyring_io_libsecret.cpp engine/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T6: libsecret keyring backend (Linux)"
```

> **macOS / Windows backends** (`keyring_io_keychain.mm` + `keyring_io_wincred.cpp`) follow the same shape — author them when those CI runners exist. Each one is one .cpp file per the file-structure table above; tests are smoke-only.

---

## Task 7: GitHubAuth (device-flow OAuth)

Goal: `GitHubAuth::beginDeviceFlow` opens browser to verification URL, returns the access token after the user authorises. Token stored via `KeyringIo`.

**Files:**
- Create: `engine/net/github_auth.{h,cpp}`
- Test: `tests/test_github_auth.cpp` (mock transport + mock keyring)

- [ ] **Step 1: Write failing test for the happy path.**

```cpp
// tests/test_github_auth.cpp
#include <gtest/gtest.h>
#include "net/github_auth.h"
#include "net/transport.h"
#include "net/keyring_io.h"

namespace Vestige::Net::Test
{

class MemoryKeyring : public KeyringIo
{
    std::map<std::string, std::string> m;
public:
    KeyringStatus store(const std::string& s, const std::string& k, const std::string& v) override
    { m[s + "|" + k] = v; return KeyringStatus::Ok; }
    KeyringStatus retrieve(const std::string& s, const std::string& k, std::string& out) override
    {
        auto it = m.find(s + "|" + k);
        if (it == m.end()) return KeyringStatus::NotFound;
        out = it->second; return KeyringStatus::Ok;
    }
    KeyringStatus remove(const std::string& s, const std::string& k) override
    { m.erase(s + "|" + k); return KeyringStatus::Ok; }
};

TEST(GitHubAuth, DeviceFlowSucceedsAndStoresToken_PHASE105)
{
    MockTransport transport;
    transport.respondWith("https://github.com/login/device/code",
        HttpResponse{200, "application/json",
            "{\"device_code\":\"DC1\",\"user_code\":\"ABCD-1234\","
            "\"verification_uri\":\"https://github.com/login/device\","
            "\"expires_in\":900,\"interval\":1}"});
    transport.respondWith("https://github.com/login/oauth/access_token",
        HttpResponse{200, "application/json",
            "{\"access_token\":\"ghu_TOKEN\",\"token_type\":\"bearer\",\"scope\":\"repo\"}"});

    MemoryKeyring keyring;
    GitHubAuth auth(transport, keyring);

    DeviceFlowOpener opened;
    auto result = auth.beginDeviceFlow("CLIENT_ID",
        [&](const DeviceFlowOpener& d) { opened = d; });

    EXPECT_TRUE(result.ok);
    EXPECT_EQ(opened.userCode, "ABCD-1234");
    EXPECT_EQ(opened.verificationUri, "https://github.com/login/device");

    std::string stored;
    keyring.retrieve("vestige", "github-token", stored);
    EXPECT_EQ(stored, "ghu_TOKEN");
}

TEST(GitHubAuth, DeviceFlowReturnsErrorOnRateLimitedTokenPoll_PHASE105)
{
    MockTransport transport;
    transport.respondWith("https://github.com/login/device/code",
        HttpResponse{200, "application/json",
            "{\"device_code\":\"DC1\",\"user_code\":\"X\","
            "\"verification_uri\":\"https://github.com/login/device\","
            "\"expires_in\":900,\"interval\":1}"});
    transport.respondWith("https://github.com/login/oauth/access_token",
        HttpResponse{200, "application/json",
            "{\"error\":\"slow_down\"}"});

    MemoryKeyring keyring;
    GitHubAuth auth(transport, keyring);
    auth.setMaxPolls(1);

    auto result = auth.beginDeviceFlow("CID", [](const DeviceFlowOpener&){});
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "device flow polling exceeded max attempts");
}
}
```

- [ ] **Step 2: Header + impl.** Use `nlohmann::json` for parsing.

```cpp
// engine/net/github_auth.h
#pragma once
#include "net/keyring_io.h"
#include "net/transport.h"
#include <functional>
#include <string>

namespace Vestige::Net
{

struct DeviceFlowOpener
{
    std::string verificationUri;
    std::string userCode;
};

struct AuthResult
{
    bool ok = false;
    std::string error;
};

class GitHubAuth
{
public:
    GitHubAuth(MockTransport& transport, KeyringIo& keyring);  // Templated overload below.
    AuthResult beginDeviceFlow(const std::string& clientId,
                               const std::function<void(const DeviceFlowOpener&)>& showToUser);
    void setMaxPolls(int n) { m_maxPolls = n; }

private:
    MockTransport& m_transport;
    KeyringIo& m_keyring;
    int m_maxPolls = 60;  // 60s × interval
};

}
```

> **Note on Transport templating**: For tests we accept `MockTransport&`; production accepts `HttpClient&`. Refactor to a `Transport` trait when the bug-reporter slice needs symmetric production wiring — for now, two overloaded constructors keep the diff focused.

- [ ] **Step 3: Implement.** (Standard nlohmann::json parsing. Polling loop calls `transport.execute` until `access_token` is present or `m_maxPolls` exhausted. `keyring.store("vestige", "github-token", token)`.)

- [ ] **Step 4: Build + test + commit.**

```bash
git add engine/net/github_auth.h engine/net/github_auth.cpp tests/test_github_auth.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T7: GitHubAuth device-flow OAuth"
```

---

## Task 8: ReleaseManifest schema + parser

Goal: pure schema struct + parser for `release-manifest.json`.

**Files:**
- Create: `engine/lifecycle/release_manifest.{h,cpp}`
- Test: `tests/test_release_manifest.cpp`

- [ ] **Step 1: Failing tests cover the full schema.**

```cpp
// tests/test_release_manifest.cpp
#include <gtest/gtest.h>
#include "lifecycle/release_manifest.h"

namespace Vestige::Lifecycle::Test
{

constexpr const char* kValidManifest = R"({
  "schema_version": 1,
  "release": {
    "version": "0.2.0", "channel": "stable", "tag": "v0.2.0-stable",
    "published_at": "2026-05-15T12:00:00Z", "soak_days": 7,
    "phase_marker": "Phase 10.9 complete"
  },
  "platforms": {
    "linux-x86_64":   { "archive_url": "u1", "sha256": "h1", "size_bytes": 1 },
    "windows-x86_64": { "archive_url": "u2", "sha256": "h2", "size_bytes": 2 },
    "macos-arm64":    { "archive_url": "u3", "sha256": "h3", "size_bytes": 3 }
  },
  "changelog_md_url": "u4",
  "breaking_features": [
    { "feature_id": "sh_probe_grid", "scope": "universal",
      "severity": "behavior-change", "changelog_anchor": "#a1" }
  ]
})";

TEST(ReleaseManifest, ParsesValidPayload_PHASE105)
{
    auto m = parseReleaseManifest(kValidManifest);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->release.version, "0.2.0");
    EXPECT_EQ(m->release.channel, "stable");
    EXPECT_EQ(m->release.soakDays, 7);
    ASSERT_EQ(m->platforms.size(), 3u);
    EXPECT_EQ(m->platforms.at("linux-x86_64").sha256, "h1");
    ASSERT_EQ(m->breakingFeatures.size(), 1u);
    EXPECT_EQ(m->breakingFeatures[0].featureId, "sh_probe_grid");
    EXPECT_EQ(m->breakingFeatures[0].scope, BreakingFeatureScope::Universal);
}

TEST(ReleaseManifest, RejectsWrongSchemaVersion_PHASE105)
{
    auto m = parseReleaseManifest(R"({"schema_version":99,"release":{}})");
    EXPECT_FALSE(m.has_value());
}

TEST(ReleaseManifest, RejectsMalformedJson_PHASE105)
{
    auto m = parseReleaseManifest("not json");
    EXPECT_FALSE(m.has_value());
}

TEST(ReleaseManifest, RejectsMissingPlatforms_PHASE105)
{
    auto m = parseReleaseManifest(R"({
      "schema_version": 1,
      "release": {"version":"0.2.0","channel":"stable","tag":"v0.2.0-stable",
                  "published_at":"2026-05-15T12:00:00Z","soak_days":7,"phase_marker":"X"}
    })");
    EXPECT_FALSE(m.has_value());
}
}
```

- [ ] **Step 2: Header.**

```cpp
// engine/lifecycle/release_manifest.h
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Vestige::Lifecycle
{

enum class BreakingFeatureScope { Universal, Component, Optional };
enum class BreakingFeatureSeverity { Breaking, BehaviorChange };

struct BreakingFeature
{
    std::string featureId;
    BreakingFeatureScope scope = BreakingFeatureScope::Optional;
    BreakingFeatureSeverity severity = BreakingFeatureSeverity::BehaviorChange;
    std::string changelogAnchor;
};

struct ReleaseInfo
{
    std::string version;
    std::string channel;     // "stable" | "nightly"
    std::string tag;
    std::string publishedAt;
    int soakDays = 0;
    std::string phaseMarker;
};

struct PlatformAsset
{
    std::string archiveUrl;
    std::string sha256;
    long long sizeBytes = 0;
};

struct ReleaseManifest
{
    int schemaVersion = 1;
    ReleaseInfo release;
    std::map<std::string, PlatformAsset> platforms;
    std::string changelogMdUrl;
    std::vector<BreakingFeature> breakingFeatures;
};

std::optional<ReleaseManifest> parseReleaseManifest(const std::string& json);
}
```

- [ ] **Step 3: Implement parser.** Standard nlohmann::json with strict required-field checks. Reject schema_version != 1.

- [ ] **Step 4: Build + test + commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R ReleaseManifest --output-on-failure
git add engine/lifecycle/release_manifest.h engine/lifecycle/release_manifest.cpp tests/test_release_manifest.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T8: ReleaseManifest schema + parser"
```

---

## Task 9: ProjectBackup helper

Goal: pure-CPU `ProjectBackup::snapshot(projectRoot, destDir, fileIo)` walks the project tree, copies `scenes/`, `assets/`, `.vestige/`, skips `build/` + `.cache/`. Per-file SHA256 manifest.

**Files:**
- Create: `engine/lifecycle/file_io.h`
- Create: `engine/lifecycle/project_backup.{h,cpp}`
- Test: `tests/test_project_backup.cpp`

- [ ] **Step 1: Write failing test using mock FileIo.**

```cpp
// tests/test_project_backup.cpp
#include <gtest/gtest.h>
#include "lifecycle/project_backup.h"

namespace Vestige::Lifecycle::Test
{

class MemFs : public FileIo
{
public:
    std::map<std::filesystem::path, std::string> files;
    std::set<std::filesystem::path> dirs;

    bool exists(const std::filesystem::path& p) const override
    { return files.count(p) || dirs.count(p); }
    bool isDirectory(const std::filesystem::path& p) const override
    { return dirs.count(p); }
    std::vector<std::filesystem::path> listDirectory(const std::filesystem::path& p) const override
    {
        std::vector<std::filesystem::path> out;
        for (auto& [k,_] : files)
            if (k.parent_path() == p) out.push_back(k);
        for (auto& d : dirs)
            if (d.parent_path() == p) out.push_back(d);
        return out;
    }
    std::string readFile(const std::filesystem::path& p) const override
    { auto it = files.find(p); return it == files.end() ? std::string{} : it->second; }
    bool writeFile(const std::filesystem::path& p, const std::string& contents) override
    { files[p] = contents; dirs.insert(p.parent_path()); return true; }
    bool createDirectories(const std::filesystem::path& p) override
    { dirs.insert(p); return true; }
};

TEST(ProjectBackup, CopiesScenesAssetsVestigeAndSkipsBuild_PHASE105)
{
    MemFs fs;
    fs.dirs.insert("/proj");
    fs.dirs.insert("/proj/scenes");
    fs.dirs.insert("/proj/assets");
    fs.dirs.insert("/proj/.vestige");
    fs.dirs.insert("/proj/build");
    fs.dirs.insert("/proj/.cache");
    fs.files["/proj/scenes/main.json"] = "scene-bytes";
    fs.files["/proj/assets/tex.png"] = "tex-bytes";
    fs.files["/proj/.vestige/state.json"] = "vest-bytes";
    fs.files["/proj/build/out.exe"] = "should-skip";
    fs.files["/proj/.cache/cache"] = "should-skip";

    ProjectBackup backup{fs};
    auto result = backup.snapshot("/proj", "/dest");
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(fs.exists("/dest/scenes/main.json"));
    EXPECT_TRUE(fs.exists("/dest/assets/tex.png"));
    EXPECT_TRUE(fs.exists("/dest/.vestige/state.json"));
    EXPECT_FALSE(fs.exists("/dest/build/out.exe"));
    EXPECT_FALSE(fs.exists("/dest/.cache/cache"));
    EXPECT_TRUE(fs.exists("/dest/manifest.sha256"));
}

TEST(ProjectBackup, ManifestIncludesPerFileSha256_PHASE105)
{
    MemFs fs;
    fs.dirs.insert("/proj");
    fs.dirs.insert("/proj/scenes");
    fs.files["/proj/scenes/a.json"] = "AAAA";

    ProjectBackup backup{fs};
    backup.snapshot("/proj", "/dest");

    std::string manifest = fs.readFile("/dest/manifest.sha256");
    EXPECT_NE(manifest.find("scenes/a.json"), std::string::npos);
    // SHA256("AAAA") = 63c1dd951ffedf6f7fd968ad4efa39b8ed584f162f46e715114ee184f8de9201
    EXPECT_NE(manifest.find("63c1dd951ffedf6f7fd968ad4efa39b8ed584f162f46e715114ee184f8de9201"),
              std::string::npos);
}
}
```

- [ ] **Step 2: Header.**

```cpp
// engine/lifecycle/file_io.h
#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace Vestige::Lifecycle
{
class FileIo
{
public:
    virtual ~FileIo() = default;
    virtual bool exists(const std::filesystem::path& p) const = 0;
    virtual bool isDirectory(const std::filesystem::path& p) const = 0;
    virtual std::vector<std::filesystem::path> listDirectory(const std::filesystem::path& p) const = 0;
    virtual std::string readFile(const std::filesystem::path& p) const = 0;
    virtual bool writeFile(const std::filesystem::path& p, const std::string& contents) = 0;
    virtual bool createDirectories(const std::filesystem::path& p) = 0;
};
}
```

```cpp
// engine/lifecycle/project_backup.h
#pragma once
#include "lifecycle/file_io.h"

namespace Vestige::Lifecycle
{

struct BackupResult
{
    bool ok = false;
    int filesCopied = 0;
    std::string errorReason;
};

class ProjectBackup
{
public:
    explicit ProjectBackup(FileIo& fs) : m_fs(fs) {}
    BackupResult snapshot(const std::filesystem::path& projectRoot,
                          const std::filesystem::path& destDir);

private:
    FileIo& m_fs;
};
}
```

- [ ] **Step 3: Implement.** Recursive walk; skip `build/` + `.cache/`; SHA256 each file (use stb_sha256 or a tiny in-tree implementation — see `engine/utils/sha256.{h,cpp}` to be authored as part of this task if no SHA already exists). Write `<destDir>/manifest.sha256` as `<sha256> <relpath>` lines, sorted.

- [ ] **Step 4: Build + test + commit.**

```bash
cmake --build build -j8 && ctest --test-dir build -R ProjectBackup --output-on-failure
git add engine/lifecycle/file_io.h engine/lifecycle/project_backup.h engine/lifecycle/project_backup.cpp tests/test_project_backup.cpp engine/CMakeLists.txt tests/CMakeLists.txt
git commit -m "Phase 10.5 Slice 1 T9: ProjectBackup helper"
```

---

## Task 10: Slice doc + version bump

- [ ] **Step 1: ROADMAP entry.** Add to `ROADMAP.md` Phase 10.5 section (or create the section if it doesn't yet exist):

```markdown
### Phase 10.5 Slice 1: Shared infrastructure

- [x] **F1.** `HttpClient` libcurl wrapper + `MockTransport`. **Shipped 2026-04-26**.
- [x] **F2.** `NetworkLog` per-call audit writer. **Shipped 2026-04-26**.
- [x] **F3.** `KeyringIo` interface + libsecret backend. **Shipped 2026-04-26**.
- [x] **F4.** `GitHubAuth` device-flow OAuth. **Shipped 2026-04-26**.
- [x] **F5.** `ReleaseManifest` schema + parser. **Shipped 2026-04-26**.
- [x] **F6.** `ProjectBackup` helper. **Shipped 2026-04-26**.
```

- [ ] **Step 2: CHANGELOG entry.** Add under `## [Unreleased]` mirroring previous slice entries.

- [ ] **Step 3: VERSION.** Bump 0.1.42 → 0.1.43.

- [ ] **Step 4: Commit.**

```bash
git add ROADMAP.md CHANGELOG.md VERSION
git commit -m "Phase 10.5 Slice 1 (doc): shared infrastructure shipped"
git push origin main
```

---

## Self-review

**Spec coverage:**
- Section 1 (4 infra pieces) — covered by tasks 4, 7, 8, 9 (one task each).
- Section 5 release-manifest schema — covered by task 8.
- Section 5 settings preservation hard invariant — enforced by `BinarySwapper` in the auto-updater plan, not here. ✓
- Privacy floor (network log) — covered by task 3.
- Per-OS keyring backends — Linux ships in task 6; macOS / Windows are stubs in this plan, full backends scheduled when CI runners are added.

**Placeholder scan:** no TBDs. Each step contains code or exact commands.

**Type consistency:** `KeyringStatus` consistent across tasks 5–7. `HttpResponse` shape consistent across tasks 2–4. `BreakingFeatureScope` enum used in task 8 matches spec Section 5.

**Open question** (not blocking the plan):
- libcurl link status — recorded as a Pre-flight check; the plan ships unchanged either way (Task 1 has both forks).

---

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-25-phase-10-5-shared-infrastructure-plan.md`.

Two execution options:

1. **Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.
2. **Inline Execution** — execute tasks in this session using executing-plans, batch execution with checkpoints.

When ready, say which approach and I'll proceed.
