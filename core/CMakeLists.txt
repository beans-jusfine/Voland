find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(A64_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated/a64")

add_custom_command(
  OUTPUT
    "${A64_GENERATED_DIR}/decoder_generated.c"
    "${A64_GENERATED_DIR}/a64_decoder_vectors_generated.h"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${A64_GENERATED_DIR}"
  COMMAND ${Python3_EXECUTABLE}
          "${PROJECT_SOURCE_DIR}/tools/a64/generate_decoder.py"
          --output-dir "${A64_GENERATED_DIR}"
  DEPENDS
    "${PROJECT_SOURCE_DIR}/tools/a64/generate_decoder.py"
    "${PROJECT_SOURCE_DIR}/tools/a64/generate_decoder_impl.py.gz"
    "${PROJECT_SOURCE_DIR}/core/cpu/a64/spec/AARCHMRS-2026-09-A64-supported.json.gz"
  VERBATIM
)
add_custom_target(voland_a64_decoder_generated DEPENDS
  "${A64_GENERATED_DIR}/decoder_generated.c"
  "${A64_GENERATED_DIR}/a64_decoder_vectors_generated.h"
)

# Builds the platform-agnostic C11 emulator core. See docs/DESIGN.md section 2.

set(SWITCH_CORE_SOURCES
  common/log.c
  common/arena.c
  common/layout.c
  common/input_region.c
  common/vmm.c
  cpu/cpu.c
  ${A64_GENERATED_DIR}/decoder_generated.c
  cpu/backends/noop/noop.c
  cpu/backends/interpreter/interpreter.c
  hle/hle.c
  hle/kernel/page_allocator.c
  hle/kernel/address_space.c
  hle/kernel/process.c
  hle/kernel/handle_table.c
  hle/kernel/svc_memory.c
  hle/kernel/ipc.c
  hle/kernel/svc_ipc.c
  hle/kernel/tls.c
  hle/kernel/thread.c
  hle/kernel/scheduler.c
  hle/services/sm/sm.c
  hle/loader/byte_source.c
  hle/loader/nca_parse.c
  hle/loader/system_software.c
  hle/loader/system_content.c
  hle/loader/cnmt.c
  hle/loader/meta_nca.c
  hle/loader/exefs.c
  hle/loader/romfs.c
  hle/loader/npdm.c
  hle/loader/nso.c
  hle/loader/nro.c
  emulator.c
)

#
# The ONLY third-party code in core/: single-file LZ4 (BSD-2) for NSO
# segment decompression (docs/DESIGN.md �12, �26). Built with every
# allocating entry point compiled out, so the object contains no
# malloc/calloc/free - the loader uses LZ4_decompress_safe over arena
# storage and tests use the stack-state LZ4_compress_default.
#
set(SWITCH_CORE_THIRD_PARTY_SOURCES
  third_party/lz4/lz4.c
)
# Upstream lz4.c defines one non-static helper without a prototype
# (LZ4_loadDict_internal); relax -Wmissing-prototypes for that file only.
# Every other project warning stays on and lz4.c is clean under them.
set_source_files_properties(${SWITCH_CORE_THIRD_PARTY_SOURCES} PROPERTIES
  COMPILE_DEFINITIONS "LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION;LZ4_HEAPMODE=0"
  COMPILE_OPTIONS "$<$<C_COMPILER_ID:GNU,Clang>:-Wno-missing-prototypes>"
)

set(SWITCH_CORE_PUBLIC_HEADERS
  common/log.h
  common/arena.h
  common/assert.h
  common/result.h
  common/layout.h
  common/input_region.h
  common/vmm.h
  cpu/cpu.h
  cpu/a64/insn.h
  cpu/a64/decoder.h
  cpu/backends/noop/noop.h
  cpu/backends/interpreter/interpreter.h
  hle/hle.h
  hle/kernel/page_allocator.h
  hle/kernel/address_space.h
  hle/kernel/process.h
  hle/kernel/handle_table.h
  hle/kernel/svc_memory.h
  hle/kernel/ipc.h
  hle/kernel/svc_ipc.h
  hle/kernel/tls.h
  hle/kernel/thread.h
  hle/kernel/scheduler.h
  hle/services/sm/sm.h
  hle/loader/byte_source.h
  hle/loader/nca_parse.h
  hle/loader/system_software.h
  hle/loader/system_content.h
  hle/loader/cnmt.h
  hle/loader/meta_nca.h
  hle/loader/exefs.h
  hle/loader/romfs.h
  hle/loader/npdm.h
  hle/loader/nso.h
  hle/loader/nro.h
  emulator.h
)

add_library(switch_core STATIC ${SWITCH_CORE_SOURCES} ${SWITCH_CORE_THIRD_PARTY_SOURCES})
add_dependencies(switch_core voland_a64_decoder_generated)

# MSVC gates its C11 <stdatomic.h> implementation behind an experimental flag.
# PUBLIC propagates it to tests and consumers that include public atomic headers.
target_compile_options(switch_core PUBLIC "$<$<C_COMPILER_ID:MSVC>:/experimental:c11atomics>")

target_include_directories(switch_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<BUILD_INTERFACE:${A64_GENERATED_DIR}>
)

#
# Backend selection. Every backend obeys CPU_Backend; a single define wires
# cpu_get_active_backend() to the compiled-in choice. Additional backends
# (interpreter, ballistic) will add their own subdirectory and define.
#
if(CPU_BACKEND STREQUAL "noop")
  target_compile_definitions(switch_core PUBLIC SWITCH_CPU_BACKEND_NOOP=1)
elseif(CPU_BACKEND STREQUAL "interpreter")
  target_compile_definitions(switch_core PUBLIC SWITCH_CPU_BACKEND_INTERPRETER=1)
elseif(CPU_BACKEND STREQUAL "ballistic")
  message(FATAL_ERROR "ballistic backend is a Phase 5 deliverable, not yet built (§10)")
else()
  message(FATAL_ERROR "Unknown CPU_BACKEND '${CPU_BACKEND}'")
endif()

#
# Web build: expose the core as a WASM module directly from this target.
#
if(EMSCRIPTEN)
  target_compile_definitions(switch_core PUBLIC
    LAYOUT_LINEAR_MEMORY_PAGES=${VOLAND_WEB_MEMORY_PAGES}
    LAYOUT_GUEST_RAM_APPLICATION_POOL_SIZE=${VOLAND_WEB_GUEST_APP_POOL_BYTES}ULL
  )
  add_executable(switch_core_wasm stubs/wasm_entry.c)
  target_link_libraries(switch_core_wasm PRIVATE switch_core)
  set_target_properties(switch_core_wasm PROPERTIES
    OUTPUT_NAME "switch_core"
    SUFFIX ".js"
  )
endif()
