// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file http_transport.h
/// @brief The one seam through which the self-updater reaches the network
///        (3D_E-0729, spec §4.5, INV-4). Tests pass a fake.
#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace Vestige::Update
{

/// @brief Outcome of one GET.
struct HttpResult
{
    int status = 0;     ///< HTTP status; 0 when no response arrived.
    std::string body;   ///< At most the caller's maxBytes.
    std::string error;  ///< Empty on a completed transfer.

    bool ok() const { return error.empty() && status == 200; }
};

/// @brief Called as bytes arrive: (received, total or 0 if unknown).
///        Returning false cancels the transfer.
using ProgressFn = std::function<bool(std::size_t received, std::size_t total)>;

/// @brief HTTPS GET, nothing else.
class IHttpTransport
{
public:
    virtual ~IHttpTransport() = default;

    /// @brief GET @a url. A body longer than @a maxBytes fails the transfer.
    virtual HttpResult get(const std::string& url, std::size_t maxBytes,
                           const ProgressFn& progress) = 0;
};

/// @brief libcurl implementation: https only (redirects included), TLS 1.2+,
///        15 s connect timeout, aborts below 1 KB/s for 30 s.
class CurlTransport final : public IHttpTransport
{
public:
    /// @param userAgent Sent on every request; GitHub's API requires one.
    explicit CurlTransport(std::string userAgent);

    HttpResult get(const std::string& url, std::size_t maxBytes,
                   const ProgressFn& progress) override;

private:
    std::string m_userAgent;
};

}  // namespace Vestige::Update
