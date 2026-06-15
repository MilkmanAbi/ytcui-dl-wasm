#pragma once
/*
 * ytfast_http.h — WASM / Emscripten build
 *
 * Replaces the libcurl implementation with emscripten_fetch.
 * Must be compiled on a pthread worker (EMSCRIPTEN_FETCH_SYNCHRONOUS works
 * only off the main thread). The outer JS glue runs the WASM module on a
 * Worker, so this is fine.
 *
 * Browser CORS note:
 *   YouTube's InnerTube API does NOT send CORS headers to arbitrary Origins.
 *   Direct browser→YouTube calls will be blocked unless you route through a
 *   CORS proxy.  The default proxy below is configurable at compile time via
 *   -DYTFAST_CORS_PROXY="https://your.proxy/?url=" — set to "" to disable.
 */

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <emscripten/fetch.h>

#ifndef YTFAST_CORS_PROXY
// Simple passthrough proxy — replace with your own.
// Empty string = no proxy (direct, will fail for YouTube from a browser).
#define YTFAST_CORS_PROXY "https://corsproxy.io/?"
#endif

namespace ytfast {

class HttpClient {
public:
    struct Response {
        long        status = 0;
        std::string body;
        std::string content_type;
    };

    HttpClient()  = default;
    ~HttpClient() = default;
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    Response post(const std::string& url,
                  const std::string& json_body,
                  const std::vector<std::string>& extra_headers = {}) {
        return do_fetch("POST", url, json_body, extra_headers);
    }

    Response get(const std::string& url,
                 const std::vector<std::string>& = {}) {
        return do_fetch("GET", url, "", {});
    }

    // emscripten_fetch has no encode helper; provide a trivial one.
    static std::string url_encode(const std::string& s) {
        std::string out;
        for (unsigned char c : s) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out += (char)c;
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", c);
                out += buf;
            }
        }
        return out;
    }

private:
    static std::string proxy_url(const std::string& url) {
        std::string proxy = YTFAST_CORS_PROXY;
        if (proxy.empty()) return url;
        return proxy + url_encode(url);
    }

    Response do_fetch(const char* method,
                      const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& /*extra_hdrs*/) {
        emscripten_fetch_attr_t attr;
        emscripten_fetch_attr_init(&attr);
        strncpy(attr.requestMethod, method, sizeof(attr.requestMethod) - 1);
        attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY | EMSCRIPTEN_FETCH_SYNCHRONOUS;

        // Headers: Content-Type + Accept-Language
        const char* headers[] = {
            "Content-Type",    "application/json",
            "Accept-Language", "en-US,en;q=0.9",
            nullptr, nullptr
        };
        attr.requestHeaders = headers;

        if (!body.empty()) {
            attr.requestData     = body.data();
            attr.requestDataSize = body.size();
        }

        std::string target = proxy_url(url);
        emscripten_fetch_t* fetch = emscripten_fetch(&attr, target.c_str());
        if (!fetch)
            throw std::runtime_error("emscripten_fetch() returned null for: " + url);

        Response resp;
        resp.status = (long)fetch->status;
        resp.body.assign(fetch->data, fetch->data + fetch->numBytes);

        emscripten_fetch_close(fetch);

        if (resp.status == 0 || resp.status >= 500)
            throw std::runtime_error("HTTP " + std::to_string(resp.status) + " for: " + url);

        return resp;
    }
};

// No-op shim — emscripten_fetch manages its own setup.
struct CurlGlobalInit {};

// Per-"thread" client. In the WASM build there's one worker; thread_local is
// fine even without full pthread thread-locals since we only call from one worker.
inline HttpClient& get_thread_http() {
    thread_local HttpClient tl_http;
    return tl_http;
}

} // namespace ytfast
