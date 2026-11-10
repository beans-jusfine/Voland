import assert from "node:assert/strict";
import test from "node:test";
import { inspectGamePackage } from "../src/packages.ts";

function makePfs0(entries: Array<{ name: string; data: Uint8Array }>): Uint8Array {
  const strings = new TextEncoder().encode(entries.map((entry) => entry.name + "\0").join(""));
  const tableSize = entries.length * 0x18;
  const dataSize = entries.reduce((sum, entry) => sum + entry.data.length, 0);
  const bytes = new Uint8Array(0x10 + tableSize + strings.length + dataSize);
  bytes.set(new TextEncoder().encode("PFS0"), 0);
  const view = new DataView(bytes.buffer);
  view.setUint32(4, entries.length, true);
  view.setUint32(8, strings.length, true);
  let stringOffset = 0;
  let dataOffset = 0;
  entries.forEach((entry, index) => {
    const at = 0x10 + index * 0x18;
    view.setBigUint64(at, BigInt(dataOffset), true);
    view.setBigUint64(at + 8, BigInt(entry.data.length), true);
    view.setUint32(at + 0x10, stringOffset, true);
    stringOffset += entry.name.length + 1;
    dataOffset += entry.data.length;
  });
  bytes.set(strings, 0x10 + tableSize);
  dataOffset = 0;
  for (const entry of entries) {
    bytes.set(entry.data, 0x10 + tableSize + strings.length + dataOffset);
    dataOffset += entry.data.length;
  }
  return bytes;
}

function makeHfs0(entries: Array<{ name: string; data: Uint8Array }>): Uint8Array {
  const strings = new TextEncoder().encode(entries.map((entry) => entry.name + "\0").join(""));
  const tableSize = entries.length * 0x40;
  const dataSize = entries.reduce((sum, entry) => sum + entry.data.length, 0);
  const bytes = new Uint8Array(0x10 + tableSize + strings.length + dataSize);
  bytes.set(new TextEncoder().encode("HFS0"), 0);
  const view = new DataView(bytes.buffer);
  view.setUint32(4, entries.length, true);
  view.setUint32(8, strings.length, true);
  let stringOffset = 0;
  let dataOffset = 0;
  entries.forEach((entry, index) => {
    const at = 0x10 + index * 0x40;
    view.setBigUint64(at, BigInt(dataOffset), true);
    view.setBigUint64(at + 8, BigInt(entry.data.length), true);
    view.setUint32(at + 0x38, stringOffset, true);
    stringOffset += entry.name.length + 1;
    dataOffset += entry.data.length;
  });
  bytes.set(strings, 0x10 + tableSize);
  dataOffset = 0;
  for (const entry of entries) {
    bytes.set(entry.data, 0x10 + tableSize + strings.length + dataOffset);
    dataOffset += entry.data.length;
  }
  return bytes;
}

test("inventories PFS0/NSP entries and counts NCAs", async () => {
  const file = new Blob([makePfs0([
    { name: "program.nca", data: new Uint8Array([1, 2, 3]) },
    { name: "control.nca", data: new Uint8Array([4, 5]) },
    { name: "metadata.xml", data: new Uint8Array([6]) },
  ])]);
  const result = await inspectGamePackage(file, "dump.nsp");
  assert.equal(result.format, "NSP/PFS0");
  assert.equal(result.entries.length, 3);
  assert.equal(result.ncaCount, 2);
  assert.equal(result.totalEntryBytes, 6);
  assert.equal(result.entries[0].path, "program.nca");
});

test("walks the XCI root and secure HFS0 partitions", async () => {
  const secure = makeHfs0([{ name: "game.nca", data: new Uint8Array([7, 8, 9, 10]) }]);
  const root = makeHfs0([{ name: "secure", data: secure }, { name: "normal", data: new Uint8Array([1]) }]);
  const bytes = new Uint8Array(0x200 + root.length);
  bytes.set(new TextEncoder().encode("HEAD"), 0x100);
  new DataView(bytes.buffer).setBigUint64(0x130, 1n, true);
  bytes.set(root, 0x200);
  const result = await inspectGamePackage(new Blob([bytes]), "cartridge.xci");
  assert.equal(result.format, "XCI");
  assert.equal(result.ncaCount, 1);
  assert.equal(result.entries[0].path, "secure/game.nca");
  assert.equal(result.entries[0].size, 4);
});

test("rejects malformed PFS0 tables", async () => {
  const bad = new Uint8Array(16);
  bad.set(new TextEncoder().encode("PFS0"));
  new DataView(bad.buffer).setUint32(4, 0xffffffff, true);
  await assert.rejects(() => inspectGamePackage(new Blob([bad]), "broken.nsp"), /too many entries|metadata/);
});

test("rejects unknown package formats", async () => {
  await assert.rejects(() => inspectGamePackage(new Blob([new Uint8Array(64)]), "game.bin"), /Unsupported package/);
});

 
test("directs encrypted NCA selections to the dumping guide", async () => {
  const encrypted = new Uint8Array(0x204);
  await assert.rejects(
    () => inspectGamePackage(new Blob([encrypted]), "game.nca"),
    /NCA header is encrypted or invalid.*docs\/DUMP\.md/,
  );
});

test("does not mistake a plaintext NCA for an encrypted input", async () => {
  const plaintext = new Uint8Array(0x204);
  plaintext.set(new TextEncoder().encode("NCA3"), 0x200);
  await assert.rejects(
    () => inspectGamePackage(new Blob([plaintext]), "game.nca"),
    /decrypted NCA file, not an NSP\/XCI container/,
  );
});
