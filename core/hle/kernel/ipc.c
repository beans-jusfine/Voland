/**
 * HIPC + CMIF framing, service-object dispatch, and process-owned session
 * object storage. Guest handles are issued by the generic Handle_Table.
 */
#include "hle/kernel/ipc.h"

#include "hle/hle.h" /* HLE_RESULT_* / HLE_MAKE_RESULT (hle.h is not
                      * ipc.h's dependency - only ipc.c's, for these
                      * value-level constants; see ipc.h's header comment) */

#include <string.h>

static uint32_t read_u32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint64_t read_u64(const uint8_t *p) {
  uint64_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static void write_u32(uint8_t *p, uint32_t v) { memcpy(p, &v, sizeof(v)); }

static uint32_t align_up(uint32_t value, uint32_t align) {
  return (value + align - 1u) & ~(align - 1u);
}

/* ------------------------------------------------------------------ */
/* IPC_Response.                                                        */
/* ------------------------------------------------------------------ */

bool ipc_response_add_move_object(IPC_Response *res, Service_Object object) {
  if (!res || res->out_object_count >= IPC_MAX_OUT_OBJECTS) return false;
  res->out_objects[res->out_object_count++] = object;
  return true;
}

/* ------------------------------------------------------------------ */
/* Session object storage + generic process handles.                    */
/* ------------------------------------------------------------------ */

void ipc_session_pool_init(IPC_Session_Pool *pool) {
  if (!pool) return;
  memset(pool, 0, sizeof(*pool));
}

Error ipc_session_open(Handle_Table *handles, IPC_Session_Pool *pool,
                       Service_Object object, uint32_t *out_handle) {
  if (!handles || !pool || !out_handle) {
    return ERR(RESULT_INVALID_ARGUMENT, "ipc_session_open: invalid argument");
  }
  for (uint32_t i = 0; i < IPC_MAX_SESSIONS; i++) {
    if (pool->in_use[i]) continue;
    IPC_Session *session = &pool->sessions[i];
    memset(session, 0, sizeof(*session));
    session->object = object;
    Error err = handle_table_insert(handles, KERNEL_OBJECT_SESSION, session, out_handle);
    if (!error_is_ok(err)) {
      memset(session, 0, sizeof(*session));
      return err;
    }
    pool->in_use[i] = true;
    return OK;
  }
  return ERR(RESULT_OUT_OF_MEMORY, "ipc_session_open: session storage exhausted");
}

IPC_Session *ipc_session_get(const Handle_Table *handles, uint32_t handle) {
  void *object = NULL;
  if (!handles ||
      !error_is_ok(handle_table_get(handles, handle, KERNEL_OBJECT_SESSION, &object))) {
    return NULL;
  }
  return (IPC_Session *)object;
}

bool ipc_session_close(Handle_Table *handles, IPC_Session_Pool *pool,
                       uint32_t handle) {
  if (!handles || !pool) return false;
  IPC_Session *session = ipc_session_get(handles, handle);
  if (!session) return false;
  uint32_t slot = IPC_MAX_SESSIONS;
  for (uint32_t i = 0; i < IPC_MAX_SESSIONS; i++) {
    if (&pool->sessions[i] == session && pool->in_use[i]) {
      slot = i;
      break;
    }
  }
  if (slot == IPC_MAX_SESSIONS) return false;
  if (!error_is_ok(handle_table_close(handles, handle))) return false;
  pool->in_use[slot] = false;
  memset(&pool->sessions[slot], 0, sizeof(pool->sessions[slot]));
  return true;
}

/* ------------------------------------------------------------------ */
/* Parse.                                                               */
/* ------------------------------------------------------------------ */

Error ipc_request_parse(const uint8_t *buffer, uint32_t buffer_size, IPC_Request *out) {
  if (!buffer || !out || buffer_size != TLS_IPC_COMMAND_BUFFER_BYTES) {
    return ERR(RESULT_INVALID_ARGUMENT, "ipc_request_parse: NULL argument or bad buffer_size");
  }
  memset(out, 0, sizeof(*out));

  const uint32_t word0 = read_u32(buffer + 0);
  const uint32_t word1 = read_u32(buffer + 4);

  const uint32_t type = word0 & IPC_HIPC_TYPE_MASK;
  const uint32_t num_x = (word0 >> IPC_HIPC_NUM_X_SHIFT) & IPC_HIPC_DESCRIPTOR_COUNT_MASK;
  const uint32_t num_a = (word0 >> IPC_HIPC_NUM_A_SHIFT) & IPC_HIPC_DESCRIPTOR_COUNT_MASK;
  const uint32_t num_b = (word0 >> IPC_HIPC_NUM_B_SHIFT) & IPC_HIPC_DESCRIPTOR_COUNT_MASK;
  const uint32_t num_w = (word0 >> IPC_HIPC_NUM_W_SHIFT) & IPC_HIPC_DESCRIPTOR_COUNT_MASK;
  const uint32_t num_data_words = word1 & IPC_HIPC_NUM_DATA_WORDS_MASK;
  const uint32_t recv_static_mode =
      (word1 >> IPC_HIPC_RECV_STATIC_MODE_SHIFT) & IPC_HIPC_RECV_STATIC_MODE_MASK;
  const bool has_special_header = (word1 & IPC_HIPC_HAS_SPECIAL_HEADER_BIT) != 0;

  if (type == (uint32_t)IPC_MESSAGE_TYPE_TIPC_CLOSE ||
      type >= (uint32_t)IPC_MESSAGE_TYPE_TIPC_REQUEST_BASE) {
    return ERR(RESULT_NOT_IMPLEMENTED, "ipc_request_parse: TIPC messages are deferred (ipc.h)");
  }
  if (type != (uint32_t)IPC_MESSAGE_TYPE_REQUEST && type != (uint32_t)IPC_MESSAGE_TYPE_CLOSE) {
    return ERR(RESULT_NOT_IMPLEMENTED,
              "ipc_request_parse: only Request and Close messages are supported (Phase 1)");
  }
  if (num_x != 0 || num_a != 0 || num_b != 0 || num_w != 0 || recv_static_mode != 0) {
    return ERR(RESULT_NOT_IMPLEMENTED,
              "ipc_request_parse: X/A/B/W buffer descriptors and receive lists are not supported (Phase 1)");
  }

  uint32_t offset = IPC_HIPC_HEADER_BYTES;
  bool has_pid = false;
  uint64_t guest_pid = 0;
  if (has_special_header) {
    const uint32_t special = read_u32(buffer + offset);
    offset += IPC_HIPC_SPECIAL_HEADER_BYTES;
    const uint32_t copy_handle_count =
        (special >> IPC_HIPC_NUM_COPY_HANDLES_SHIFT) & IPC_HIPC_HANDLE_COUNT_MASK;
    const uint32_t move_handle_count =
        (special >> IPC_HIPC_NUM_MOVE_HANDLES_SHIFT) & IPC_HIPC_HANDLE_COUNT_MASK;
    if (copy_handle_count != 0 || move_handle_count != 0) {
      return ERR(RESULT_NOT_IMPLEMENTED,
                "ipc_request_parse: request-direction handle descriptors are not supported (Phase 1)");
    }
    if ((special & IPC_HIPC_SEND_PID_BIT) != 0) {
      guest_pid = read_u64(buffer + offset);
      offset += IPC_HIPC_PID_BYTES;
      has_pid = true;
    }
  }
  /* Every fixed-size field above ends at most 20 bytes in, well inside
   * the 0x100-byte buffer; only the guest-declared data size needs a
   * bounds check. */

  out->has_pid = has_pid;
  out->guest_pid = guest_pid;

  if (type == (uint32_t)IPC_MESSAGE_TYPE_CLOSE) {
    /* libnx's non-domain Close carries no raw data. A domain Close (a
     * Request with a domain header) never reaches here: no session is a
     * domain in Phase 1. */
    out->type = IPC_MESSAGE_TYPE_CLOSE;
    return OK;
  }

  /* The raw data section starts right after the last descriptor and
   * num_data_words covers all of it, alignment padding included (ipc.h). */
  const uint32_t data_start = offset;
  const uint32_t data_end = data_start + num_data_words * IPC_HIPC_DATA_WORD_BYTES;
  const uint32_t cmif_offset = align_up(data_start, IPC_CMIF_ALIGN);
  if (data_end > buffer_size) {
    return ERR(RESULT_MEMORY_FAULT, "ipc_request_parse: declared data size runs past the command buffer");
  }
  if (cmif_offset + IPC_CMIF_HEADER_BYTES > data_end) {
    return ERR(RESULT_MEMORY_FAULT, "ipc_request_parse: raw data too short for the aligned CMIF header");
  }

  if (read_u32(buffer + cmif_offset + IPC_CMIF_MAGIC_OFFSET) != IPC_CMIF_MAGIC_SFCI) {
    return ERR(RESULT_NOT_IMPLEMENTED, "ipc_request_parse: raw data is not an SFCI (CMIF) request");
  }
  /* Version and token are ignored: version is nonzero only for the
   * WithContext types rejected above, and no Phase 1 service reads the
   * token. */

  out->type = IPC_MESSAGE_TYPE_REQUEST;
  out->command_id = read_u32(buffer + cmif_offset + IPC_CMIF_COMMAND_ID_OFFSET);
  out->data = buffer + cmif_offset + IPC_CMIF_HEADER_BYTES;
  out->data_size = data_end - (cmif_offset + IPC_CMIF_HEADER_BYTES);
  return OK;
}

/* ------------------------------------------------------------------ */
/* Dispatch.                                                            */
/* ------------------------------------------------------------------ */

void ipc_dispatch(Service_Object *object, const IPC_Request *req, IPC_Response *res) {
  memset(res, 0, sizeof(*res));
  if (!object || !object->interface || !req) {
    res->result = HLE_RESULT_NOT_IMPLEMENTED;
    return;
  }

  const Service_Interface *iface = object->interface;
  size_t lo = 0;
  size_t hi = iface->command_count;
  const Service_Command *found = NULL;
  while (lo < hi) {
    const size_t mid = lo + (hi - lo) / 2;
    const Service_Command *candidate = &iface->commands[mid];
    if (candidate->command_id == req->command_id) {
      found = candidate;
      break;
    }
    if (candidate->command_id < req->command_id) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }

  if (!found) {
    /* §12 unimplemented-surface policy: unknown command id is logged and
     * fed to the trace buffer by the caller (svc_ipc.c), which has the
     * log/trace access and the interface name; this function stays
     * pure. */
    res->result = HLE_RESULT_NOT_IMPLEMENTED;
    return;
  }

  res->result = found->handler(object, req, res);
}

/* ------------------------------------------------------------------ */
/* Write.                                                               */
/* ------------------------------------------------------------------ */

Error ipc_response_write(uint8_t *buffer, uint32_t buffer_size, const IPC_Response *res,
                         const uint32_t *out_handles, uint32_t out_handle_count) {
  if (!buffer || !res || buffer_size != TLS_IPC_COMMAND_BUFFER_BYTES ||
      out_handle_count != res->out_object_count ||
      (out_handle_count > 0 && !out_handles)) {
    return ERR(RESULT_INVALID_ARGUMENT,
              "ipc_response_write: NULL argument, bad buffer_size, or handle count mismatch");
  }
  memset(buffer, 0, buffer_size);

  const bool has_special_header = out_handle_count > 0;
  uint32_t offset = IPC_HIPC_HEADER_BYTES;
  if (has_special_header) {
    /* No pid and no copy handles: a response never sends its own pid, and
     * Phase 1 never produces a copy handle (ipc.h). */
    write_u32(buffer + offset, out_handle_count << IPC_HIPC_NUM_MOVE_HANDLES_SHIFT);
    offset += IPC_HIPC_SPECIAL_HEADER_BYTES;
    for (uint32_t i = 0; i < out_handle_count; i++) {
      write_u32(buffer + offset, out_handles[i]);
      offset += IPC_HIPC_HANDLE_BYTES;
    }
  }

  /* Raw data: starts right after the handles; the SFCO header sits at the
   * first aligned boundary inside it. Sized the way libnx sizes requests -
   * a full padding reserve plus the header - so the declared section
   * always covers the header wherever the handles leave the start. Phase 1
   * commands produce no output payload. */
  const uint32_t data_start = offset;
  const uint32_t data_bytes = IPC_CMIF_PADDING_BYTES + IPC_CMIF_HEADER_BYTES;
  const uint32_t cmif_offset = align_up(data_start, IPC_CMIF_ALIGN);
  if (data_start + data_bytes > buffer_size) {
    return ERR(RESULT_INVALID_ARGUMENT, "ipc_response_write: response does not fit the command buffer");
  }

  write_u32(buffer + cmif_offset + IPC_CMIF_MAGIC_OFFSET, IPC_CMIF_MAGIC_SFCO);
  write_u32(buffer + cmif_offset + IPC_CMIF_VERSION_OFFSET, 0);
  write_u32(buffer + cmif_offset + IPC_CMIF_RESULT_OFFSET, res->result);
  write_u32(buffer + cmif_offset + IPC_CMIF_TOKEN_OFFSET, 0);

  /* Type 0 in word 0: libnx's response parsing never reads it (ipc.h). */
  const uint32_t word1 = ((data_bytes / IPC_HIPC_DATA_WORD_BYTES) & IPC_HIPC_NUM_DATA_WORDS_MASK) |
                         (has_special_header ? IPC_HIPC_HAS_SPECIAL_HEADER_BIT : 0u);
  write_u32(buffer + 0, (uint32_t)IPC_MESSAGE_TYPE_INVALID);
  write_u32(buffer + 4, word1);

  return OK;
}
