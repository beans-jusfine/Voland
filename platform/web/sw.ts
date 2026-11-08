/**
 * Service Worker (docs/DESIGN.md section 16). Two jobs:
 *
 * 1. Re-inject COOP/COEP/CSP on every served response. Real deployments
 *    should set these at the server/hosting-config level; the SW path is
 *    the fallback for self-hosters who can't - and requires one reload on
 *    first visit before it can control the page (handled in main.ts).
 *  2. Cache strategy: app shell network-first with an offline fallback,
 *     .wasm cache-first, /api/ network-only, everything else network-first.
 */

const sw: ServiceWorkerGlobalScope = globalThis as unknown as ServiceWorkerGlobalScope;

const SHELL_CACHE = "voland-shell-v2";
const WASM_CACHE = "voland-wasm-v3";
const BASE_URL = new URL(".", sw.registration.scope);
const BASE_PATH = BASE_URL.pathname;
const SHELL_URL = new URL("index.html", BASE_URL).href;

const CROSS_ORIGIN_ISOLATION_HEADERS: ReadonlyArray<readonly [string, string]> = [
  ["Cross-Origin-Opener-Policy", "same-origin"],
  ["Cross-Origin-Embedder-Policy", "require-corp"],
  ["Content-Security-Policy", "frame-ancestors 'none'"],
];

function addCrossOriginIsolationHeaders(response: Response): Response {
  const headers = new Headers(response.headers);
  for (const [key, value] of CROSS_ORIGIN_ISOLATION_HEADERS) {
    headers.set(key, value);
  }
  return new Response(response.body, {
    status: response.status,
    statusText: response.statusText,
    headers,
  });
}

function offlineResponse(): Response {
  return addCrossOriginIsolationHeaders(new Response(
    "Voland is temporarily unavailable. Check your connection and reload.",
    { status: 503, headers: { "Content-Type": "text/plain; charset=utf-8" } },
  ));
}

async function cacheFirst(request: Request, cacheName: string): Promise<Response> {
  const cache = await sw.caches.open(cacheName);
  const cached = await cache.match(request);
  if (cached) return addCrossOriginIsolationHeaders(cached);

  try {
    const response = await fetch(request);
    if (response.ok) {
      try { await cache.put(request, response.clone()); } catch { /* cache is optional */ }
    }
    return addCrossOriginIsolationHeaders(response);
  } catch {
    return offlineResponse();
  }
}

async function networkFirst(request: Request, isNavigation = false): Promise<Response> {
  const cache = await sw.caches.open(SHELL_CACHE);
  let response: Response;

  try {
    response = await fetch(request);
  } catch {
    // Navigation requests may have been cached under either the original
    // URL or index.html, depending on which version of the worker cached it.
    const cached = await cache.match(request) ??
      (isNavigation ? await cache.match(SHELL_URL) : undefined);
    return cached ? addCrossOriginIsolationHeaders(cached) : offlineResponse();
  }

  if (response.ok) {
    try { await cache.put(request, response.clone()); } catch { /* cache is optional */ }
  }
  return addCrossOriginIsolationHeaders(response);
}

sw.addEventListener("install", () => {
  sw.skipWaiting();
});

sw.addEventListener("activate", (event: ExtendableEvent) => {
  event.waitUntil((async () => {
    const names = await sw.caches.keys();
    await Promise.all(names
      .filter(name =>
        ((name.startsWith("voland-shell-") && name !== SHELL_CACHE) ||
         (name.startsWith("voland-wasm-") && name !== WASM_CACHE)))
      .map(name => sw.caches.delete(name)));
    await sw.clients.claim();
  })());
});

sw.addEventListener("message", (event: ExtendableMessageEvent) => {
  if (event.data?.type === "VOLAND_CLAIM_CLIENTS") {
    event.waitUntil(sw.clients.claim());
  }
});

sw.addEventListener("fetch", (event: FetchEvent) => {
  const url = new URL(event.request.url);
  if (url.origin !== location.origin) return;

  if (url.pathname.startsWith(`${BASE_PATH}api/`)) {
    event.respondWith(fetch(event.request)
      .then(addCrossOriginIsolationHeaders)
      .catch(() => offlineResponse()));
    return;
  }

  if (url.pathname.endsWith(".wasm")) {
    event.respondWith(cacheFirst(event.request, WASM_CACHE));
    return;
  }

  if (event.request.mode === "navigate") {
    event.respondWith(networkFirst(new Request(SHELL_URL), true));
    return;
  }

  event.respondWith(networkFirst(event.request));
});
