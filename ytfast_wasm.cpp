/*
 * ytfast_wasm.cpp
 *
 * Emscripten/Embind glue that exposes ytcui-dl's InnerTube client to
 * JavaScript. Compiled to ytfast.js + ytfast.wasm (+ ytfast.worker.js when
 * using -pthread).
 *
 * JS usage (after Module is ready):
 *
 *   const yt = new Module.YtFast();
 *
 *   // Search
 *   const results = yt.search("lofi hip hop", 15);
 *   for (let i = 0; i < results.size(); i++) {
 *       const v = results.get(i);
 *       console.log(v.id, v.title, v.channel, v.thumbnail_url, v.duration_str);
 *   }
 *
 *   // Stream URLs for a video (returns VideoInfo with formats[])
 *   const info = yt.getStreamFormats("dQw4w9WgXcQ");
 *   const audioUrl  = yt.getBestAudioUrl("dQw4w9WgXcQ");   // for <audio src=…>
 *   const videoUrl  = yt.getBestVideoUrl("dQw4w9WgXcQ", 720); // for <video src=…>
 *
 *   // Individual format inspection
 *   for (let i = 0; i < info.formats.size(); i++) {
 *       const f = info.formats.get(i);
 *       console.log(f.itag, f.quality_label, f.container, f.stream_url);
 *   }
 *
 * CORS note:
 *   YouTube InnerTube does not allow browser cross-origin requests. All calls
 *   go through a CORS proxy. Pass your own at build time with
 *   -DYTFAST_CORS_PROXY="https://your.proxy/?url=" or set corsProxy on the
 *   Module before calling anything:
 *
 *     Module.corsProxy = "https://corsproxy.io/?";
 *
 *   The proxy URL is prepended to the percent-encoded target URL.
 */

#include <emscripten/bind.h>
#include "ytfast_innertube.h"

using namespace emscripten;
using namespace ytfast;

// ── Thin wrapper class exposed to JS ─────────────────────────────────────────
// We wrap the singleton-pattern InnertubeClient with a value-type facade so
// Embind can bind it as a class rather than a raw singleton reference.
struct YtFast {
    InnertubeClient& client() { return InnertubeClient::get_instance(); }

    std::vector<SearchResult> search(const std::string& query, int max = 20) {
        return client().search(query, max);
    }

    VideoInfo getStreamFormats(const std::string& video_id) {
        return client().get_stream_formats(video_id);
    }

    std::string getBestAudioUrl(const std::string& vid) {
        return client().get_best_audio_stream_url(vid);
    }

    std::string getBestVideoUrl(const std::string& vid, int max_height = 1080) {
        return client().get_best_video_stream_url(vid, max_height);
    }

    // Convenience: title + thumbnail for a video ID without full format parsing.
    VideoInfo getVideoInfo(const std::string& vid) {
        auto info = getStreamFormats(vid);
        // Clear the large formats vector so JS only pays to parse what it asked for.
        info.formats.clear();
        return info;
    }
};

// ── Embind registrations ──────────────────────────────────────────────────────
EMSCRIPTEN_BINDINGS(ytfast_module) {

    // StreamFormat
    class_<StreamFormat>("StreamFormat")
        .property("itag",          &StreamFormat::itag)
        .property("url",           &StreamFormat::url)
        .property("stream_url",    &StreamFormat::stream_url)
        .property("mime_type",     &StreamFormat::mime_type)
        .property("quality",       &StreamFormat::quality)
        .property("quality_label", &StreamFormat::quality_label)
        .property("width",         &StreamFormat::width)
        .property("height",        &StreamFormat::height)
        .property("fps",           &StreamFormat::fps)
        .property("bitrate",       &StreamFormat::bitrate)
        .property("has_video",     &StreamFormat::has_video)
        .property("has_audio",     &StreamFormat::has_audio)
        .property("audio_codec",   &StreamFormat::audio_codec)
        .property("video_codec",   &StreamFormat::video_codec)
        .property("container",     &StreamFormat::container)
        .function("isAudioOnly",   &StreamFormat::is_audio_only)
        .function("isVideoOnly",   &StreamFormat::is_video_only)
        .function("isMuxed",       &StreamFormat::is_muxed)
        ;

    register_vector<StreamFormat>("VectorStreamFormat");

    // ThumbnailInfo
    class_<ThumbnailInfo>("ThumbnailInfo")
        .property("url",    &ThumbnailInfo::url)
        .property("width",  &ThumbnailInfo::width)
        .property("height", &ThumbnailInfo::height)
        ;

    register_vector<ThumbnailInfo>("VectorThumbnailInfo");

    // VideoInfo / SearchResult (same struct)
    class_<VideoInfo>("VideoInfo")
        .property("id",            &VideoInfo::id)
        .property("title",         &VideoInfo::title)
        .property("channel",       &VideoInfo::channel)
        .property("channel_id",    &VideoInfo::channel_id)
        .property("description",   &VideoInfo::description)
        .property("thumbnail_url", &VideoInfo::thumbnail_url)
        .property("thumbnails",    &VideoInfo::thumbnails)
        .property("upload_date",   &VideoInfo::upload_date)
        .property("duration_str",  &VideoInfo::duration_str)
        .property("duration_secs", &VideoInfo::duration_secs)
        .property("view_count",    &VideoInfo::view_count)
        .property("view_count_str",&VideoInfo::view_count_str)
        .property("is_live",       &VideoInfo::is_live)
        .property("url",           &VideoInfo::url)
        .property("formats",       &VideoInfo::formats)
        ;

    register_vector<VideoInfo>("VectorVideoInfo");

    // Main API class
    class_<YtFast>("YtFast")
        .constructor()
        .function("search",           &YtFast::search)
        .function("getStreamFormats", &YtFast::getStreamFormats)
        .function("getBestAudioUrl",  &YtFast::getBestAudioUrl)
        .function("getBestVideoUrl",  &YtFast::getBestVideoUrl)
        .function("getVideoInfo",     &YtFast::getVideoInfo)
        ;
}
