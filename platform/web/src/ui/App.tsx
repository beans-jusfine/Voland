/**
 * Voland web shell: dedicated emulator display, homebrew loader and event log.
 */
import { For, createSignal, onCleanup, onMount } from "solid-js";
import type { LogEntry } from "../log";
import { getLogHistory, getStatus, subscribeLogs, subscribeStatus } from "../log";

interface AppProps {
  readonly adapterLabel: string;
  readonly cpuBackend: string;
  readonly guestRamMiB: number;
  readonly onLoadNro: (file: File) => void;
}

function App(props: AppProps) {
  const [logs, setLogs] = createSignal<readonly LogEntry[]>(getLogHistory().slice());
  const [status, setStatusSig] = createSignal(getStatus());
  const [selectedNro, setSelectedNro] = createSignal<File | null>(null);

  onMount(() => {
    // The canvas is transferred to the GPU worker during boot. Move that same
    // canvas into the display frame after the shell mounts; do not recreate it.
    const canvas = document.getElementById("game");
    const screen = document.querySelector(".voland-screen");
    if (canvas && screen) screen.appendChild(canvas);

    const offLogs = subscribeLogs((entry) => {
      setLogs((prev) => {
        const next = prev.length >= 500 ? prev.slice(1) : prev.slice();
        next.push(entry);
        return next;
      });
    });
    const offStatus = subscribeStatus((text) => setStatusSig(text));
    onCleanup(() => { offLogs(); offStatus(); });
  });

  return (
    <div class="voland-shell">
      <header class="voland-header">
        <div class="voland-brand">
          <span class="voland-title">Voland</span>
          <span class="voland-subtitle">Nintendo Switch emulator</span>
        </div>
        <div class="voland-status">{status()}</div>
      </header>

      <main class="voland-main">
        <section class="voland-display-column">
          <div class="voland-section-heading">
            <div>
              <h1>Emulator display</h1>
              <p>Newly published RGBA8 frames from shared WASM memory appear here.</p>
            </div>
            <span class="voland-display-state">GPU</span>
          </div>
          <div class="voland-display-frame">
            <div class="voland-screen">
              <div class="voland-screen-placeholder">Waiting for display output</div>
            </div>
          </div>
          <div class="voland-display-caption">
            <span>DISPLAY</span>
            <span>1280 × 720</span>
          </div>
          <div class="voland-facts">
            <div><dt>CPU backend</dt><dd>{props.cpuBackend}</dd></div>
            <div><dt>GPU adapter</dt><dd>{props.adapterLabel}</dd></div>
            <div><dt>Guest RAM</dt><dd>{props.guestRamMiB} MiB</dd></div>
          </div>
        </section>

        <aside class="voland-sidebar">
          <section class="voland-panel voland-loader">
            <div class="voland-panel-heading">
              <h2>Homebrew loader</h2>
              <span>NRO</span>
            </div>
            <label class="voland-file-label" for="nro-file">Choose a homebrew file</label>
            <input id="nro-file" class="voland-file-input" type="file" accept=".nro,application/octet-stream"
              onChange={(event) => setSelectedNro(event.currentTarget.files?.[0] ?? null)} />
            <div class="voland-file-name">{selectedNro()?.name ?? "No NRO selected"}</div>
            <button class="voland-load-button" type="button" disabled={!selectedNro()}
              onClick={() => { const file = selectedNro(); if (file) props.onLoadNro(file); }}>Load NRO</button>
            <a class="voland-sample-link" href={`${import.meta.env.BASE_URL}homebrew/voland-animation-demo.nro`} download="voland-animation-demo.nro">Download Voland animation demo NRO</a>
            <p class="voland-loader-note">Guest homebrew can publish a VFB1 descriptor (width, height, RGBA8 format) followed by its pixels in .data. Voland validates and copies each guest frame into shared memory for WebGPU presentation. Legacy 32×32 demos remain supported.</p>
          </section>

          <section class="voland-panel voland-log">
            <div class="voland-panel-heading">
              <h2>Event log</h2>
              <span>LIVE</span>
            </div>
            <div class="voland-log-scroll">
              <For each={logs()}>
                {(entry) => (
                  <p class={`log-line log-${entry.level}`}>
                    {`[${entry.level.toUpperCase().padEnd(5, " ")}] ${entry.message}`}
                  </p>
                )}
              </For>
            </div>
          </section>
        </aside>
      </main>
    </div>
  );
}

export default App;
