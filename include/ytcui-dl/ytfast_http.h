#pragma once
/*
 * ytcui-dl — ytfast_http.h  (WebAssembly / emscripten build)
 *
 * Drop-in replacement for the libcurl HttpClient. Same interface
 * (post / get / url_encode / Response), but the actual transport is the
 * browser's fetch() — reached synchronously from C++ via EM_ASYNC_JS, which
 * requires the module to be linked with -sASYNCIFY. That lets the existing
 * synchronous InnerTube code run unchanged: each http call suspends the C++
 * stack, awaits the JS fetch, then resumes.
 *
 * CORS: YouTube"s InnerTube endpoints do NOT send Access-Control-Allow-Origin,
 * so a browser cannot call them directly. Every request is therefore routed
 * through a proxy whose base URL is read at runtime from Module.YTFAST_PROXY:
 *
 *   - ""                       → no proxy (only works for already-CORS-OK URLs)
 *   - "https://host/api?url="  → target is appended, URL-encoded
 *   - "https://host/{url}"     → {url} placeholder is replaced, URL-encoded
 *
 * The bundled Vercel function (api/proxy.js) implements the "?url=" style and
 * forwards server-side (no CORS limits, and it can set Origin etc.).
 */

#include <string>
#include <vector>
#include <stdexcept>
#include <emscripten.h>
#include "ytcui-dl/nlohmann/json.hpp"

namespace ytfast {

// Performs one fetch on the JS side and returns a malloc"d UTF-8 C string
// containing a JSON object: {"status":N,"body":"...","ct":"...","error":"..."}.
// The C++ side owns the returned pointer and must free() it.
EM_ASYNC_JS(char*, ytfast_js_fetch,
            (const char* method_c, const char* url_c,
             const char* body_c,  const char* headers_json_c), {
    const method  = UTF8ToString(method_c);
    const url     = UTF8ToString(url_c);
    const body    = UTF8ToString(body_c);
    let headers   = {};
    try { headers = JSON.parse(UTF8ToString(headers_json_c) || "{}"); } catch (e) {}

    // Resolve proxy base (set from JS before use).
    const proxy = (typeof Module !== "undefined" && Module.YTFAST_PROXY) ? Module.YTFAST_PROXY : "";
    let target;
    if (!proxy)                      target = url;
    else if (proxy.indexOf("{url}") >= 0) target = proxy.replace("{url}", encodeURIComponent(url));
    else                             target = proxy + encodeURIComponent(url);

    const pack = (obj) => {
        const s = JSON.stringify(obj);
        const len = lengthBytesUTF8(s) + 1;
        const ptr = _malloc(len);
        stringToUTF8(s, ptr, len);
        return ptr;
    };

    try {
        const init = { method: method, headers: headers };
        if (method === "POST") init.body = body;
        const resp = await fetch(target, init);
        const text = await resp.text();
        return pack({ status: resp.status, body: text,
                      ct: resp.headers.get("content-type") || "" });
    } catch (e) {
        return pack({ status: 0, body: "", ct: "", error: String(e && e.message ? e.message : e) });
    }
});

class HttpClient {
public:
    struct Response {
        long        status = 0;
        std::string body;
        std::string content_type;
    };

    HttpClient() = default;
    ~HttpClient() = default;
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    Response post(const std::string& url,
                  const std::string& json_body,
                  const std::vector<std::string>& extra_headers = {}) {
        return do_fetch("POST", url, json_body, extra_headers);
    }

    Response get(const std::string& url,
                 const std::vector<std::string>& extra_headers = {}) {
        return do_fetch("GET", url, "", extra_headers);
    }

    // Pure-C++ percent-encoding (RFC 3986 unreserved set kept literal).
    static std::string url_encode(const std::string& s) {
        static const char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s) {
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                c == '.' || c == '~') {
                out.push_back((char)c);
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }

private:
    Response do_fetch(const char* method, const std::string& url,
                      const std::string& body,
                      const std::vector<std::string>& extra_headers) {
        // Build a JSON header object. The browser silently drops "forbidden"
        // headers (Origin, User-Agent, ...) — that's fine, the proxy supplies
        // them server-side; the InnerTube client context lives in the body.
        nlohmann::json hdrs;
        hdrs["Content-Type"]    = "application/json";
        hdrs["Accept-Language"] = "en-US,en;q=0.9";
        for (const auto& h : extra_headers) {
            auto colon = h.find(':');
            if (colon == std::string::npos) continue;
            std::string key = trim(h.substr(0, colon));
            std::string val = trim(h.substr(colon + 1));
            if (!key.empty()) hdrs[key] = val;
        }

        char* raw = ytfast_js_fetch(method, url.c_str(), body.c_str(), hdrs.dump().c_str());
        if (!raw) throw std::runtime_error("fetch: no response");
        std::string j(raw);
        free(raw);

        Response resp;
        try {
            auto parsed = nlohmann::json::parse(j);
            resp.status       = parsed.value("status", 0);
            resp.body         = parsed.value("body", std::string());
            resp.content_type = parsed.value("ct", std::string());
            std::string err   = parsed.value("error", std::string());
            if (resp.status == 0 && !err.empty())
                throw std::runtime_error("fetch failed: " + err +
                    " (CORS proxy unreachable? set a working Module.YTFAST_PROXY)");
        } catch (const nlohmann::json::exception&) {
            throw std::runtime_error("fetch: malformed bridge response");
        }
        return resp;
    }

    static std::string trim(const std::string& s) {
        size_t a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t");
        return s.substr(a, b - a + 1);
    }
};

// No global init needed in the browser (no curl_global_init). Single-threaded,
// so the "thread-local" factory just hands back one shared client.
inline HttpClient& get_thread_http() {
    static HttpClient http;
    return http;
}

} // namespace ytfast
