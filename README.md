# ytcui-dl-wasm

The native ytcui-dl InnerTube client (the same C++ that powers ytcui) compiled
to WebAssembly. It runs YouTube search and stream-URL resolution entirely in the
browser via Embind - no yt-dlp, no server-side YouTube logic.

This is a real compiled artifact: `web/ytfast.wasm` is built from the vendored
`ytcui-dl` headers with emscripten, and has been verified against live YouTube
(search returns real results; resolution returns real googlevideo URLs).

## What works, and the one hard constraint

- Search and stream resolution: working and verified.
- Playback: works from a normal (residential) browser, but is subject to
  YouTube's CDN restrictions - see "The CORS / IP reality" below. It is not
  something this library can guarantee, because YouTube actively gates it.

## Layout

```
include/ytcui-dl/   vendored client (ytfast_innertube.h, ytfast_types.h, json)
                    + ytfast_http.h  -> WASM fetch transport (replaces libcurl)
src/ytfast_bindings.cpp   Embind surface (search / getStreamFormats / ...)
web/                static site: index.html + ytfast.js + ytfast.wasm + .nojekyll
api/proxy.js        Vercel serverless CORS proxy
vercel.json         Vercel config (outputDirectory: web)
build.sh            the emcc command, documented
```

## Quickstart (local)

The page is an ES module loading a `.wasm`, so it needs to be served over HTTP
(not `file://`). Any static server works, but search will fail without a proxy
(see below). The simplest fully-working local setup is the Vercel CLI, which
also runs `api/proxy.js`:

```
npm i -g vercel
vercel dev
```

Then open the printed URL and search. The page auto-detects `/api/proxy`.

## Deploy: Vercel (recommended, zero-config)

```
vercel            # or: connect the repo at vercel.com
```

`vercel.json` serves `web/` as static files and `api/proxy.js` as a serverless
function. The page auto-detects the proxy at `/api/proxy`. Nothing to configure.

## Deploy: GitHub Pages

GitHub Pages is static-only - it cannot run `api/proxy.js`. So:

1. Publish the `web/` folder (Pages > Build and deployment > from a branch >
   `/web`, or copy `web/*` to the publish root). The included `.nojekyll`
   stops Jekyll from dropping the `.wasm`.
2. Deploy the proxy separately (the included Vercel function is easiest), then
   open the page, expand "Proxy settings", and paste your proxy base, e.g.
   `https://your-app.vercel.app/api/proxy?url=`. It is saved to localStorage.

Any CORS proxy works, not just the bundled one. The base may use a `{url}`
placeholder or end in `?url=` (the target is appended, URL-encoded).

## The CORS / IP reality (why a proxy is required)

This is the part that makes a naive browser port "not work", so it is worth
being precise:

- YouTube's InnerTube API sends no `Access-Control-Allow-Origin`. A browser
  therefore cannot call it directly - search and resolution must go through a
  proxy. That is what `api/proxy.js` is for, and why GitHub Pages alone is not
  enough.
- googlevideo media URLs are IP-aware. Requests from datacenter IPs (most cloud
  hosts, including Vercel's functions) frequently get HTTP 403. Requests from a
  normal residential browser usually succeed, and a successful googlevideo media
  response does carry CORS headers.

The consequence drives the playback design:

- Search / resolve -> always through the proxy (InnerTube has no CORS).
- Playback -> the page points the `<audio>` element at the googlevideo URL
  directly, so the fetch happens on the user's own IP (not a datacenter), with a
  proxy fallback if the direct fetch fails. Routing media through a datacenter
  proxy is the thing most likely to 403, so it is the fallback, not the default.

## JavaScript API

`createYtFast()` resolves to a module. Set the proxy, then call the functions -
each returns a JSON string (parse it), and because the module is built with
ASYNCIFY, each returns a Promise.

```js
import createYtFast from "./ytfast.js";
const Module = await createYtFast();
Module.YTFAST_PROXY = "/api/proxy?url=";          // or your proxy base

const { results } = JSON.parse(await Module.search("ive xoxz", 15));
// results: [{ id, title, channel, duration_str, thumbnail_url, is_live, ... }]

const a = JSON.parse(await Module.getBestAudioUrl(results[0].id));
// a: { ok, url, title, channel, duration }   - muxed progressive stream

const info = JSON.parse(await Module.getStreamFormats(results[0].id));
// info.info.formats: [{ itag, url, mime_type, quality_label, has_video, ... }]

const v = JSON.parse(await Module.getBestVideoUrl(results[0].id, 1080));
```

| Function | Returns (parsed) |
| --- | --- |
| `search(query, maxResults)` | `{ ok, results[] }` (no formats) |
| `getStreamFormats(videoId)` | `{ ok, info }` (full VideoInfo + formats) |
| `getBestAudioUrl(videoId)` | `{ ok, url, title, channel, duration }` |
| `getBestVideoUrl(videoId, maxHeight)` | `{ ok, url, title, channel, duration }` |

On failure each returns `{ ok: false, error }` rather than throwing.

## Rebuilding

Install the Emscripten SDK, then run `./build.sh`. The build flags and the
reason each one is needed are documented in that script - in particular
`-sASYNCIFY` (sync C++ over async fetch) and `-fexceptions`
`-sDISABLE_EXCEPTION_CATCHING=0` (without them a C++ `throw` aborts the whole
module instead of being caught and returned as `{ ok: false }`).

## Notes

- Single-threaded by design: the native client's background prefetch thread is
  compiled out under `__EMSCRIPTEN__`, because pthreads in the browser need
  SharedArrayBuffer with COOP/COEP headers that static hosts do not send. Search
  and resolution do not depend on it.
- The proxy whitelists only youtube.com / googlevideo / ytimg hosts so it cannot
  be used as a general open proxy.
