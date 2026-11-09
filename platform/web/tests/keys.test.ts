import assert from "node:assert/strict";
import test from "node:test";
import { parseKeyFile } from "../src/keys.ts";

test("parses comments, blank lines, BOM, and hexadecimal bytes", () => {
  const keys = parseKeyFile("\uFEFF# local dump\n\nheader_key = 0011aAff\nmaster_key_00=deadbeef");
  assert.equal(keys.size, 2);
  assert.deepEqual(Array.from(keys.get("header_key") ?? []), [0, 17, 170, 255]);
  assert.deepEqual(Array.from(keys.get("master_key_00") ?? []), [222, 173, 190, 239]);
});

test("rejects malformed non-comment lines with line number", () => {
  assert.throws(() => parseKeyFile("# comment\nnot a key"), /line 2/);
});

test("rejects odd-length hexadecimal values", () => {
  assert.throws(() => parseKeyFile("header_key = abc"), /odd number/);
});

test("rejects duplicate key names", () => {
  assert.throws(() => parseKeyFile("key = 00\nkey = 11"), /Duplicate key name/);
});

test("rejects files without key entries", () => {
  assert.throws(() => parseKeyFile("# only a comment\n\n"), /No key entries/);
});
