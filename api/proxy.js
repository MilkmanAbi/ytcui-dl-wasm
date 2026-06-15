// api/proxy.js — Vercel serverless function (Node runtime).
//
// Browsers cannot call YouTube's InnerTube endpoints (or googlevideo media)
// directly: those responses carry no Access-Control-Allow-Origin header, so the
// fetch is blocked by CORS. This function runs server-side (no CORS rules
// apply), forwards the request, and returns the response with permissive CORS
// headers so the WASM module — and the <audio> element — can read it.
//
// Two kinds of traffic:
//   1. InnerTube API   — POST youtube.com/youtubei/...  -> JSON, buffered.
//   2. Media streaming — GET  *.googlevideo.com/...     -> binary, with Range
//                                                          passthrough for seek.
//
// Called as:  /api/proxy?url=<url-encoded target>

export const config = { api: { responseLimit: false } };

export default async function handler(req, res) {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Accept-Language, Range');
  res.setHeader('Access-Control-Expose-Headers', 'Content-Length, Content-Range, Accept-Ranges');
  if (req.method === 'OPTIONS') { res.status(204).end(); return; }

  const target = req.query.url;
  if (!target || typeof target !== 'string') {
    res.status(400).json({ error: 'missing ?url= parameter' });
    return;
  }

  // Restrict to YouTube / Google video hosts so this can't be abused as an
  // open proxy.
  let host;
  try { host = new URL(target).hostname; }
  catch { res.status(400).json({ error: 'invalid url' }); return; }
  const allowed = /(^|\.)(youtube\.com|youtubei\.googleapis\.com|googlevideo\.com|ytimg\.com|youtube-nocookie\.com)$/;
  if (!allowed.test(host)) {
    res.status(403).json({ error: 'host not allowed: ' + host });
    return;
  }

  // Read raw body for POST (InnerTube context).
  let body;
  if (req.method === 'POST') {
    body = await new Promise((resolve) => {
      let d = ''; req.on('data', (c) => (d += c)); req.on('end', () => resolve(d));
    });
  }

  const isMedia = /(^|\.)googlevideo\.com$/.test(host);
  const headers = {};
  if (isMedia) {
    // googlevideo CDN rejects requests carrying an Origin/X-Origin header with
    // 403. Send only a browser User-Agent and pass the Range through.
    headers['User-Agent'] =
      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' +
      '(KHTML, like Gecko) Chrome/120.0 Safari/537.36';
  } else {
    headers['Accept-Language'] = 'en-US,en;q=0.9';
    headers['Origin']   = 'https://www.youtube.com';
    headers['X-Origin'] = 'https://www.youtube.com';
    headers['User-Agent'] =
      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' +
      '(KHTML, like Gecko) Chrome/120.0 Safari/537.36';
  }
  if (req.method === 'POST') headers['Content-Type'] = 'application/json';
  // Forward Range so seeking / chunked media playback works.
  if (req.headers['range']) headers['Range'] = req.headers['range'];

  try {
    const upstream = await fetch(target, { method: req.method, headers, body });

    res.status(upstream.status);
    // Pass through headers that matter — but NOT content-length or
    // content-encoding: fetch() auto-decompresses gzip, so the upstream
    // content-length (compressed size) no longer matches the bytes we send and
    // would truncate the body. Node sets the correct length from the buffer.
    for (const h of ['content-type', 'content-range', 'accept-ranges', 'cache-control']) {
      const v = upstream.headers.get(h);
      if (v) res.setHeader(h, v);
    }

    // Stream the body through as binary (works for both JSON and media).
    const buf = Buffer.from(await upstream.arrayBuffer());
    res.send(buf);
  } catch (e) {
    res.status(502).json({ error: 'upstream fetch failed: ' + String(e && e.message ? e.message : e) });
  }
}
