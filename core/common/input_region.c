#include "common/input_region.h"

#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(_Atomic uint32_t) == 4, "input ABI requires 32-bit atomics");
_Static_assert(_Alignof(_Atomic uint32_t) == 4, "input ABI requires 32-bit atomic alignment");
_Static_assert(sizeof(_Atomic int16_t) == 2, "input ABI requires 16-bit atomics");
_Static_assert(_Alignof(_Atomic int16_t) == 2, "input ABI requires 16-bit atomic alignment");
_Static_assert(sizeof(float) == 4, "input ABI requires IEEE-754 binary32 floats");
_Static_assert(offsetof(Input_Region_Controller, sequence) == 0, "controller sequence offset");
_Static_assert(offsetof(Input_Region_Controller, buttons) == 4, "controller buttons offset");
_Static_assert(offsetof(Input_Region_Controller, axes) == 8, "controller axes offset");
_Static_assert(offsetof(Input_Region_Controller, flags) == 24, "controller flags offset");
_Static_assert(offsetof(Input_Region_Controller, motion_sample) == 28, "controller motion offset");
_Static_assert(offsetof(Input_Region_Controller, accelerometer_bits) == 32, "controller accelerometer offset");
_Static_assert(offsetof(Input_Region_Controller, gyroscope_bits) == 44, "controller gyroscope offset");
_Static_assert(offsetof(Input_Region_Controller, reserved) == 56, "controller reserved offset");
_Static_assert(sizeof(Input_Region_Controller) == INPUT_REGION_CONTROLLER_SLOT_BYTES,
               "controller slot must be 64 bytes");
_Static_assert(offsetof(Input_Region_Touch, points) == 8, "touch points offset");
_Static_assert(sizeof(Input_Region_Touch) == INPUT_REGION_TOUCH_BYTES,
               "touch block must be 128 bytes");
_Static_assert(offsetof(Input_Region, touch) ==
                   INPUT_REGION_CONTROLLER_COUNT * INPUT_REGION_CONTROLLER_SLOT_BYTES,
               "touch block follows controller slots");
_Static_assert(sizeof(Input_Region) ==
                   INPUT_REGION_CONTROLLER_COUNT * INPUT_REGION_CONTROLLER_SLOT_BYTES +
                       INPUT_REGION_TOUCH_BYTES,
               "input region must be 640 bytes");

static float float_from_bits(uint32_t bits) {
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

bool input_region_read_controller(const Input_Region *region,
                                  uint32_t controller_index,
                                  Input_Controller_Snapshot *out) {
  if (!region || !out || controller_index >= INPUT_REGION_CONTROLLER_COUNT) return false;

  const Input_Region_Controller *slot = &region->controllers[controller_index];
  for (uint32_t attempt = 0; attempt < INPUT_REGION_MAX_READ_ATTEMPTS; attempt++) {
    const uint32_t before = atomic_load_explicit(&slot->sequence, memory_order_seq_cst);
    if ((before & 1u) != 0) continue;

    Input_Controller_Snapshot snapshot;
    snapshot.buttons = atomic_load_explicit(&slot->buttons, memory_order_seq_cst);
    for (uint32_t i = 0; i < 8; i++) {
      snapshot.axes[i] = atomic_load_explicit(&slot->axes[i], memory_order_seq_cst);
    }
    snapshot.flags = atomic_load_explicit(&slot->flags, memory_order_seq_cst);
    snapshot.motion_sample = atomic_load_explicit(&slot->motion_sample, memory_order_seq_cst);
    for (uint32_t i = 0; i < 3; i++) {
      const uint32_t accel = atomic_load_explicit(&slot->accelerometer_bits[i], memory_order_seq_cst);
      const uint32_t gyro = atomic_load_explicit(&slot->gyroscope_bits[i], memory_order_seq_cst);
      snapshot.accelerometer[i] = float_from_bits(accel);
      snapshot.gyroscope[i] = float_from_bits(gyro);
    }

    const uint32_t after = atomic_load_explicit(&slot->sequence, memory_order_seq_cst);
    if (before == after && (after & 1u) == 0) {
      *out = snapshot;
      return true;
    }
  }
  return false;
}

bool input_region_read_touch(const Input_Region *region, Input_Touch_Snapshot *out) {
  if (!region || !out) return false;

  const Input_Region_Touch *touch = &region->touch;
  for (uint32_t attempt = 0; attempt < INPUT_REGION_MAX_READ_ATTEMPTS; attempt++) {
    const uint32_t before = atomic_load_explicit(&touch->sequence, memory_order_seq_cst);
    if ((before & 1u) != 0) continue;

    Input_Touch_Snapshot snapshot;
    snapshot.point_count = atomic_load_explicit(&touch->point_count, memory_order_seq_cst);
    const bool valid_count = snapshot.point_count <= INPUT_REGION_TOUCH_POINT_COUNT;
    if (valid_count) {
      for (uint32_t i = 0; i < snapshot.point_count; i++) {
        const Input_Region_Touch_Point *point = &touch->points[i];
        const uint32_t x = atomic_load_explicit(&point->x_bits, memory_order_seq_cst);
        const uint32_t y = atomic_load_explicit(&point->y_bits, memory_order_seq_cst);
        snapshot.points[i].finger_id =
            atomic_load_explicit(&point->finger_id, memory_order_seq_cst);
        snapshot.points[i].x = float_from_bits(x);
        snapshot.points[i].y = float_from_bits(y);
      }
    }

    const uint32_t after = atomic_load_explicit(&touch->sequence, memory_order_seq_cst);
    if (before == after && (after & 1u) == 0) {
      if (!valid_count) return false;
      *out = snapshot;
      return true;
    }
  }
  return false;
}
