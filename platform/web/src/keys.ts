/**
 * Parser for user-supplied Switch key text files.
 *
 * This validates and decodes KEY_NAME = hexadecimal_value records only.
 * It does not derive, verify, or use cryptographic keys.
 */
export function parseKeyFile(contents: string): ReadonlyMap<string, Uint8Array> {
  const keys = new Map<string, Uint8Array>();
  const lines = contents.replace(/^\uFEFF/, "").split(/\r?\n/);

  for (let index = 0; index < lines.length; index += 1) {
    const line = lines[index].trim();
    if (line.length === 0 || line.startsWith("#")) continue;

    const match = /^([A-Za-z0-9_]+)\s*=\s*([0-9a-fA-F]+)$/.exec(line);
    if (!match) {
      throw new Error("Invalid key entry on line " + (index + 1) + ". Expected KEY_NAME = hexadecimal_value.");
    }

    const name = match[1];
    const hex = match[2];
    if (hex.length % 2 !== 0) {
      throw new Error("Key value on line " + (index + 1) + " has an odd number of hexadecimal digits.");
    }
    if (keys.has(name)) {
      throw new Error('Duplicate key name "' + name + '" on line ' + (index + 1) + ".");
    }

    const bytes = new Uint8Array(hex.length / 2);
    for (let offset = 0; offset < hex.length; offset += 2) {
      bytes[offset / 2] = Number.parseInt(hex.slice(offset, offset + 2), 16);
    }
    keys.set(name, bytes);
  }

  if (keys.size === 0) {
    throw new Error("No key entries were found. Expected KEY_NAME = hexadecimal_value lines.");
  }
  return keys;
}
