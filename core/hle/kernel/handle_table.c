#include "hle/kernel/handle_table.h"

#include <string.h>

static uint32_t make_handle(uint32_t slot, uint32_t generation) {
  return (generation << HANDLE_TABLE_INDEX_BITS) | slot;
}

static bool decode_handle(uint32_t handle, uint32_t *slot, uint32_t *generation) {
  const uint32_t index = handle & HANDLE_TABLE_INDEX_MASK;
  const uint32_t linear_id = handle >> HANDLE_TABLE_INDEX_BITS;
  if (index >= HANDLE_TABLE_CAPACITY || linear_id == 0 ||
      linear_id > HANDLE_TABLE_GENERATION_MAX) {
    return false;
  }
  *slot = index;
  *generation = linear_id;
  return true;
}

void handle_table_init(Handle_Table *table) {
  if (!table) return;
  memset(table, 0, sizeof(*table));
  /* Generation one gives the first entry Horizon's conventional 0x8000. */
  for (uint32_t i = 0; i < HANDLE_TABLE_CAPACITY; i++) {
    table->entries[i].generation = 1;
  }
}

Error handle_table_insert(Handle_Table *table, Kernel_Object_Type type,
                          void *object, uint32_t *out_handle) {
  if (!table || !object || !out_handle || type <= KERNEL_OBJECT_NONE ||
      type > KERNEL_OBJECT_TRANSFER_MEMORY) {
    return ERR(RESULT_INVALID_ARGUMENT, "handle_table_insert: invalid argument");
  }
  for (uint32_t n = 0; n < HANDLE_TABLE_CAPACITY; n++) {
    const uint32_t slot = (table->next_slot + n) % HANDLE_TABLE_CAPACITY;
    Handle_Entry *entry = &table->entries[slot];
    if (entry->object) continue;
    if (entry->generation == 0) entry->generation = 1;
    entry->object = object;
    entry->type = type;
    table->live_count++;
    table->next_slot = (slot + 1) % HANDLE_TABLE_CAPACITY;
    *out_handle = make_handle(slot, entry->generation);
    return OK;
  }
  return ERR(RESULT_OUT_OF_MEMORY, "handle_table_insert: table full");
}

Error handle_table_get(const Handle_Table *table, uint32_t handle,
                       Kernel_Object_Type expected_type, void **out_object) {
  if (!table || !out_object || expected_type > KERNEL_OBJECT_TRANSFER_MEMORY) {
    return ERR(RESULT_INVALID_ARGUMENT, "handle_table_get: invalid argument");
  }
  uint32_t slot = 0, generation = 0;
  if (!decode_handle(handle, &slot, &generation)) {
    return ERR(RESULT_NOT_FOUND, "handle_table_get: invalid handle");
  }
  const Handle_Entry *entry = &table->entries[slot];
  if (!entry->object || entry->generation != generation ||
      (expected_type != KERNEL_OBJECT_NONE && entry->type != expected_type)) {
    return ERR(RESULT_NOT_FOUND, "handle_table_get: stale handle or wrong object type");
  }
  *out_object = entry->object;
  return OK;
}

Error handle_table_resolve(const Handle_Table *table, uint32_t handle,
                           Kernel_Object_Type expected_type,
                           void *current_thread, void *current_process,
                           void **out_object) {
  if (!out_object || expected_type > KERNEL_OBJECT_TRANSFER_MEMORY) {
    return ERR(RESULT_INVALID_ARGUMENT, "handle_table_resolve: invalid argument");
  }
  if (handle == HANDLE_PSEUDO_CURRENT_THREAD) {
    if (!current_thread || (expected_type != KERNEL_OBJECT_NONE &&
                            expected_type != KERNEL_OBJECT_THREAD)) {
      return ERR(RESULT_NOT_FOUND, "handle_table_resolve: current thread unavailable");
    }
    *out_object = current_thread;
    return OK;
  }
  if (handle == HANDLE_PSEUDO_CURRENT_PROCESS) {
    if (!current_process || (expected_type != KERNEL_OBJECT_NONE &&
                             expected_type != KERNEL_OBJECT_PROCESS)) {
      return ERR(RESULT_NOT_FOUND, "handle_table_resolve: current process unavailable");
    }
    *out_object = current_process;
    return OK;
  }
  return handle_table_get(table, handle, expected_type, out_object);
}

Error handle_table_close(Handle_Table *table, uint32_t handle) {
  if (!table) return ERR(RESULT_INVALID_ARGUMENT, "handle_table_close: NULL table");
  if (handle == HANDLE_PSEUDO_CURRENT_THREAD ||
      handle == HANDLE_PSEUDO_CURRENT_PROCESS) {
    return ERR(RESULT_INVALID_ARGUMENT, "handle_table_close: pseudo-handle");
  }
  uint32_t slot = 0, generation = 0;
  if (!decode_handle(handle, &slot, &generation)) {
    return ERR(RESULT_NOT_FOUND, "handle_table_close: invalid handle");
  }
  Handle_Entry *entry = &table->entries[slot];
  if (!entry->object || entry->generation != generation) {
    return ERR(RESULT_NOT_FOUND, "handle_table_close: stale handle");
  }
  entry->object = NULL;
  entry->type = KERNEL_OBJECT_NONE;
  entry->generation = entry->generation == HANDLE_TABLE_GENERATION_MAX
                          ? 1 : entry->generation + 1;
  table->live_count--;
  if (slot < table->next_slot) table->next_slot = slot;
  return OK;
}
