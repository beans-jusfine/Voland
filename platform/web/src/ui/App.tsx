/**
 * Voland web shell: HOME-style launcher, emulator display, homebrew loader and event log.
 */
import { For, createEffect, createSignal, onCleanup, onMount } from "solid-js";
import type { LogEntry } from "../log";
import { appendLogLine, getFps, getLogHistory, getStatus, subscribeFps, subscribeLogs, subscribeStatus } from "../log";

interface AppProps {
  readonly adapterLabel: string;
  readonly cpuBackend: string;
  readonly guestRamMiB: number;
  readonly onLoadNro: (file: File) => void;
}

type Page = "home" | "library" | "emulator" | "settings";

const demos = [
  { name: "Bouncing Square", file: "voland-bouncing.nro", detail: "Continuous animation · 96 × 54", art: "bounce", tag: "SMOOTH 60 FPS" },
  { name: "Calibration Lab", file: "voland-calibration.nro", detail: "Framebuffer and color test", art: "calibration", tag: "DISPLAY TEST" },
  { name: "Particle Storm", file: "voland-particle-storm.nro", detail: "GPU and framebuffer stress test", art: "particles", tag: "STRESS TEST" },
  { name: "Visual Diagnostic", file: "voland-visual-diagnostic.nro", detail: "Moving shapes and color patterns", art: "diagnostic", tag: "DIAGNOSTIC" },
];

function App(props: AppProps) {
  const [page, setPage] = createSignal<Page>("home");
  const [logs, setLogs] = createSignal<readonly LogEntry[]>(getLogHistory().slice());
  const [status, setStatusSig] = createSignal(getStatus());
  const [selectedNro, setSelectedNro] = createSignal<File | null>(null);
  const [showFps, setShowFps] = createSignal(false);
  const [fps, setFps] = createSignal(getFps());
  const [launching, setLaunching] = createSignal("");

  onMount(() => {
    const offLogs = subscribeLogs((entry) => {
      setLogs((prev) => {
        const next = prev.length >= 500 ? prev.slice(1) : prev.slice();
        next.push(entry);
        return next;
      });
    });
    const offStatus = subscribeStatus((text) => setStatusSig(text));
    const offFps = subscribeFps((value) => setFps(value));
    onCleanup(() => { offLogs(); offStatus(); offFps(); });
  });

  createEffect(() => {
    if (page() !== "emulator") return;
    const canvas = document.getElementById("game");
    const screen = document.querySelector(".voland-screen");
    if (canvas && screen && canvas.parentElement !== screen) screen.appendChild(canvas);
  });

  async function launchDemo(file: string): Promise<void> {
    if (launching()) return;
    setLaunching(file);
    try {
      const url = new URL(`${import.meta.env.BASE_URL}homebrew/${file}`, location.origin);
      const response = await fetch(url);
      if (!response.ok) throw new Error(`Download failed (HTTP ${response.status})`);
      const blob = await response.blob();
      props.onLoadNro(new File([blob], file, { type: "application/octet-stream" }));
      appendLogLine("info", `launcher: starting ${file}`);
      setPage("emulator");
    } catch (error) {
      appendLogLine("error", `launcher: ${(error as Error).message}`);
    } finally {
      setLaunching("");
    }
  }

  function loadSelected(): void {
    const file = selectedNro();
    if (file) {
      props.onLoadNro(file);
      setPage("emulator");
    }
  }

  return (
    <div class="voland-shell">
      <header class="voland-header">
        <button class="voland-brand" type="button" onClick={() => setPage("home")} aria-label="Voland home">
          <span class="voland-mark">V</span>
          <span class="voland-brand-copy"><span class="voland-title">VOLAND</span><span class="voland-subtitle">WEB CONSOLE</span></span>
        </button>
        <nav class="voland-nav" aria-label="Main navigation">
          <button classList={{ "is-current": page() === "home" }} onClick={() => setPage("home")}>HOME</button>
          <button classList={{ "is-current": page() === "library" }} onClick={() => setPage("library")}>LIBRARY</button>
          <button classList={{ "is-current": page() === "emulator" }} onClick={() => setPage("emulator")}>EMULATOR</button>
          <button classList={{ "is-current": page() === "settings" }} onClick={() => setPage("settings")}>SETTINGS</button>
        </nav>
        <div class="voland-header-right"><span class="voland-online-dot" />{status()}</div>
      </header>

      {page() === "home" && (
        <main class="voland-home">
          <section class="voland-welcome">
            <div><p class="voland-eyebrow">YOUR CONSOLE. YOUR LIBRARY.</p><h1>Welcome to <span>Voland.</span></h1><p class="voland-welcome-copy">Pick up where you left off, or launch something new.</p></div>
            <div class="voland-clock-card"><span>CORE STATUS</span><strong>ONLINE</strong><small>{props.cpuBackend}</small></div>
          </section>
          <section class="voland-feature">
            <div class="voland-feature-art"><div class="voland-feature-orbit orbit-one" /><div class="voland-feature-orbit orbit-two" /><div class="voland-feature-cube">V</div><span class="voland-feature-label">VOLAND HOME</span></div>
            <div class="voland-feature-copy"><p class="voland-eyebrow">FEATURED DEMO</p><h2>Bouncing Square</h2><p>Our first continuously animated homebrew demo. Smooth motion, real guest framebuffer output.</p><div class="voland-feature-meta"><span>96 × 54 FRAMEBUFFER</span><span>58–60 FPS</span></div><button class="voland-primary-button" disabled={!!launching()} onClick={() => void launchDemo("voland-bouncing.nro")}>{launching() === "voland-bouncing.nro" ? "STARTING…" : "▶  PLAY DEMO"}</button></div>
          </section>
          <section class="voland-section">
            <div class="voland-section-title"><div><p class="voland-eyebrow">QUICK ACCESS</p><h2>Recently available</h2></div><button class="voland-text-button" onClick={() => setPage("library")}>VIEW LIBRARY <span>→</span></button></div>
            <div class="voland-game-grid">
              <For each={demos.slice(1, 4)}>{(demo) => <button class="voland-game-card" disabled={!!launching()} onClick={() => void launchDemo(demo.file)}><span class={`voland-game-art art-${demo.art}`}><span>{demo.art === "calibration" ? "◉" : demo.art === "particles" ? "✳" : "▧"}</span></span><span class="voland-game-info"><strong>{demo.name}</strong><small>{demo.detail}</small></span></button>}</For>
            </div>
          </section>
          <footer class="voland-home-footer"><span>VOLAND · WEB CONSOLE</span><span>EARLY ACCESS BUILD</span></footer>
        </main>
      )}

      {page() === "library" && (
        <main class="voland-page">
          <div class="voland-page-heading"><div><p class="voland-eyebrow">YOUR COLLECTION</p><h1>Game Library</h1><p>Launch built-in demos or load your own homebrew.</p></div><button class="voland-primary-button" onClick={() => setPage("emulator")}>＋ LOAD NRO</button></div>
          <div class="voland-library-grid"><For each={demos}>{(demo) => <button class="voland-library-card" disabled={!!launching()} onClick={() => void launchDemo(demo.file)}><span class={`voland-library-art art-${demo.art}`}><b>{demo.art === "bounce" ? "□" : demo.art === "calibration" ? "◉" : demo.art === "particles" ? "✳" : "▧"}</b><small>{demo.tag}</small></span><strong>{demo.name}</strong><span>{launching() === demo.file ? "Starting…" : demo.detail}</span></button>}</For></div>
          <div class="voland-empty-library"><span>＋</span><div><strong>Have your own homebrew?</strong><p>Open the emulator page to load an .nro file from your device.</p></div><button class="voland-secondary-button" onClick={() => setPage("emulator")}>OPEN LOADER</button></div>
        </main>
      )}

      {page() === "emulator" && (
        <main class="voland-emulator-page">
          <section class="voland-display-column">
            <div class="voland-section-heading"><div><p class="voland-eyebrow">LIVE OUTPUT</p><h1>Emulator</h1><p>Guest framebuffer output appears here.</p></div><div class="voland-display-controls"><button class={`voland-fps-toggle${showFps() ? " is-active" : ""}`} type="button" aria-pressed={showFps()} onClick={() => setShowFps((value) => !value)}>FPS {showFps() ? "ON" : "OFF"}</button><span class="voland-display-state">GPU</span></div></div>
            <div class="voland-display-frame"><div class="voland-screen"><div class="voland-screen-placeholder">Waiting for display output</div>{showFps() && <div class="voland-fps-counter">FPS <strong>{fps()}</strong></div>}</div></div>
            <div class="voland-display-caption"><span>DISPLAY</span><span>1280 × 720</span></div>
            <div class="voland-facts"><div><dt>CPU backend</dt><dd>{props.cpuBackend}</dd></div><div><dt>GPU adapter</dt><dd>{props.adapterLabel}</dd></div><div><dt>Guest RAM</dt><dd>{props.guestRamMiB} MiB</dd></div></div>
          </section>
          <aside class="voland-sidebar">
            <section class="voland-panel voland-loader"><div class="voland-panel-heading"><h2>Homebrew loader</h2><span>NRO</span></div><label class="voland-file-label" for="nro-file">Choose a homebrew file</label><input id="nro-file" class="voland-file-input" type="file" accept=".nro,application/octet-stream" onChange={(event) => setSelectedNro(event.currentTarget.files?.[0] ?? null)} /><div class="voland-file-name">{selectedNro()?.name ?? "No NRO selected"}</div><button class="voland-load-button" type="button" disabled={!selectedNro()} onClick={loadSelected}>Load NRO</button><For each={demos}>{(demo) => <a class="voland-sample-link" href={`${import.meta.env.BASE_URL}homebrew/${demo.file}`} download={demo.file}>Download {demo.name}</a>}</For><p class="voland-loader-note">NRO homebrew runs in the guest CPU. Your files stay in this browser session.</p></section>
            <section class="voland-panel voland-log"><div class="voland-panel-heading"><h2>Event log</h2><span>LIVE</span></div><div class="voland-log-scroll"><For each={logs()}>{(entry) => <p class={`log-line log-${entry.level}`}>{`[${entry.level.toUpperCase().padEnd(5, " ")}] ${entry.message}`}</p>}</For></div></section>
          </aside>
        </main>
      )}

      {page() === "settings" && (
        <main class="voland-page"><div class="voland-page-heading"><div><p class="voland-eyebrow">CONSOLE CONFIGURATION</p><h1>Settings</h1><p>Current runtime information and build capabilities.</p></div></div><div class="voland-settings-grid"><div class="voland-setting-card"><span>PROCESSOR</span><strong>{props.cpuBackend}</strong><small>Active guest CPU backend</small></div><div class="voland-setting-card"><span>GRAPHICS</span><strong>{props.adapterLabel}</strong><small>Active WebGPU adapter</small></div><div class="voland-setting-card"><span>MEMORY</span><strong>{props.guestRamMiB} MiB</strong><small>Allocated guest RAM</small></div><div class="voland-setting-card"><span>RUNTIME</span><strong>{status()}</strong><small>Worker and emulator status</small></div></div><p class="voland-settings-note">More console settings will appear as Voland's system emulation develops.</p></main>
      )}
    </div>
  );
}
export default App;
