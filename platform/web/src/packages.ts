/**
 * Read-only Nintendo package inventory for user-selected files.
 *
 * This module parses public PFS0/NSP and XCI/HFS0 container structure only.
 * It never decrypts content, reads key material, extracts files, or uploads data.
 * Blob slices keep multi-gigabyte packages out of memory.
 */

export interface PackageEntry {
  readonly name: string;
  readonly path: string;
  readonly size: number;
  readonly offset: number;
  readonly kind: "nca" | "ticket" | "certificate" | "other";
}

export interface PackageInventory {
  readonly format: "NSP/PFS0" | "XCI";
  readonly entries: readonly PackageEntry[];
  readonly ncaCount: number;
  readonly totalEntryBytes: number;
}

const MEDIA_UNIT = 0x200;
const MAX_ENTRIES = 100_000;
const decoder = new TextDecoder("utf-8", { fatal: true });

function fail(message: string): never {
  throw new Error(message);
}

function checkedRange(offset: number, size: number, limit: number, label: string): void {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(size) ||
      offset < 0 || size < 0 || offset > limit || size > limit - offset) {
    fail(label + " points outside the selected file.");
  }
}

async function readBytes(blob: Blob, offset: number, size: number, label: string): Promise<Uint8Array> {
  checkedRange(offset, size, blob.size, label);
  return new Uint8Array(await blob.slice(offset, offset + size).arrayBuffer());
}

function u32(view: DataView, offset: number): number {
  return view.getUint32(offset, true);
}

function u64(view: DataView, offset: number): number {
  const value = view.getBigUint64(offset, true);
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) fail("Package contains an offset too large for this browser.");
  return Number(value);
}

function magic(bytes: Uint8Array, expected: string): boolean {
  return expected.split("").every((char, index) => bytes[index] === char.charCodeAt(0));
}

function decodeName(strings: Uint8Array, offset: number): string {
  if (offset >= strings.length) fail("Package contains an invalid filename offset.");
  let end = offset;
  while (end < strings.length && strings[end] !== 0) end += 1;
  if (end === strings.length) fail("Package contains an unterminated filename.");
  const name = decoder.decode(strings.subarray(offset, end));
  if (!name || name.includes("/") || name.includes("\\") || name === "." || name === "..") {
    fail("Package contains an unsafe filename.");
  }
  return name;
}

function entryKind(name: string): PackageEntry["kind"] {
  const lower = name.toLowerCase();
  if (lower.endsWith(".nca")) return "nca";
  if (lower.endsWith(".tik")) return "ticket";
  if (lower.endsWith(".cert")) return "certificate";
  return "other";
}

interface ContainerEntry {
  readonly name: string;
  readonly offset: number;
  readonly size: number;
}

async function parseTable(
  blob: Blob,
  baseOffset: number,
  containerSize: number,
  expectedMagic: "PFS0" | "HFS0",
): Promise<ContainerEntry[]> {
  checkedRange(baseOffset, containerSize, blob.size, expectedMagic);
  const fixed = await readBytes(blob, baseOffset, 0x10, expectedMagic + " header");
  if (!magic(fixed, expectedMagic)) fail("The selected file does not contain a valid " + expectedMagic + " container.");
  const header = new DataView(fixed.buffer, fixed.byteOffset, fixed.byteLength);
  const count = u32(header, 4);
  const stringSize = u32(header, 8);
  if (count > MAX_ENTRIES) fail("Package has too many entries to inspect safely.");
  const entrySize = expectedMagic === "PFS0" ? 0x18 : 0x40;
  const tableSize = count * entrySize;
  if (!Number.isSafeInteger(tableSize)) fail("Package entry table is too large.");
  const metadataSize = 0x10 + tableSize + stringSize;
  checkedRange(0, metadataSize, containerSize, expectedMagic + " metadata");
  const metadata = await readBytes(blob, baseOffset + 0x10, tableSize + stringSize, expectedMagic + " entry table");
  const view = new DataView(metadata.buffer, metadata.byteOffset, metadata.byteLength);
  const strings = metadata.subarray(tableSize);
  const dataStart = baseOffset + metadataSize;
  const entries: ContainerEntry[] = [];

  for (let index = 0; index < count; index += 1) {
    const at = index * entrySize;
    const relativeOffset = u64(view, at);
    const size = u64(view, at + 8);
    const nameOffset = u32(view, at + (expectedMagic === "PFS0" ? 0x10 : 0x38));
    const name = decodeName(strings, nameOffset);
    checkedRange(relativeOffset, size, containerSize - metadataSize, expectedMagic + " entry " + name);
    entries.push({ name, offset: dataStart + relativeOffset, size });
  }
  return entries;
}

async function inspectPfs0(file: Blob): Promise<PackageInventory> {
  const entries = await parseTable(file, 0, file.size, "PFS0");
  const mapped = entries.map((entry) => ({
    ...entry,
    path: entry.name,
    kind: entryKind(entry.name),
  }));
  return {
    format: "NSP/PFS0",
    entries: mapped,
    ncaCount: mapped.filter((entry) => entry.kind === "nca").length,
    totalEntryBytes: mapped.reduce((sum, entry) => sum + entry.size, 0),
  };
}

async function inspectXci(file: Blob): Promise<PackageInventory> {
  const headerBytes = await readBytes(file, 0x100, 0x40, "XCI header");
  if (!magic(headerBytes, "HEAD")) fail("The selected file is not a valid XCI game-card image.");
  const header = new DataView(headerBytes.buffer, headerBytes.byteOffset, headerBytes.byteLength);
  const rootSector = u64(header, 0x30);
  if (rootSector === 0) fail("XCI does not declare a root partition.");
  const rootOffset = rootSector * MEDIA_UNIT;
  if (!Number.isSafeInteger(rootOffset)) fail("XCI root partition offset is too large.");
  checkedRange(rootOffset, 0x10, file.size, "XCI root partition");
  const rootEntries = await parseTable(file, rootOffset, file.size - rootOffset, "HFS0");
  const secure = rootEntries.find((entry) => entry.name.toLowerCase() === "secure");
  const selectedEntries = secure
    ? await parseTable(file, secure.offset, secure.size, "HFS0")
    : rootEntries;
  const mapped = selectedEntries.map((entry) => ({
    ...entry,
    path: secure ? "secure/" + entry.name : entry.name,
    kind: entryKind(entry.name),
  }));
  return {
    format: "XCI",
    entries: mapped,
    ncaCount: mapped.filter((entry) => entry.kind === "nca").length,
    totalEntryBytes: mapped.reduce((sum, entry) => sum + entry.size, 0),
  };
}

/** Inspect an NSP/PFS0 or XCI file without loading its contents into memory. */
export async function inspectGamePackage(file: Blob, filename = ""): Promise<PackageInventory> {
  if (filename.toLowerCase().endsWith(".nca")) {
    if (file.size < 0x204) {
      fail("The selected NCA is too small to contain a complete header. Check the file and try again.");
    }
    const ncaMagic = await readBytes(file, 0x200, 4, "NCA header");
    if (magic(ncaMagic, "NCA3") || magic(ncaMagic, "NCA2")) {
      fail("This is a decrypted NCA file, not an NSP/XCI container. Select an NSP or XCI to inspect its package inventory.");
    }
    fail("The NCA header is encrypted or invalid. Voland currently requires pre-decrypted NCA files; prepare your own dump with separate tools first (docs/DUMP.md).");
  }
  if (file.size < 0x10) fail("Selected package is too small to contain a valid header.");
  const prefix = await readBytes(file, 0, Math.min(file.size, 4), "package magic");
  if (magic(prefix, "PFS0")) return inspectPfs0(file);
  if (filename.toLowerCase().endsWith(".xci")) return inspectXci(file);
  if (file.size >= 0x104) {
    const xciMagic = await readBytes(file, 0x100, 4, "XCI magic");
    if (magic(xciMagic, "HEAD")) return inspectXci(file);
  }
  fail("Unsupported package. Select an NSP/PFS0 or XCI file.");
}
