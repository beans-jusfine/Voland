/**
 * Main-thread writer for the shared input region (§18).
 *
 * The caller must be the sole writer. All payload writes use Atomics against
 * the shared linear memory; float values are bit-cast through a private
 * scratch buffer so no non-atomic shared writes occur.
 */

export const INPUT_CONTROLLER_COUNT = 8;
export const INPUT_CONTROLLER_SLOT_BYTES = 64;
export const INPUT_TOUCH_BYTES = 128;
export const INPUT_TOUCH_POINT_COUNT = 10;
export const INPUT_REGION_BYTES =
  INPUT_CONTROLLER_COUNT * INPUT_CONTROLLER_SLOT_BYTES + INPUT_TOUCH_BYTES;
export const INPUT_CONTROLLER_FLAG_CONNECTED = 1;

export interface ControllerInput {
  readonly buttons: number;
  readonly axes: readonly number[];
  readonly flags: number;
  readonly motionSample: number;
  readonly accelerometer: readonly [number, number, number];
  readonly gyroscope: readonly [number, number, number];
}

export interface TouchInputPoint {
  readonly fingerId: number;
  readonly x: number;
  readonly y: number;
}

export class InputRegionWriter {
  private readonly words: Int32Array;
  private readonly shorts: Int16Array;
  private readonly floatScratch = new Float32Array(1);
  private readonly bitsScratch = new Uint32Array(this.floatScratch.buffer);

  constructor(buffer: ArrayBufferLike, regionBase: number) {
    if (!Number.isSafeInteger(regionBase) || regionBase < 0 ||
        regionBase % 8 !== 0 || regionBase + INPUT_REGION_BYTES > buffer.byteLength) {
      throw new RangeError("input region is outside the shared linear memory or misaligned");
    }
    this.words = new Int32Array(buffer, regionBase, INPUT_REGION_BYTES / 4);
    this.shorts = new Int16Array(buffer, regionBase, INPUT_REGION_BYTES / 2);
  }

  writeController(index: number, input: ControllerInput): void {
    if (!Number.isInteger(index) || index < 0 || index >= INPUT_CONTROLLER_COUNT) {
      throw new RangeError("controller index is outside the input region");
    }
    if (input.axes.length > 8) throw new RangeError("controller has more than eight axes");

    const wordBase = index * (INPUT_CONTROLLER_SLOT_BYTES / 4);
    const shortBase = index * (INPUT_CONTROLLER_SLOT_BYTES / 2);
    const odd = this.beginWrite(this.words, wordBase);
    Atomics.store(this.words, wordBase + 1, input.buttons | 0);
    for (let i = 0; i < 8; i++) {
      const axis = input.axes[i] ?? 0;
      const scaled = Math.max(-32768, Math.min(32767, Math.round(axis * (axis < 0 ? 32768 : 32767))));
      Atomics.store(this.shorts, shortBase + 4 + i, scaled);
    }
    Atomics.store(this.words, wordBase + 6, input.flags | 0);
    Atomics.store(this.words, wordBase + 7, input.motionSample | 0);
    for (let i = 0; i < 3; i++) {
      Atomics.store(this.words, wordBase + 8 + i, this.floatBits(input.accelerometer[i]));
      Atomics.store(this.words, wordBase + 11 + i, this.floatBits(input.gyroscope[i]));
    }
    Atomics.store(this.words, wordBase, (odd + 1) | 0);
  }

  writeTouch(points: readonly TouchInputPoint[]): void {
    if (points.length > INPUT_TOUCH_POINT_COUNT) {
      throw new RangeError("touch input exceeds ten points");
    }
    const wordBase = INPUT_CONTROLLER_COUNT * (INPUT_CONTROLLER_SLOT_BYTES / 4);
    const odd = this.beginWrite(this.words, wordBase);
    Atomics.store(this.words, wordBase + 1, points.length);
    for (let i = 0; i < INPUT_TOUCH_POINT_COUNT; i++) {
      const point = points[i];
      const pointBase = wordBase + 2 + i * 3;
      Atomics.store(this.words, pointBase, point?.fingerId ?? 0);
      Atomics.store(this.words, pointBase + 1, this.floatBits(point?.x ?? 0));
      Atomics.store(this.words, pointBase + 2, this.floatBits(point?.y ?? 0));
    }
    Atomics.store(this.words, wordBase, (odd + 1) | 0);
  }

  private beginWrite(view: Int32Array, sequenceIndex: number): number {
    const current = Atomics.load(view, sequenceIndex);
    let odd = (current + 1) | 0;
    if ((odd & 1) === 0) odd = (odd + 1) | 0;
    Atomics.store(view, sequenceIndex, odd);
    return odd;
  }

  private floatBits(value: number): number {
    this.floatScratch[0] = value;
    return this.bitsScratch[0] | 0;
  }
}

/** Poll the browser's eight standard Gamepad slots directly into shared memory. */
export function pollGamepads(writer: InputRegionWriter): void {
  const gamepads = navigator.getGamepads();
  for (let i = 0; i < INPUT_CONTROLLER_COUNT; i++) {
    const gamepad = gamepads[i];
    if (!gamepad || !gamepad.connected) {
      writer.writeController(i, {
        buttons: 0,
        axes: [],
        flags: 0,
        motionSample: 0,
        accelerometer: [0, 0, 0],
        gyroscope: [0, 0, 0],
      });
      continue;
    }

    let buttons = 0;
    for (let button = 0; button < Math.min(gamepad.buttons.length, 32); button++) {
      if (gamepad.buttons[button]?.pressed) buttons |= (1 << button);
    }
    writer.writeController(i, {
      buttons,
      axes: gamepad.axes.slice(0, 8),
      flags: INPUT_CONTROLLER_FLAG_CONNECTED,
      motionSample: 0,
      accelerometer: [0, 0, 0],
      gyroscope: [0, 0, 0],
    });
  }
}
