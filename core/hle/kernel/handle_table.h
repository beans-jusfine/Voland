/**
 * Per-process kernel handle table. See docs/DESIGN.md §12.
 *
 * Handles use Horizon's (linear_id << 15) | index encoding. The table
 * owns no objects: callers retain object lifetime and close entries
 * before destroying their objects. A slot's linear_id advances on reuse,
 * so a stale handle cannot alias the next object in that slot.
 */
#ifndef SWITCH_HLE_KERNEL_HANDLE_TABLE_H
#define SWITCH_HLE_KERNEL_HANDLE_TABLE_H

#include <stdbool.h>
#include <stdint.h>

#include "common/result.h"

#define HANDLE_TABLE_CAPACITY ((uint32_t)1024)
#define HANDLE_TABLE_INDEX_BITS 15u
#define HANDLE_TABLE_INDEX_MASK ((uint32_t)0x7FFFu)
/* Reserve the top generation value for Horizon's pseudo-handles. */
#define HANDLE_TABLE_GENERATION_MAX ((uint32_t)0x1FFFEu)

#define HANDLE_PSEUDO_CURRENT_THREAD ((uint32_t)0xFFFF8000u)
#define HANDLE_PSEUDO_CURRENT_PROCESS ((uint32_t)0xFFFF8001u)

typedef enum Kernel_Object_Type {
  KERNEL_OBJECT_NONE = 0,
  KERNEL_OBJECT_THREAD,
  KERNEL_OBJECT_PROCESS,
  KERNEL_OBJECT_SESSION,
  KERNEL_OBJECT_EVENT,
  KERNEL_OBJECT_SHARED_MEMORY,
  KERNEL_OBJECT_TRANSFER_MEMORY
} Kernel_Object_Type;

typedef struct Handle_Entry {
  void *object;
  uint32_t generation;
  Kernel_Object_Type type;
} Handle_Entry;

typedef struct Handle_Table {
  Handle_Entry entries[HANDLE_TABLE_CAPACITY];
  uint32_t next_slot;
  uint32_t live_count;
} Handle_Table;

void handle_table_init(Handle_Table *table);
Error handle_table_insert(Handle_Table *table, Kernel_Object_Type type,
                          void *object, uint32_t *out_handle);
Error handle_table_get(const Handle_Table *table, uint32_t handle,
                       Kernel_Object_Type expected_type, void **out_object);
Error handle_table_resolve(const Handle_Table *table, uint32_t handle,
                           Kernel_Object_Type expected_type,
                           void *current_thread, void *current_process,
                           void **out_object);
Error handle_table_close(Handle_Table *table, uint32_t handle);

#endif
