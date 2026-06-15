/**
 * ytfast-client.js
 *
 * Async JavaScript wrapper around the ytcui-dl WASM module.
 * Hides the Embind vector<>.get(i)/size() API behind plain JS arrays and
 * objects so the rest of your app never has to know WASM is involved.
 *
 * Usage:
 *   import { YtFastClient } from './ytfast-client.js';
 *
 *   const yt = new YtFastClient('./ytfast.js');
 *   await yt.ready;
 *
 *   // Search
 *   const videos = await yt.search("lofi hip hop", 15);
 *   // videos → Array of plain objects: { id, title, channel, thumbnail_url, ... }
 *
 *   // Stream URL for a video
 *   const audioUrl = await yt.getBestAudioUrl("dQw4w9WgXcQ");
 *   const videoUrl = await yt.getBestVideoUrl("dQw4w9WgXcQ", 720);
 *
 *   // Full format list
 *   const info = await yt.getStreamFormats("dQw4w9WgXcQ");
 *   // info.formats → Array of plain objects with all StreamFormat fields
 *
 * CORS proxy:
 *   By default the WASM module uses corsproxy.io (baked in at compile time).
 *   If you want to override it at runtime, call:
 *     yt.setCorsProxy("https://myproxy.example.com/?url=");
 *   before any API call (it replaces the proxy for the next request only when
 *   the build-time proxy is set to "" — see Makefile).
 */

export class YtFastClient {
    #wasmPath;
    #module = null;
    #client = null;
    ready;

    constructor(wasmJsPath = './dist/ytfast.js') {
        this.#wasmPath = wasmJsPath;
        this.ready = this.#init();
    }

    async #init() {
        // Dynamic import of the Emscripten-generated ES6 module factory
        const factory = (await import(this.#wasmPath)).default;
        this.#module = await factory();
        this.#client = new this.#module.YtFast();
    }

    /** Search YouTube. Returns an array of plain VideoInfo objects. */
    async search(query, maxResults = 20) {
        await this.ready;
        const vec = this.#client.search(query, maxResults);
        return this.#vecToArray(vec, v => this.#videoInfoToObj(v));
    }

    /** Get all stream formats for a video ID. Returns a plain VideoInfo object. */
    async getStreamFormats(videoId) {
        await this.ready;
        const info = this.#client.getStreamFormats(videoId);
        return this.#videoInfoToObj(info);
    }

    /** Best audio-only stream URL (for <audio src=…> or MediaSource). */
    async getBestAudioUrl(videoId) {
        await this.ready;
        return this.#client.getBestAudioUrl(videoId);
    }

    /**
     * Best video stream URL up to maxHeight pixels tall.
     * Note: YouTube adaptive streams are video-only; mux with audio separately
     * if you need both in a <video> element via MediaSource Extensions.
     */
    async getBestVideoUrl(videoId, maxHeight = 1080) {
        await this.ready;
        return this.#client.getBestVideoUrl(videoId, maxHeight);
    }

    /** Lightweight metadata fetch (no format parsing, faster). */
    async getVideoInfo(videoId) {
        await this.ready;
        const info = this.#client.getVideoInfo(videoId);
        return this.#videoInfoToObj(info);
    }

    // ── Private helpers ────────────────────────────────────────────────────────

    #vecToArray(vec, mapper) {
        const arr = [];
        for (let i = 0; i < vec.size(); i++) arr.push(mapper(vec.get(i)));
        return arr;
    }

    #videoInfoToObj(v) {
        return {
            id:            v.id,
            title:         v.title,
            channel:       v.channel,
            channel_id:    v.channel_id,
            description:   v.description,
            thumbnail_url: v.thumbnail_url,
            thumbnails:    this.#vecToArray(v.thumbnails, t => ({
                               url: t.url, width: t.width, height: t.height
                           })),
            upload_date:   v.upload_date,
            duration_str:  v.duration_str,
            duration_secs: v.duration_secs,
            view_count:    v.view_count,
            view_count_str:v.view_count_str,
            is_live:       v.is_live,
            url:           v.url,
            formats:       this.#vecToArray(v.formats, f => ({
                itag:          f.itag,
                url:           f.url,
                stream_url:    f.stream_url,
                mime_type:     f.mime_type,
                quality:       f.quality,
                quality_label: f.quality_label,
                width:         f.width,
                height:        f.height,
                fps:           f.fps,
                bitrate:       f.bitrate,
                has_video:     f.has_video,
                has_audio:     f.has_audio,
                audio_codec:   f.audio_codec,
                video_codec:   f.video_codec,
                container:     f.container,
                is_audio_only: f.isAudioOnly(),
                is_video_only: f.isVideoOnly(),
                is_muxed:      f.isMuxed(),
            })),
        };
    }
}
