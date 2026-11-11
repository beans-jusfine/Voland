#include "emulator.h"

#include "common/arena.h"
#include "common/assert.h"
#include "common/log.h"
#include "hle/loader/exefs.h"
#include "hle/loader/nca_parse.h"
#include "hle/loader/meta_nca.h"
#include "hle/loader/system_content.h"
#include "hle/loader/npdm.h"
#include "hle/loader/nro.h"

#include <string.h>

Error emulator_create(Emulator* out) {
  if (!out) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_create: out is NULL");
  }

  memset(out, 0, sizeof(*out));

  /* 1. Linear memory layout (§4): every region reserved in one pass. */
  Error layout_err = layout_create();
  if (!error_is_ok(layout_err)) {
    return layout_err;
  }

  /* 2. Softmmu (§5): page tables over the layout's L1 region. Shared by
   * every CPU_State and by HLE. */
  out->vmm = vmm_create();
  if (!out->vmm) {
    layout_destroy();
    memset(out, 0, sizeof(*out));
    return ERR(RESULT_OUT_OF_MEMORY, "emulator_create: vmm_create failed");
  }

  /* 3. CPU backend selected at configure time; it receives the MMU, never
   * raw guest RAM (§8). */
  out->cpu_backend = cpu_get_active_backend();
  SWITCH_ASSERT_ALWAYS(out->cpu_backend != NULL, "no CPU backend registered");
  const Error scheduler_err = scheduler_init(&out->scheduler, out->cpu_backend);
  if (!error_is_ok(scheduler_err)) {
    vmm_destroy(out->vmm);
    layout_destroy();
    memset(out, 0, sizeof(*out));
    return scheduler_err;
  }

  out->cpu_state = out->cpu_backend->create(out->vmm, &out->hle);
  if (!out->cpu_state) {
    vmm_destroy(out->vmm);
    layout_destroy();
    memset(out, 0, sizeof(*out));
    return ERR(RESULT_OUT_OF_MEMORY, "emulator_create: CPU backend failed to init");
  }

  /* 4. Guest physical pages (§4): all of guest RAM, handed out by the
   * bootstrap and later the memory HLE. */
  const Error pages_err = page_allocator_init_guest_ram(&out->pages);
  SWITCH_ASSERT_ALWAYS(error_is_ok(pages_err), "page allocator over guest RAM failed");

  /* 5. HLE context + svc/undefined hooks. `&out->process` is valid
   * before emulator_load_program() populates it - same pattern as
   * `out->cpu_state` above being handed `&out->hle` before
   * hle_context_init() has even run (see hle.h's doc comment). */
  hle_context_init(&out->hle, out->cpu_backend, out->vmm, &out->process, &out->pages);
  out->cpu_backend->set_svc_handler(out->cpu_state, hle_on_svc);
  out->cpu_backend->set_undefined_handler(out->cpu_state, hle_on_undefined);

  /* 6. IPC (§25 Phase 1 "Minimal IPC + sm: stub"): sm:'s registry, bound
   * to the well-known port ConnectToNamedPort("sm:") resolves to
   * (svc_ipc.c). No
   * service module registers with sm yet - none exist before Phase 2+. */
  sm_context_init(&out->sm);
  out->hle.sm_object = sm_make_service_object(&out->sm);

  log_info("[emulator] created (backend=%s %s)",
           out->cpu_backend->name,
           out->cpu_backend->version);

  return OK;
}

void emulator_destroy(Emulator* emulator) {
  if (!emulator) return;
  emulator_unload_program(emulator);
  if (emulator->cpu_backend && emulator->cpu_state) {
    emulator->cpu_backend->destroy(emulator->cpu_state);
  }
  vmm_destroy(emulator->vmm);
  layout_destroy();
  memset(emulator, 0, sizeof(*emulator));
}

/* Title-level launch: Meta NCA -> CNMT -> content-ID resolution ->
 * unique PROGRAM NCA -> the existing NCA/ExeFS/NPDM/process bootstrap.
 * The temporary metadata arena is released before emulator_load_program()
 * allocates its larger NSO staging arena. */
Error emulator_load_title(Emulator *emulator, const Byte_Source *meta_source,
                          const SystemSoftware_Input *content_inputs,
                          uint32_t content_count, CNMT_Content_Layout layout,
                          uint64_t aslr_seed) {
  if (!emulator || !meta_source || (content_count != 0 && !content_inputs)) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_load_title: invalid argument");
  }
  if (emulator->program_loaded) {
    return ERR(RESULT_INVALID_ARGUMENT,
               "emulator_load_title: unload the current program first");
  }

  Arena scratch;
  if (!arena_create(&scratch, (size_t)16 * 1024 * 1024)) {
    return ERR(RESULT_OUT_OF_MEMORY, "emulator_load_title: metadata arena");
  }

  Meta_NCA meta;
  Error err = meta_nca_open(meta_source, &scratch, layout, &meta);
  SystemContent_Inventory inventory;
  if (error_is_ok(err)) {
    err = system_content_resolve(&meta.cnmt, content_inputs, content_count,
                                 &scratch, &inventory);
  }

  const Byte_Source *program_source = NULL;
  bool found_program = false;
  if (error_is_ok(err)) {
    for (uint32_t i = 0; i < meta.cnmt.header.content_count; i++) {
      const SystemContent_Entry *entry = &inventory.entries[i];
      if (entry->content->content_type != NCA_CONTENT_PROGRAM) continue;
      if (found_program) {
        err = ERR(RESULT_ALREADY_EXISTS,
                  "emulator_load_title: CNMT contains multiple PROGRAM contents");
        break;
      }
      found_program = true;
      if (entry->status != SYSTEM_CONTENT_MATCHED) {
        err = entry->error;
        if (error_is_ok(err)) {
          err = ERR(RESULT_NOT_FOUND,
                    "emulator_load_title: PROGRAM content is not available");
        }
        break;
      }
      if (entry->input_index >= content_count ||
          !content_inputs[entry->input_index].source) {
        err = ERR(RESULT_INVALID_ARGUMENT,
                  "emulator_load_title: resolved PROGRAM source is invalid");
        break;
      }
      program_source = content_inputs[entry->input_index].source;
    }
    if (error_is_ok(err) && !found_program) {
      err = ERR(RESULT_NOT_FOUND,
                "emulator_load_title: CNMT contains no PROGRAM content");
    }
  }

  arena_destroy(&scratch);
  if (!error_is_ok(err)) return err;
  return emulator_load_program(emulator, program_source, aslr_seed);
}

/* §12 "what load a game actually does", step 1 through 4, with the §1.6
 * encrypted-input diagnosis surfaced before any structural parse. */
Error emulator_load_program(Emulator* emulator, const Byte_Source* nca_source,
                            uint64_t aslr_seed) {
  if (!emulator || !nca_source) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_load_program: NULL argument");
  }
  if (emulator->program_loaded) {
    return ERR(RESULT_INVALID_ARGUMENT,
               "emulator_load_program: a program is already loaded; unload it first");
  }

  /* Step 1: NCA -> ExeFS -> npdm. */
  NCA_File nca;
  Error err = nca_open(nca_source, &nca);
  if (!error_is_ok(err)) return err;
  if (nca.header.content_type != NCA_CONTENT_PROGRAM) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_load_program: not a PROGRAM NCA");
  }
  const int exefs_index = nca_find_section(&nca, NCA_FS_PARTITION_FS);
  if (exefs_index < 0) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_load_program: PROGRAM NCA has no ExeFS");
  }
  err = nca_probe_section(&nca, (uint32_t)exefs_index);
  if (!error_is_ok(err)) return err;

  /* The loader arena lives for this call only: ExeFS directory, the npdm
   * bytes, then NSO staging. Not a hot path (§3). */
  Arena scratch;
  if (!arena_create(&scratch, EMULATOR_LOADER_ARENA_BYTES)) {
    return ERR(RESULT_OUT_OF_MEMORY, "emulator_load_program: loader arena");
  }

  ExeFS exefs;
  NPDM npdm;
  err = exefs_open(nca_section_source(&nca, (uint32_t)exefs_index), &scratch, &exefs);
  if (error_is_ok(err)) {
    const ExeFS_Entry* npdm_entry = exefs_find(&exefs, EXEFS_FILE_NPDM);
    if (!npdm_entry) {
      err = ERR(RESULT_NOT_FOUND, "emulator_load_program: ExeFS has no main.npdm");
    } else if (npdm_entry->size > NPDM_MAX_FILE_BYTES) {
      err = ERR(RESULT_INVALID_ARGUMENT, "emulator_load_program: main.npdm over size cap");
    } else {
      uint8_t* npdm_bytes = ARENA_ALLOC_ARRAY(&scratch, uint8_t, (size_t)npdm_entry->size);
      if (!npdm_bytes) {
        err = ERR(RESULT_OUT_OF_MEMORY, "emulator_load_program: loader arena exhausted");
      } else {
        err = exefs_read(&exefs, npdm_entry, 0, npdm_bytes, npdm_entry->size);
        if (error_is_ok(err)) err = npdm_parse(npdm_bytes, npdm_entry->size, &npdm);
      }
    }
  }

  /* Steps 2-4: the process. */
  if (error_is_ok(err)) {
    const Process_Bootstrap_Params params = {
        .exefs = &exefs,
        .npdm = &npdm,
        .vmm = emulator->vmm,
        .pages = &emulator->pages,
        .scratch = &scratch,
        .aslr_seed = aslr_seed,
    };
    err = process_bootstrap(&params, &emulator->process);
    if (!error_is_ok(err)) {
      /* The bootstrap unmapped its own; the pages it consumed come back
       * with the allocator (nothing else has allocated yet). */
      page_allocator_reset(&emulator->pages);
    }
  }
  arena_destroy(&scratch);
  if (!error_is_ok(err)) return err;

  err = process_enter_main_thread(&emulator->process, emulator->cpu_backend, emulator->cpu_state);
  if (!error_is_ok(err)) {
    process_teardown(&emulator->process, emulator->vmm, &emulator->pages);
    page_allocator_reset(&emulator->pages);
    return err;
  }
  const uint32_t main_core = emulator->process.npdm.main_thread_core_number < SCHEDULER_VIRTUAL_CORE_COUNT
      ? emulator->process.npdm.main_thread_core_number : 0u;
  err = scheduler_add_thread(&emulator->scheduler, emulator->cpu_state,
      emulator->process.npdm.main_thread_priority, main_core,
      SCHEDULER_ALL_CORE_AFFINITY, &emulator->main_thread_scheduler_id);
  if (!error_is_ok(err)) {
    process_teardown(&emulator->process, emulator->vmm, &emulator->pages);
    page_allocator_reset(&emulator->pages);
    return err;
  }
  emulator->program_loaded = true;
  log_info("[emulator] program '%s' loaded; main thread armed at pc=0x%010llx",
           emulator->process.npdm.name, (unsigned long long)emulator->process.entry_point);
  return OK;
}


Error emulator_load_nro(Emulator *emulator, const Byte_Source *nro_source,
                        uint64_t aslr_seed) {
  if (!emulator || !nro_source) {
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_load_nro: NULL argument");
  }
  if (emulator->program_loaded) {
    return ERR(RESULT_INVALID_ARGUMENT,
               "emulator_load_nro: a program is already loaded; unload it first");
  }
  NRO nro;
  Error err = nro_open(nro_source, &nro);
  if (!error_is_ok(err)) return err;

  err = process_bootstrap_nro(&nro, emulator->vmm, &emulator->pages,
                              aslr_seed, &emulator->process);
  if (!error_is_ok(err)) {
    page_allocator_reset(&emulator->pages);
    return err;
  }
  err = process_enter_main_thread(&emulator->process, emulator->cpu_backend,
                                  emulator->cpu_state);
  if (!error_is_ok(err)) {
    process_teardown(&emulator->process, emulator->vmm, &emulator->pages);
    page_allocator_reset(&emulator->pages);
    return err;
  }
  const uint32_t core = emulator->process.npdm.main_thread_core_number <
                                SCHEDULER_VIRTUAL_CORE_COUNT
                            ? emulator->process.npdm.main_thread_core_number : 0u;
  err = scheduler_add_thread(&emulator->scheduler, emulator->cpu_state,
                             emulator->process.npdm.main_thread_priority, core,
                             SCHEDULER_ALL_CORE_AFFINITY,
                             &emulator->main_thread_scheduler_id);
  if (!error_is_ok(err)) {
    process_teardown(&emulator->process, emulator->vmm, &emulator->pages);
    page_allocator_reset(&emulator->pages);
    return err;
  }
  emulator->program_loaded = true;
  log_info("[emulator] NRO homebrew loaded; main thread armed at pc=0x%010llx",
           (unsigned long long)emulator->process.entry_point);
  return OK;
}

void emulator_unload_program(Emulator* emulator) {
  if (!emulator || !emulator->program_loaded) return;
  (void)scheduler_remove_thread(&emulator->scheduler, emulator->main_thread_scheduler_id);
  emulator->main_thread_scheduler_id = 0;
  process_teardown(&emulator->process, emulator->vmm, &emulator->pages);
  page_allocator_reset(&emulator->pages);
  emulator->program_loaded = false;
}

CPU_ExitReason emulator_run(Emulator* emulator, uint64_t cycle_budget) {
  SWITCH_ASSERT_ALWAYS(emulator != NULL, "emulator_run: emulator is NULL");
  if (!emulator->program_loaded) return CPU_EXIT_CYCLES_ELAPSED;
  return scheduler_tick_budget(&emulator->scheduler, cycle_budget).last_exit_reason;
}

Error emulator_set_paused(Emulator *emulator,bool paused) {
  if (!emulator || !emulator->cpu_backend)
    return ERR(RESULT_INVALID_ARGUMENT, "emulator_set_paused: invalid emulator");
  return scheduler_set_paused(&emulator->scheduler, paused);
}

bool emulator_is_paused(const Emulator *emulator) {
  return emulator && scheduler_is_paused(&emulator->scheduler);
}

CPU_ExitReason emulator_step(Emulator* emulator) {
  SWITCH_ASSERT_ALWAYS(emulator != NULL, "emulator_step: emulator is NULL");
  return emulator->cpu_backend->step(emulator->cpu_state);
}

const char* emulator_backend_name(const Emulator* emulator) {
  if (!emulator || !emulator->cpu_backend) return "<none>";
  return emulator->cpu_backend->name;
}
