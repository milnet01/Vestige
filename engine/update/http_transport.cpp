// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file http_transport.cpp
/// @brief The only file in the engine that includes libcurl (INV-4).
#include "update/http_transport.h"

#include <curl/curl.h>

#include <filesystem>
#include <mutex>

namespace Vestige::Update
{

namespace
{

struct Transfer
{
    std::string body;
    std::size_t maxBytes = 0;
    bool overCap = false;
    const ProgressFn* progress = nullptr;
};

size_t onData(char* data, size_t size, size_t count, void* user)
{
    auto* t = static_cast<Transfer*>(user);
    const size_t n = size * count;
    if (t->body.size() + n > t->maxBytes)
    {
        t->overCap = true;
        return 0;  // makes curl fail the transfer with CURLE_WRITE_ERROR
    }
    t->body.append(data, n);
    return n;
}

int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t)
{
    auto* t = static_cast<Transfer*>(user);
    if (t->progress && *t->progress)
    {
        const bool keepGoing = (*t->progress)(static_cast<std::size_t>(now),
                                              static_cast<std::size_t>(total));
        return keepGoing ? 0 : 1;  // non-zero aborts
    }
    return 0;
}

/// An AppImage built on Ubuntu runs on distros that keep the CA bundle
/// elsewhere, so the bundle curl was compiled against may not exist. Use the
/// first one this machine has.
const char* linuxCaBundle()
{
#ifdef __linux__
    static const char* const kCandidates[] = {
        "/etc/ssl/certs/ca-certificates.crt",  // Debian, Ubuntu, Arch
        "/etc/pki/tls/certs/ca-bundle.crt",    // Fedora, RHEL
        "/etc/ssl/ca-bundle.pem",              // openSUSE
        "/etc/ssl/cert.pem",                   // Alpine
    };
    for (const char* path : kCandidates)
    {
        std::error_code ec;
        if (std::filesystem::is_regular_file(path, ec))
        {
            return path;
        }
    }
#endif
    return nullptr;
}

void globalInitOnce()
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

}  // namespace

CurlTransport::CurlTransport(std::string userAgent) : m_userAgent(std::move(userAgent)) {}

HttpResult CurlTransport::get(const std::string& url, std::size_t maxBytes,
                              const ProgressFn& progress)
{
    HttpResult result;
    if (url.rfind("https://", 0) != 0)
    {
        result.error = "refused a non-https URL";
        return result;
    }
    globalInitOnce();
    CURL* curl = curl_easy_init();
    if (!curl)
    {
        result.error = "could not start a transfer";
        return result;
    }

    Transfer transfer;
    transfer.maxBytes = maxBytes;
    transfer.progress = &progress;

    curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json, */*");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, m_userAgent.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // safe off the main thread
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &transfer);
    if (const char* ca = linuxCaBundle())
    {
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    }

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    result.status = static_cast<int>(status);
    if (transfer.overCap)
    {
        result.error = "the download is larger than allowed";
    }
    else if (code == CURLE_ABORTED_BY_CALLBACK)
    {
        result.error = "cancelled";
    }
    else if (code != CURLE_OK)
    {
        result.error = curl_easy_strerror(code);
    }
    else
    {
        result.body = std::move(transfer.body);
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

}  // namespace Vestige::Update
