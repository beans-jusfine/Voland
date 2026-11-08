/**
 * platform/web/src/ui/App.tsx
 *
 * SolidJS shell mounted once the boot sequence reports all workers ready.
 * Phase 0 scope: a minimal landing surface that proves the reactive runtime
 * is alive and that the log/status streams keep flowing into the UI.
 * Phase 1+ will flesh this out into the game library + settings screens.
 */
import { For, createSignal, onCleanup, onMount } from "solid-js";
import type { LogEntry } from "../log";
import { getLogHistory, getStatus, subscribeLogs, subscribeStatus } from "../log";

interface AppProps {
  readonly adapterLabel: string;
  readonly cpuBackend:   string;
  readonly guestRamMiB:  number;
  readonly onLoadNro: (file: File) => void;
}

function App(props: AppProps) {
  const [logs, setLogs] = createSignal<readonly LogEntry[]>(getLogHistory().slice());
  const [status, setStatusSig] = createSignal(getStatus());
  const [selectedNro, setSelectedNro] = createSignal<File | null>(null);

  onMount(() => {
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
          <span class="voland-subtitle">Phase 0 · skeleton</span>
        </div>
        <div class="voland-status">{status()}</div>
      </header>

      <section class="voland-main">
        <div class="voland-hero">
          <h2>Runtime online.</h2>
          <p>Both workers initialised. Choose a Switch homebrew NRO to send it to the emulator core.</p>
          <div class="voland-loader">
            <label class="voland-file-label" for="nro-file">Choose homebrew (.nro)</label>
            <input id="nro-file" class="voland-file-input" type="file" accept=".nro,application/octet-stream"
              onChange={(event) => setSelectedNro(event.currentTarget.files?.[0] ?? null)} />
            <div class="voland-file-name">{selectedNro()?.name ?? "No NRO selected"}</div>
            <button class="voland-load-button" type="button" disabled={!selectedNro()}
              onClick={() => { const file = selectedNro(); if (file) props.onLoadNro(file); }}>Load NRO</button>
            <a class="voland-sample-link" href={`${import.meta.env.BASE_URL}homebrew/voland-demo.nro`} download="voland-demo.nro">Download Voland demo homebrew</a>
            <p class="voland-loader-note">The demo executes a tiny ARM64 test loop; it is a loader smoke-test, not a graphical game.</p>
          </div>
          <dl class="voland-facts">
            <div><dt>CPU backend</dt><dd>{props.cpuBackend}</dd></div>
            <div><dt>GPU adapter</dt><dd>{props.adapterLabel}</dd></div>
            <div><dt>Guest RAM</dt><dd>{props.guestRamMiB} MiB</dd></div>
          </dl>
        </div>

        <aside class="voland-log">
          <header>event log</header>
          <div class="voland-log-scroll">
            <For each={logs()}>
              {(entry) => (
                <p class={`log-line log-${entry.level}`}>
                  {`[${entry.level.toUpperCase().padEnd(5, " ")}] ${entry.message}`}
                </p>
              )}
            </For>
          </div>
        </aside>
      </section>
    </div>
  );
}

export default App;
