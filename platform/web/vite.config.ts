import { fileURLToPath } from "node:url";
import { defineConfig } from "vite";
import solid from "vite-plugin-solid";

/** Mirrors tsconfig.json's "paths" - Rollup/Vite don't read tsconfig
 * path mapping on their own, so the alias has to be declared twice. */
const alias = {
  "@":         fileURLToPath(new URL("./src", import.meta.url)),
  "@bindings": fileURLToPath(new URL("./bindings", import.meta.url)),
  "@workers":  fileURLToPath(new URL("./workers", import.meta.url)),
};

/**
 * Cross-origin isolation is mandatory - the shared wasm32 WebAssembly.
 * Memory (§4) is gated behind COOP + COEP. See docs/DESIGN.md section 16
 * ("Required HTTP headers"). Vite's dev and preview servers also set these
 * headers so local testing matches production hosting.
 */
const crossOriginIsolationHeaders = {
  "Cross-Origin-Opener-Policy":   "same-origin",
  "Cross-Origin-Embedder-Policy": "require-corp",
  "Content-Security-Policy":     "frame-ancestors 'none'",
};

export default defineConfig({
  base: process.env.VOLAND_BASE_PATH ?? "/",
  define: {
    __VOLAND_WEB_MEMORY_PAGES__: JSON.stringify(Number(process.env.VOLAND_WEB_MEMORY_PAGES ?? "65535")),
  },
  plugins: [solid()],
  resolve: {
    alias,
  },
  server: {
    headers: crossOriginIsolationHeaders,
    port:    5173,
  },
  preview: {
    headers: crossOriginIsolationHeaders,
    port:    5174,
  },
  worker: {
    format: "es",
  },
  build: {
    target: "es2022",
    sourcemap: true,
    rollupOptions: {
      input: {
        main: fileURLToPath(new URL("./index.html", import.meta.url)),
        // A Service Worker's own URL must be stable, so it gets its own
        // unhashed entry rather than going through the normal asset chunks.
        sw: fileURLToPath(new URL("./sw.ts", import.meta.url)),
      },
      output: {
        entryFileNames: (chunk) => (chunk.name === "sw" ? "sw.js" : "assets/[name]-[hash].js"),
      },
    },
  },
});
