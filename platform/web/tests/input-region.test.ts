/**
 * Input-region writer tests: ABI offsets, atomic payload publication and bounds.
 */
import assert from "node:assert/strict";
import { test } from "node:test";

import {
  INPUT_CONTROLLER_FLAG_CONNECTED,
  INPUT_REGION_BYTES,
  InputRegionWriter,
} from "../src/input-region.ts";

test("controller and touch writes stay inside the fixed 640-byte ABI", () => {
  const buffer = new SharedArrayBuffer(INPUT_REGION_BYTES);
  const writer = new InputRegionWriter(buffer, 0);
  writer.writeController(2, {
    buttons: 0xA5A55A5A,
    axes: [1, -1, 0.5],
    flags: INPUT_CONTROLLER_FLAG_CONNECTED,
    motionSample: 7,
    accelerometer: [1.25, -2.5, 0],
    gyroscope: [0.1, 0.2, 0.3],
  });
  writer.writeTouch([{ fingerId: 12, x: 640.5, y: 360.25 }]);

  const words = new Int32Array(buffer);
  const shorts = new Int16Array(buffer);
  assert.equal(words[2 * 16], 2);
  assert.equal(words[2 * 16 + 1] >>> 0, 0xA5A55A5A);
  assert.equal(shorts[2 * 32 + 4], 32767);
  assert.equal(shorts[2 * 32 + 5], -32768);
  assert.equal(words[2 * 16 + 6], INPUT_CONTROLLER_FLAG_CONNECTED);
  assert.equal(words[2 * 16 + 7], 7);
  assert.equal(words[128], 2);
  assert.equal(words[129], 1);
  assert.equal(words[130], 12);
  assert.equal(words[0], 0);
});

test("writer rejects invalid offsets, controller indices and excessive touch points", () => {
  const buffer = new SharedArrayBuffer(INPUT_REGION_BYTES + 8);
  assert.throws(() => new InputRegionWriter(buffer, 4), RangeError);
  assert.throws(() => new InputRegionWriter(buffer, 16), RangeError);
  const writer = new InputRegionWriter(buffer, 8);
  assert.throws(() => writer.writeController(8, {
    buttons: 0, axes: [], flags: 0, motionSample: 0,
    accelerometer: [0, 0, 0], gyroscope: [0, 0, 0],
  }), RangeError);
  assert.throws(() => writer.writeTouch(Array.from({ length: 11 }, () => ({
    fingerId: 0, x: 0, y: 0,
  }))), RangeError);
});
