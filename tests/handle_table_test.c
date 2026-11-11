#define CHECK_NAME "handle_table_test"
#include "check.h"

#include "hle/kernel/handle_table.h"

int main(void) {
  Handle_Table table;
  handle_table_init(&table);
  CHECK(table.live_count == 0);

  int thread_object = 11;
  int process_object = 22;
  int session_object = 33;
  uint32_t thread_handle = 0, process_handle = 0, session_handle = 0;

  CHECK_OK(handle_table_insert(&table, KERNEL_OBJECT_THREAD, &thread_object,
                               &thread_handle));
  CHECK(thread_handle == 0x8000u);
  CHECK_OK(handle_table_insert(&table, KERNEL_OBJECT_PROCESS, &process_object,
                               &process_handle));
  CHECK(process_handle == 0x8001u);
  CHECK_OK(handle_table_insert(&table, KERNEL_OBJECT_SESSION, &session_object,
                               &session_handle));
  CHECK(session_handle == 0x8002u);
  CHECK(table.live_count == 3);

  void *object = NULL;
  CHECK_OK(handle_table_get(&table, thread_handle, KERNEL_OBJECT_THREAD, &object));
  CHECK(object == &thread_object);
  CHECK_CODE(handle_table_get(&table, thread_handle, KERNEL_OBJECT_SESSION, &object),
             RESULT_NOT_FOUND);
  CHECK_CODE(handle_table_get(&table, 0, KERNEL_OBJECT_NONE, &object), RESULT_NOT_FOUND);

  /* Pseudo-handles resolve outside the table and enforce their types. */
  CHECK_OK(handle_table_resolve(&table, HANDLE_PSEUDO_CURRENT_THREAD,
                                KERNEL_OBJECT_THREAD, &thread_object,
                                &process_object, &object));
  CHECK(object == &thread_object);
  CHECK_OK(handle_table_resolve(&table, HANDLE_PSEUDO_CURRENT_PROCESS,
                                KERNEL_OBJECT_NONE, &thread_object,
                                &process_object, &object));
  CHECK(object == &process_object);
  CHECK_CODE(handle_table_resolve(&table, HANDLE_PSEUDO_CURRENT_THREAD,
                                  KERNEL_OBJECT_PROCESS, &thread_object,
                                  &process_object, &object), RESULT_NOT_FOUND);
  CHECK_CODE(handle_table_close(&table, HANDLE_PSEUDO_CURRENT_PROCESS),
             RESULT_INVALID_ARGUMENT);

  /* Closing invalidates the old handle; reusing the slot advances its
   * generation so stale guest values cannot alias the new object. */
  CHECK_OK(handle_table_close(&table, thread_handle));
  CHECK(table.live_count == 2);
  CHECK_CODE(handle_table_get(&table, thread_handle, KERNEL_OBJECT_NONE, &object),
             RESULT_NOT_FOUND);
  uint32_t replacement_handle = 0;
  CHECK_OK(handle_table_insert(&table, KERNEL_OBJECT_THREAD, &thread_object,
                               &replacement_handle));
  CHECK(replacement_handle != thread_handle);
  CHECK((replacement_handle & HANDLE_TABLE_INDEX_MASK) == 0);
  CHECK_CODE(handle_table_get(&table, thread_handle, KERNEL_OBJECT_NONE, &object),
             RESULT_NOT_FOUND);
  CHECK_OK(handle_table_get(&table, replacement_handle, KERNEL_OBJECT_THREAD,
                            &object));
  CHECK(object == &thread_object);

  CHECK_CODE(handle_table_insert(NULL, KERNEL_OBJECT_THREAD, &thread_object,
                                 &replacement_handle), RESULT_INVALID_ARGUMENT);
  CHECK_CODE(handle_table_insert(&table, KERNEL_OBJECT_NONE, &thread_object,
                                 &replacement_handle), RESULT_INVALID_ARGUMENT);
  CHECK_CODE(handle_table_insert(&table, KERNEL_OBJECT_THREAD, NULL,
                                 &replacement_handle), RESULT_INVALID_ARGUMENT);
  CHECK_CODE(handle_table_get(&table, replacement_handle, KERNEL_OBJECT_NONE,
                              NULL), RESULT_INVALID_ARGUMENT);
  CHECK_OK(handle_table_close(&table, replacement_handle));
  CHECK_OK(handle_table_close(&table, process_handle));
  CHECK_OK(handle_table_close(&table, session_handle));
  CHECK(table.live_count == 0);

  puts("[" CHECK_NAME "] ok");
  return 0;
}
