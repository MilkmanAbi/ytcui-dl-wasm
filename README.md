# ytcui-dl-wasm

The native ytcui-dl InnerTube client (the same C++ that powers ytcui) compiled
to WebAssembly. It runs YouTube search and stream-URL resolution entirely in the
browser via Embind - no yt-dlp, no server-side YouTube logic.

This is a real compiled artifact: `ytfast.wasm` is built from the vendored
`ytcui-dl` headers with emscripten, and has been verified against live YouTube
(search returns real results; resolution returns real googlevideo URLs).

## What works, and the one hard constraint

- Search and stream resolution: working and verified.
- Playback: works from a normal (residential) browser, but is subject to
  YouTube's CDN restrictions - see "The CORS / IP reality" below. It is not
  something this library can guarantee, because YouTube actively gates it.

## Layout

The site files sit at the repo ROOT so GitHub Pages can serve them directly.

```
index.html          the demo UI
ytfast.js           emscripten module loader (ES module)
ytfast.wasm         the compiled InnerTube client
.nojekyll           stops GitHub Pages' Jekyll from dropping the .wasm
vercel.json         Vercel config (serves root statically + api/ functions)
api/proxy.js        Vercel serverless CORS proxy
README.md
dev/                everything needed to rebuild - not served
  build.sh          the emcc command, documented
  include/ytcui-dl/ vendored client + ytfast_http.h (WASM fetch transport)
  src/ytfast_bindings.cpp
.github/workflows/pages.yml   optional Actions deploy
```

## Deploy: GitHub Pages

Because the site files are at the root, you have two equally good options:

1. Settings > Pages > Source > "Deploy from a branch" > main > `/ (root)`.
   The `.nojekyll` file keeps the `.wasm` from being stripped. Done.
2. Or Settings > Pages > Source > "GitHub Actions". The included
   `.github/workflows/pages.yml` publishes the root on every push to `main`
   (change the branch name there if yours differs). The Actions tab shows the
   build running.

Either way the site lands at `https://<you>.github.io/<repo>/`. `index.html`
uses relative paths (`./ytfast.js`), so the repo subpath is fine.

GitHub Pages is static-only - it cannot run `api/proxy.js`. So you still need a
proxy (see below): deploy the proxy separately, open the page, expand "Proxy
settings", and paste your proxy base, e.g.
`https://your-app.vercel.app/api/proxy?url=`. It is saved to localStorage.

## Deploy: Vercel (runs the proxy too)

```
vercel            # or connect the repo at vercel.com
```

`vercel.json` serves the root statically and `api/proxy.js` as a serverless
function. The page auto-detects the proxy at `/api/proxy`, so nothing to
configure. `vercel dev` runs the same thing locally on your own IP.

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
const a = JSON.parse(await Module.getBestAudioUrl(results[0].id));
const info = JSON.parse(await Module.getStreamFormats(results[0].id));
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

Install the Emscripten SDK, then:

```
cd dev && ./build.sh
```

It writes `ytfast.js` + `ytfast.wasm` to the repo root. The flags and the reason
each is needed are documented in that script - in particular `-sASYNCIFY` (sync
C++ over async fetch) and `-fexceptions` `-sDISABLE_EXCEPTION_CATCHING=0`
(without them a C++ `throw` aborts the whole module instead of being caught and
returned as `{ ok: false }`).

## Notes

- Single-threaded by design: the native client's background prefetch thread is
  compiled out under `__EMSCRIPTEN__`, because pthreads in the browser need
  SharedArrayBuffer with COOP/COEP headers that static hosts do not send. Search
  and resolution do not depend on it.
- The proxy whitelists only youtube.com / googlevideo / ytimg hosts so it cannot
  be used as a general open proxy.
