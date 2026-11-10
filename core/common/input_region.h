/**
 * Shared input region ABI. All cross-thread payload fields are atomic:
 * the sequence counters provide a coherent snapshot, while atomic payload
 * accesses avoid C11 data races with JavaScript Atomics writers (§18).
 */
#ifndef SWITCH_COMMON_INPUT_REGION_H
#define SWITCH_COMMON_INPUT_REGION_H

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

#define INPUT_REGION_CONTROLLER_COUNT 8u
#define INPUT_REGION_CONTROLLER_SLOT_BYTES 64u
#define INPUT_REGION_TOUCH_BYTES 128u
#define INPUT_REGION_TOUCH_POINT_COUNT 10u
#define INPUT_REGION_MAX_READ_ATTEMPTS 8u

typedef struct Input_Region_Controller {
  _Atomic uint32_t sequence;              /* +0 */
  _Atomic uint32_t buttons;               /* +4 */
  _Atomic int16_t axes[8];                /* +8 */
  _Atomic uint32_t flags;                 /* +24 */
  _Atomic uint32_t motion_sample;         /* +28 */
  _Atomic uint32_t accelerometer_bits[3]; /* +32: IEEE-754 f32 bits */
  _Atomic uint32_t gyroscope_bits[3];     /* +44: IEEE-754 f32 bits */
  _Atomic uint32_t reserved[2];           /* +56 */
} Input_Region_Controller;

typedef struct Input_Region_Touch_Point {
  _Atomic uint32_t finger_id;
  _Atomic uint32_t x_bits; /* IEEE-754 f32 bits */
  _Atomic uint32_t y_bits; /* IEEE-754 f32 bits */
} Input_Region_Touch_Point;

typedef struct Input_Region_Touch {
  _Atomic uint32_t sequence; /* +0 */
  _Atomic uint32_t point_count; /* +4 */
  Input_Region_Touch_Point points[INPUT_REGION_TOUCH_POINT_COUNT]; /* +8 */
} Input_Region_Touch;

typedef struct Input_Region {
  Input_Region_Controller controllers[INPUT_REGION_CONTROLLER_COUNT];
  Input_Region_Touch touch;
} Input_Region;

typedef struct Input_Controller_Snapshot {
  uint32_t buttons;
  int16_t axes[8];
  uint32_t flags;
  uint32_t motion_sample;
  float accelerometer[3];
  float gyroscope[3];
} Input_Controller_Snapshot;

typedef struct Input_Touch_Snapshot {
  uint32_t point_count;
  struct {
    uint32_t finger_id;
    float x;
    float y;
  } points[INPUT_REGION_TOUCH_POINT_COUNT];
} Input_Touch_Snapshot;

/** Read one coherent controller snapshot; false means invalid input or contention. */
bool input_region_read_controller(const Input_Region *region,
                                  uint32_t controller_index,
                                  Input_Controller_Snapshot *out);

/** Read one coherent touch snapshot; false means invalid input or contention. */
bool input_region_read_touch(const Input_Region *region,
                             Input_Touch_Snapshot *out);

#endif /* SWITCH_COMMON_INPUT_REGION_H */
