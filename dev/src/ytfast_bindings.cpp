/*
 * ytcui-dl-wasm — ytfast_bindings.cpp
 *
 * Embind surface for the InnerTube client. Compiled with -sASYNCIFY, so every
 * method that performs network I/O returns a JS Promise. Results are returned
 * as JSON strings (parsed on the JS side) to keep the binding simple and avoid
 * deep value_object registration for nested vectors.
 */

#include <emscripten/bind.h>
#include <string>
#include <vector>

#include "ytcui-dl/ytfast_types.h"
#include "ytcui-dl/ytfast_http.h"
#include "ytcui-dl/ytfast_innertube.h"
#include "ytcui-dl/nlohmann/json.hpp"

using namespace ytfast;
using json = nlohmann::json;

namespace {

json fmt_to_json(const StreamFormat& f) {
    return json{
        {"itag", f.itag},
        {"url", f.url},
        {"stream_url", f.stream_url},
        {"mime_type", f.mime_type},
        {"quality", f.quality},
        {"quality_label", f.quality_label},
        {"width", f.width},
        {"height", f.height},
        {"fps", f.fps},
        {"bitrate", f.bitrate},
        {"average_bitrate", f.average_bitrate},
        {"content_length", f.content_length},
        {"has_video", f.has_video},
        {"has_audio", f.has_audio},
        {"audio_codec", f.audio_codec},
        {"video_codec", f.video_codec},
        {"container", f.container},
        {"is_audio_only", f.is_audio_only()},
        {"is_video_only", f.is_video_only()},
        {"is_muxed", f.is_muxed()},
    };
}

json video_to_json(const VideoInfo& v, bool with_formats) {
    json j{
        {"id", v.id},
        {"title", v.title},
        {"channel", v.channel},
        {"channel_id", v.channel_id},
        {"description", v.description},
        {"thumbnail_url", v.thumbnail_url},
        {"upload_date", v.upload_date},
        {"duration_str", v.duration_str},
        {"duration_secs", v.duration_secs},
        {"view_count", v.view_count},
        {"view_count_str", v.view_count_str},
        {"is_live", v.is_live},
        {"url", v.url},
        {"category", v.category},
    };
    j["thumbnails"] = json::array();
    for (const auto& t : v.thumbnails)
        j["thumbnails"].push_back({{"url", t.url}, {"width", t.width}, {"height", t.height}});
    if (with_formats) {
        j["formats"] = json::array();
        for (const auto& f : v.formats) j["formats"].push_back(fmt_to_json(f));
    }
    return j;
}

InnertubeClient& client() { return InnertubeClient::get_instance(); }

} // namespace

// ── Exported functions (all return JSON strings) ──────────────────────────────

// search(query, maxResults) → JSON array of result objects (no formats).
std::string wasm_search(const std::string& query, int max_results) {
    try {
        auto results = client().search(query, max_results, /*prefetch_top=*/false);
        json arr = json::array();
        for (const auto& r : results) arr.push_back(video_to_json(r, /*with_formats=*/false));
        return json{{"ok", true}, {"results", arr}}.dump();
    } catch (const std::exception& e) {
        return json{{"ok", false}, {"error", e.what()}}.dump();
    }
}

// getStreamFormats(videoId) → JSON object (full VideoInfo with formats[]).
std::string wasm_get_stream_formats(const std::string& video_id) {
    try {
        auto info = client().get_stream_formats(video_id);
        return json{{"ok", true}, {"info", video_to_json(info, /*with_formats=*/true)}}.dump();
    } catch (const std::exception& e) {
        return json{{"ok", false}, {"error", e.what()}}.dump();
    }
}

// getBestAudioUrl(videoId) → JSON {ok, url}. Muxed progressive stream (plays
// directly in an <audio>/<video> element — no MSE muxing needed).
std::string wasm_get_best_audio_url(const std::string& video_id) {
    try {
        auto info = client().get_stream_formats(video_id);
        std::string url = InnertubeClient::select_best_audio_stream(info.formats);
        return json{{"ok", true}, {"url", url}, {"title", info.title},
                    {"channel", info.channel}, {"duration", info.duration_str}}.dump();
    } catch (const std::exception& e) {
        return json{{"ok", false}, {"error", e.what()}}.dump();
    }
}

// getBestVideoUrl(videoId, maxHeight) → JSON {ok, url}. Muxed video+audio
// (progressive) so it plays in a <video> element with no extra work.
std::string wasm_get_best_video_url(const std::string& video_id, int max_h) {
    try {
        auto info = client().get_stream_formats(video_id);
        std::string url = InnertubeClient::select_best_video_stream(info.formats, max_h);
        return json{{"ok", true}, {"url", url}, {"title", info.title},
                    {"channel", info.channel}, {"duration", info.duration_str}}.dump();
    } catch (const std::exception& e) {
        return json{{"ok", false}, {"error", e.what()}}.dump();
    }
}

EMSCRIPTEN_BINDINGS(ytfast_module) {
    emscripten::function("search",             &wasm_search);
    emscripten::function("getStreamFormats",   &wasm_get_stream_formats);
    emscripten::function("getBestAudioUrl",    &wasm_get_best_audio_url);
    emscripten::function("getBestVideoUrl",    &wasm_get_best_video_url);
}
