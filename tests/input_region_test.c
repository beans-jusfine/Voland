/**
 * Unit tests for the fixed shared input-region ABI and seqlock readers.
 */
#include "common/input_region.h"

#include <assert.h>
#include <string.h>

static uint32_t float_bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static void test_controller_snapshot(void) {
  static Input_Region region;
  Input_Controller_Snapshot snapshot;
  Input_Region_Controller *slot = &region.controllers[3];
  atomic_store(&slot->sequence, 1u);
  atomic_store(&slot->buttons, 0xA5A55A5Au);
  for (uint32_t i = 0; i < 8; i++) atomic_store(&slot->axes[i], (int16_t)((int32_t)i * 100 - 300));
  atomic_store(&slot->flags, 1u);
  atomic_store(&slot->motion_sample, 17u);
  for (uint32_t i = 0; i < 3; i++) {
    atomic_store(&slot->accelerometer_bits[i], float_bits((float)i + 0.25f));
    atomic_store(&slot->gyroscope_bits[i], float_bits((float)i + 1.5f));
  }
  atomic_store(&slot->sequence, 2u);

  assert(input_region_read_controller(&region, 3, &snapshot));
  assert(snapshot.buttons == 0xA5A55A5Au);
  assert(snapshot.axes[0] == -300 && snapshot.axes[7] == 400);
  assert(snapshot.flags == 1u && snapshot.motion_sample == 17u);
  assert(snapshot.accelerometer[2] == 2.25f);
  assert(snapshot.gyroscope[1] == 2.5f);
  assert(!input_region_read_controller(&region, INPUT_REGION_CONTROLLER_COUNT, &snapshot));
  assert(!input_region_read_controller(NULL, 0, &snapshot));
  assert(!input_region_read_controller(&region, 0, NULL));
}

static void test_touch_snapshot(void) {
  static Input_Region region;
  Input_Touch_Snapshot snapshot;
  Input_Region_Touch *touch = &region.touch;
  atomic_store(&touch->sequence, 1u);
  atomic_store(&touch->point_count, 2u);
  atomic_store(&touch->points[0].finger_id, 9u);
  atomic_store(&touch->points[0].x_bits, float_bits(128.5f));
  atomic_store(&touch->points[0].y_bits, float_bits(64.25f));
  atomic_store(&touch->points[1].finger_id, 10u);
  atomic_store(&touch->points[1].x_bits, float_bits(900.0f));
  atomic_store(&touch->points[1].y_bits, float_bits(700.0f));
  atomic_store(&touch->sequence, 2u);

  assert(input_region_read_touch(&region, &snapshot));
  assert(snapshot.point_count == 2u);
  assert(snapshot.points[0].finger_id == 9u);
  assert(snapshot.points[0].x == 128.5f);
  assert(snapshot.points[1].y == 700.0f);

  atomic_store(&touch->point_count, INPUT_REGION_TOUCH_POINT_COUNT + 1u);
  atomic_store(&touch->sequence, 4u);
  assert(!input_region_read_touch(&region, &snapshot));
  assert(!input_region_read_touch(NULL, &snapshot));
  assert(!input_region_read_touch(&region, NULL));
}

static void test_odd_sequence_retries_bounded(void) {
  static Input_Region region;
  Input_Controller_Snapshot controller;
  Input_Touch_Snapshot touch;
  atomic_store(&region.controllers[0].sequence, 1u);
  atomic_store(&region.touch.sequence, 1u);
  assert(!input_region_read_controller(&region, 0, &controller));
  assert(!input_region_read_touch(&region, &touch));
}

int main(void) {
  test_controller_snapshot();
  test_touch_snapshot();
  test_odd_sequence_retries_bounded();
  return 0;
}
