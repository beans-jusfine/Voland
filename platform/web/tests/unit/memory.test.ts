/**
 * Unit tests for src/memory.ts: the one shared memory's size (§4) agrees
 * with the two other places that state it. Growth is disabled, so a
 * mismatch would only surface as an instantiation failure in a browser.
 */
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";

import { TOTAL_MEMORY_BYTES, TOTAL_MEMORY_PAGES, WASM_PAGE_BYTES } from "../../src/memory.ts";

const REPO_ROOT = new URL("../../../../", import.meta.url);

function repoFile(path: string): string {
  return readFileSync(new URL(path, REPO_ROOT), "utf8");
}

test("the memory is 65535 wasm32 pages: 4 GiB minus one page, never 2^32 (§4)", () => {
  assert.equal(WASM_PAGE_BYTES, 65_536);
  assert.equal(TOTAL_MEMORY_PAGES, 65_535);
  assert.equal(TOTAL_MEMORY_BYTES, 4_294_901_760);
  assert.ok(TOTAL_MEMORY_BYTES < 2 ** 32);
});

test("CMakeLists.txt derives Emscripten memory from the shared page-count setting (§24)", () => {
  const cmake = repoFile("CMakeLists.txt");
  assert.match(cmake, /set\(VOLAND_WEB_MEMORY_PAGES "65535"/);
  assert.match(cmake, /math\(EXPR VOLAND_WEB_MEMORY_BYTES "\$\{VOLAND_WEB_MEMORY_PAGES\} \* 65536"\)/);
  assert.match(cmake, /-sINITIAL_MEMORY=\$\{VOLAND_WEB_MEMORY_BYTES\}/);
  assert.equal(TOTAL_MEMORY_BYTES, 4_294_901_760);
});

test("GitHub Pages uses the same 1.5 GiB / 512 MiB development profile in both builds", () => {
  const workflow = repoFile(".github/workflows/pages.yml");
  assert.match(workflow, /VOLAND_WEB_MEMORY_PAGES=24576/);
  assert.match(workflow, /VOLAND_WEB_GUEST_RAM_MIB=512/);
  assert.match(workflow, /VOLAND_WEB_MEMORY_PAGES: 24576/);
});

test("CMakeLists.txt builds wasm32: no -m64 or MEMORY64 codegen flag (§24)", () => {
  const flagLines = repoFile("CMakeLists.txt")
    .split("\n")
    .map(line => line.replace(/#.*$/, "").trim())
    .filter(line => line.length > 0);
  assert.ok(!flagLines.some(line => /(^|\s)-m64(\s|$)|-sMEMORY64/.test(line)));
});

test("layout.h's LAYOUT_LINEAR_MEMORY_PAGES equals TOTAL_MEMORY_PAGES (§4)", () => {
  const match = /#define LAYOUT_LINEAR_MEMORY_PAGES \(\(uint64_t\)(\d+)\)/.exec(repoFile("core/common/layout.h"));
  assert.ok(match, "no LAYOUT_LINEAR_MEMORY_PAGES in core/common/layout.h");
  assert.equal(Number(match[1]), TOTAL_MEMORY_PAGES);
});
