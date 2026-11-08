/**
 * CPU emulation worker (docs/DESIGN.md section 16). Owns the core WASM
 * module instance and, once the Phase 2 scheduler exists, the guest
 * thread scheduler loop (§7).
 *
 * Instantiation contract: `emcmake cmake --build` (CPU_BACKEND=noop by
 * default, §24) emits `switch_core.js` + `switch_core.wasm` as an
 * EXPORT_ES6 module. The build pipeline is expected to place (copy or
 * symlink) that output at `platform/web/public/core/switch_core.js` so
 * Vite serves it as a static asset; that copy step is CI plumbing, not
 * part of this design and not wired up in this repository yet. Until it
 * is, the dynamic import below fails and this worker reports a clear
 * "error" message instead of a fake "ready" - Phase 0's boot path is
 * meant to be exercised honestly, not stubbed into looking alive.
 */

import type { CpuBackendId, SwitchCoreExports } from "@bindings/core";
import { CPU_BACKEND_DISPLAY_NAMES } from "@bindings/core";
import { readMemoryLayout } from "@bindings/layout";
import type { CPUToMainMessage, MainToCPUMessage } from "@bindings/protocol";

const self: DedicatedWorkerGlobalScope =
  globalThis as unknown as DedicatedWorkerGlobalScope;

function log(level: "debug" | "info" | "warn" | "error", message: string): void {
  const msg: CPUToMainMessage = { type: "log", level, message };
  self.postMessage(msg);
}

/** Emscripten EXPORT_ES6+MODULARIZE factory shape: a default export that
 * takes an options object (here, the imported WebAssembly.Memory for
 * -sIMPORTED_MEMORY) and resolves to the instantiated module's exports. */
type CoreModuleFactory = (options: { wasmMemory: WebAssembly.Memory }) => Promise<SwitchCoreExports>;

const CORE_MODULE_URL = `${import.meta.env.BASE_URL}core/switch_core.js`;

async function loadCoreModule(memory: WebAssembly.Memory): Promise<SwitchCoreExports> {
  const factory = (await import(/* @vite-ignore */ CORE_MODULE_URL)).default as CoreModuleFactory;
  return factory({ wasmMemory: memory });
}

let core: SwitchCoreExports | null = null;
let wasmMemory: WebAssembly.Memory | null = null;
let paused = false;
let halted = false;
let tickScheduled = false;
const SCHEDULER_CYCLE_BUDGET = 10_000n;
const WORKER_TICK_INTERVAL_MS = 16;

function scheduleExecutionTick(): void {
  if (tickScheduled || halted || !core) return;
  tickScheduled = true;
  self.setTimeout(() => {
    tickScheduled = false;
    if (halted || !core) return;
    if (!paused) {
      const reason = core._emulator_run_ffi(SCHEDULER_CYCLE_BUDGET);
      if (reason === 4) log("warn", "scheduler stopped a guest thread after a CPU fault");
    }
    scheduleExecutionTick();
  }, WORKER_TICK_INTERVAL_MS);
}

async function init(memory: WebAssembly.Memory): Promise<void> {
  wasmMemory = memory;
  log("info", "cpu.worker initialising");

  try {
    core = await loadCoreModule(memory);
  } catch (e) {
    const err: CPUToMainMessage = {
      type: "error",
      message: `failed to load core module from ${CORE_MODULE_URL}: ${e instanceof Error ? e.message : String(e)}. ` +
        `Build it with emcmake cmake -B build-web && cmake --build build-web (docs/DESIGN.md §24), ` +
        `then copy build-web/core/switch_core.{js,wasm} to platform/web/public/core/.`,
    };
    self.postMessage(err);
    return;
  }

  if (core._emulator_create_ffi() === 0) {
    const err: CPUToMainMessage = { type: "error", message: "emulator_create_ffi failed" };
    self.postMessage(err);
    return;
  }

  // Unsigned: the i32 comes back signed for addresses at or above 2 GiB.
  const layoutPtr = core._layout_get_ffi() >>> 0;
  const layout = readMemoryLayout(memory, layoutPtr);
  const layoutMsg: CPUToMainMessage = { type: "layout", layout };
  self.postMessage(layoutMsg);

  const backendId = core._cpu_backend_id_ffi() as CpuBackendId;
  const backendName = CPU_BACKEND_DISPLAY_NAMES[backendId] ?? `unknown(${backendId})`;
  log("info", `guestRamSize=${layout.guestRamSize} bytes, backend=${backendName}`);

  const ready: CPUToMainMessage = {
    type: "ready",
    backendName,
    backendVersion: backendId === 1 ? "0.1.0-dev" : "1.0.0",
  };
  self.postMessage(ready);
  scheduleExecutionTick();
}

self.addEventListener("message", (event: MessageEvent<MainToCPUMessage>) => {
  const msg = event.data;

  if (msg.type === "init") {
    init(msg.memory).catch((e: unknown) => {
      const err: CPUToMainMessage = {
        type: "error",
        message: `init threw: ${e instanceof Error ? e.message : String(e)}`,
      };
      self.postMessage(err);
    });
    return;
  }

  if (msg.type === "pause" || msg.type === "resume") {
    if (!core) {
      log("warn", `${msg.type} ignored before core initialisation`);
      return;
    }
    const nextPaused = msg.type === "pause";
    if (core._emulator_set_paused_ffi(nextPaused ? 1 : 0) === 0) {
      log("error", `scheduler ${msg.type} request failed`);
      return;
    }
    paused = nextPaused;
    log("debug", `scheduler ${paused ? "paused" : "resumed"}`);
    return;
  }

  if (msg.type === "load-nro") {
    if (!core) { log("error", "cannot load NRO before the core is ready"); return; }
    if (msg.buffer.byteLength === 0 || msg.buffer.byteLength > 512 * 1024 * 1024) {
      log("error", `rejected ${msg.name}: file is empty or larger than 512 MiB`); return;
    }
    const ptr = core._malloc(msg.buffer.byteLength);
    if (!ptr) { log("error", `not enough WASM memory to stage ${msg.name}`); return; }
    try {
      if (!wasmMemory) throw new Error("shared WASM memory is unavailable");
      new Uint8Array(wasmMemory.buffer).set(new Uint8Array(msg.buffer), ptr >>> 0);
      const result = core._emulator_load_nro_ffi(ptr, msg.buffer.byteLength);
      if (result === 0) log("info", `loaded ${msg.name}; main homebrew thread is running`);
      else log("error", `could not load ${msg.name} (loader result ${result}); see core diagnostics above`);
    } finally { core._free(ptr); }
    return;
  }
  if (msg.type === "halt") {
    halted = true;
    tickScheduled = false;
    if (core) core._emulator_destroy_ffi();
    const haltedMessage: CPUToMainMessage = { type: "halted" };
    self.postMessage(haltedMessage);
    return;
  }
});
