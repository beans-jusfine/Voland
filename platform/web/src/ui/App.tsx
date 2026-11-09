/**
 * Voland web shell: HOME-style launcher, emulator display, homebrew loader and event log.
 */
import { For, createEffect, createSignal, onCleanup, onMount } from "solid-js";
import type { LogEntry } from "../log";
import { parseKeyFile } from "../keys";
import { inspectGamePackage, type PackageInventory } from "../packages";
import { appendLogLine, getFps, getLogHistory, getMemoryStats, getStatus, subscribeFps, subscribeLogs, subscribeMemoryStats, subscribeStatus } from "../log";

interface AppProps {
  readonly adapterLabel: string;
  readonly cpuBackend: string;
  readonly guestRamMiB: number;
  readonly onLoadNro: (file: File) => void;
  readonly onStopEmulator: () => void;
}

type Page = "home" | "library" | "emulator" | "settings";

const demos = [
  { name: "Bouncing Square", file: "voland-bouncing.nro", detail: "Continuous animation · 96 × 54", art: "bounce", tag: "SMOOTH 60 FPS" },
  { name: "Calibration Lab", file: "voland-calibration.nro", detail: "Framebuffer and color test", art: "calibration", tag: "DISPLAY TEST" },
  { name: "Particle Storm", file: "voland-particle-storm.nro", detail: "GPU and framebuffer stress test", art: "particles", tag: "STRESS TEST" },
  { name: "Visual Diagnostic", file: "voland-visual-diagnostic.nro", detail: "Moving shapes and color patterns", art: "diagnostic", tag: "DIAGNOSTIC" },
];


interface FirmwareManifest {
  readonly fileCount: number;
  readonly totalBytes: number;
  readonly importedAt: string;
  readonly sourceFolder: string;
}

interface ImportedKeyFile {
  readonly name: string;
  readonly byteLength: number;
  readonly entryCount: number;
  /** Parsed key bytes stay in memory only; never logged or persisted. */
  readonly keys: ReadonlyMap<string, Uint8Array>;
}

function inspectKeyFile(file: File, contents: string): ImportedKeyFile {
  if (file.size === 0 || contents.trim().length === 0) {
    throw new Error("The selected key file is empty.");
  }
  if (file.size > 2 * 1024 * 1024) {
    throw new Error("Key files larger than 2 MiB are not supported.");
  }
  const keys = parseKeyFile(contents);
  return { name: file.name, byteLength: file.size, entryCount: keys.size, keys };
}

async function firmwareDirectory(): Promise<FileSystemDirectoryHandle> {
  if (!navigator.storage || typeof navigator.storage.getDirectory !== "function") {
    throw new Error("This browser does not support local private file storage (OPFS).");
  }
  const root = await navigator.storage.getDirectory();
  const voland = await root.getDirectoryHandle("voland", { create: true });
  const system = await voland.getDirectoryHandle("system", { create: true });
  return system.getDirectoryHandle("firmware", { create: true });
}

async function readFirmwareManifest(): Promise<FirmwareManifest | null> {
  try {
    const root = await navigator.storage.getDirectory();
    const voland = await root.getDirectoryHandle("voland");
    const system = await voland.getDirectoryHandle("system");
    const handle = await system.getFileHandle("firmware-manifest.json");
    return JSON.parse(await (await handle.getFile()).text()) as FirmwareManifest;
  } catch {
    return null;
  }
}

function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 ** 2) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 ** 3) return `${(bytes / (1024 ** 2)).toFixed(1)} MB`;
  return `${(bytes / (1024 ** 3)).toFixed(2)} GB`;
}

async function storeFirmwareFiles(files: FileList): Promise<FirmwareManifest> {
  const selected = Array.from(files);
  if (!selected.length) throw new Error("The selected folder contains no files.");
  const totalBytes = selected.reduce((sum, file) => sum + file.size, 0);
  const estimate = await navigator.storage.estimate();
  if (estimate.quota !== undefined && estimate.usage !== undefined &&
      totalBytes > estimate.quota - estimate.usage) {
    throw new Error("There may not be enough browser storage space for this firmware dump.");
  }
  const target = await firmwareDirectory();
  let sourceFolder = "Firmware folder";
  for (const file of selected) {
    const parts = (file.webkitRelativePath || file.name).split("/")
      .filter((part) => part && part !== "." && part !== "..");
    if (parts.length > 1) {
      sourceFolder = parts[0];
      parts.shift();
    }
    if (!parts.length) continue;
    const filename = parts.pop()!;
    let directory = target;
    for (const part of parts) directory = await directory.getDirectoryHandle(part, { create: true });
    const handle = await directory.getFileHandle(filename, { create: true });
    const writable = await handle.createWritable();
    await writable.write(file);
    await writable.close();
  }
  const manifest: FirmwareManifest = {
    fileCount: selected.length,
    totalBytes,
    importedAt: new Date().toISOString(),
    sourceFolder,
  };
  const root = await navigator.storage.getDirectory();
  const voland = await root.getDirectoryHandle("voland", { create: true });
  const system = await voland.getDirectoryHandle("system", { create: true });
  const handle = await system.getFileHandle("firmware-manifest.json", { create: true });
  const writable = await handle.createWritable();
  await writable.write(JSON.stringify(manifest, null, 2));
  await writable.close();
  return manifest;
}

function App(props: AppProps) {
  const [page, setPage] = createSignal<Page>("home");
  const [logs, setLogs] = createSignal<readonly LogEntry[]>(getLogHistory().slice());
  const [status, setStatusSig] = createSignal(getStatus());
  const [selectedNro, setSelectedNro] = createSignal<File | null>(null);
  const [showFps, setShowFps] = createSignal(false);
  const [showGoHome, setShowGoHome] = createSignal(false);
  const [fps, setFps] = createSignal(getFps());
  const [memoryStats, setMemoryStats] = createSignal(getMemoryStats());
  const [launching, setLaunching] = createSignal("");
  const [emulatorStopped, setEmulatorStopped] = createSignal(false);
  const [firmwareInfo, setFirmwareInfo] = createSignal<FirmwareManifest | null>(null);
  const [firmwareStatus, setFirmwareStatus] = createSignal("No firmware folder imported.");
  const [firmwareBusy, setFirmwareBusy] = createSignal(false);
  const [prodKeyFile, setProdKeyFile] = createSignal<ImportedKeyFile | null>(null);
  const [titleKeyFile, setTitleKeyFile] = createSignal<ImportedKeyFile | null>(null);
  const [keyImportStatus, setKeyImportStatus] = createSignal("No key files loaded. Encrypted game loading is not available yet.");
  const [packageInfo, setPackageInfo] = createSignal<PackageInventory | null>(null);
  const [packageName, setPackageName] = createSignal("");
  const [packageStatus, setPackageStatus] = createSignal("Select an NSP or XCI dump to inspect its contents locally.");
  const [packageBusy, setPackageBusy] = createSignal(false);
  let firmwarePicker: HTMLInputElement | undefined;
  let prodKeyPicker: HTMLInputElement | undefined;
  let titleKeyPicker: HTMLInputElement | undefined;
  let packagePicker: HTMLInputElement | undefined;

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
    const offMemory = subscribeMemoryStats((stats) => setMemoryStats(stats));
    // Load previously stored firmware info from OPFS
    ;(async () => { const manifest = await readFirmwareManifest(); if (manifest) setFirmwareInfo(manifest); })();
    onCleanup(() => { offLogs(); offStatus(); offFps(); offMemory(); });
  });

  createEffect(() => {
    if (page() !== "emulator") return;
    const canvas = document.getElementById("game");
    const screen = document.querySelector(".voland-screen");
    if (canvas && screen && canvas.parentElement !== screen) screen.appendChild(canvas);
  });

  async function launchDemo(file: string): Promise<void> {
    if (launching() || emulatorStopped()) return;
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


  async function importKeyFile(kind: "prod" | "title", file: File | undefined): Promise<void> {
    if (!file) return;
    try {
      const imported = inspectKeyFile(file, await file.text());
      if (kind === "prod") setProdKeyFile(imported);
      else setTitleKeyFile(imported);
      const prodReady = kind === "prod" || !!prodKeyFile();
      const titleReady = kind === "title" || !!titleKeyFile();
      setKeyImportStatus(prodReady && titleReady
        ? "Both key files are loaded in memory. The game decryption pipeline is not connected yet."
        : "Key file loaded in memory. Import the other key file to complete the pair.");
      appendLogLine("info", `keys: loaded ${kind}.keys into this session (${imported.entryCount} entries)`);
    } catch (error) {
      setKeyImportStatus((error as Error).message);
      appendLogLine("error", `keys: ${kind}.keys import failed: ${(error as Error).message}`);
    } finally {
      if (kind === "prod" && prodKeyPicker) prodKeyPicker.value = "";
      if (kind === "title" && titleKeyPicker) titleKeyPicker.value = "";
    }
  }

  async function inspectPackageFile(file: File | undefined): Promise<void> {
    if (!file || packageBusy()) return;
    setPackageBusy(true);
    setPackageInfo(null);
    setPackageName(file.name);
    setPackageStatus("Reading package headers and file tables…");
    try {
      const inventory = await inspectGamePackage(file, file.name);
      setPackageInfo(inventory);
      setPackageStatus(`Inspected ${inventory.entries.length} entries. Package contents remain in the selected file; nothing was uploaded or copied.`);
      appendLogLine("info", `package: inspected ${file.name} (${inventory.format}, ${inventory.ncaCount} NCA entries)`);
    } catch (error) {
      setPackageStatus((error as Error).message);
      appendLogLine("error", `package inspection failed: ${(error as Error).message}`);
    } finally {
      setPackageBusy(false);
      if (packagePicker) packagePicker.value = "";
    }
  }

  function clearKeyFile(kind: "prod" | "title"): void {
    if (kind === "prod") setProdKeyFile(null);
    else setTitleKeyFile(null);
    setKeyImportStatus("Key file cleared from this session.");
  }

  async function importFirmware(files: FileList | null): Promise<void> {
    if (!files?.length || firmwareBusy()) return;
    setFirmwareBusy(true);
    setFirmwareStatus("Importing firmware files into local browser storage…");
    try {
      const manifest = await storeFirmwareFiles(files);
      setFirmwareInfo(manifest);
      setFirmwareStatus("Import complete. Files remain on this device and are not uploaded.");
      appendLogLine("info", `system: stored ${manifest.fileCount} firmware files (${formatBytes(manifest.totalBytes)}) locally`);
    } catch (error) {
      setFirmwareStatus((error as Error).message);
      appendLogLine("error", `system firmware import: ${(error as Error).message}`);
    } finally {
      setFirmwareBusy(false);
      if (firmwarePicker) firmwarePicker.value = "";
    }
  }

  async function clearFirmware(): Promise<void> {
    if (!window.confirm("Delete the locally stored firmware files from this browser?")) return;
    try {
      const root = await navigator.storage.getDirectory();
      const voland = await root.getDirectoryHandle("voland", { create: true });
      const system = await voland.getDirectoryHandle("system", { create: true });
      await system.removeEntry("firmware", { recursive: true }).catch(() => undefined);
      await system.removeEntry("firmware-manifest.json").catch(() => undefined);
      setFirmwareInfo(null);
      setFirmwareStatus("Local firmware storage cleared.");
      appendLogLine("info", "system: removed locally stored firmware files");
    } catch (error) {
      setFirmwareStatus((error as Error).message);
    }
  }

  function loadSelected(): void {
    if (emulatorStopped()) return;
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
          <section class="voland-home-menu-option">
            <div><strong>Show “Go to HOME Menu”</strong><small>When enabled, the emulator menu gets a shortcut back to the Voland HOME screen.</small></div>
            <label class="voland-toggle"><input type="checkbox" checked={showGoHome()} onChange={(event) => setShowGoHome(event.currentTarget.checked)} /><span class="voland-toggle-track" /><span class="voland-toggle-label">{showGoHome() ? "ON" : "OFF"}</span></label>
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
          <div class="voland-page-heading"><div><p class="voland-eyebrow">YOUR COLLECTION</p><h1>Game Library</h1><p>Launch built-in demos, load homebrew, or inspect your own NSP/XCI dump.</p></div><button class="voland-primary-button" onClick={() => setPage("emulator")}>＋ LOAD NRO</button></div>
          <div class="voland-library-grid"><For each={demos}>{(demo) => <button class="voland-library-card" disabled={!!launching()} onClick={() => void launchDemo(demo.file)}><span class={`voland-library-art art-${demo.art}`}><b>{demo.art === "bounce" ? "□" : demo.art === "calibration" ? "◉" : demo.art === "particles" ? "✳" : "▧"}</b><small>{demo.tag}</small></span><strong>{demo.name}</strong><span>{launching() === demo.file ? "Starting…" : demo.detail}</span></button>}</For></div>
          <div class="voland-empty-library"><span>＋</span><div><strong>Have your own homebrew?</strong><p>Open the emulator page to load an .nro file from your device.</p></div><button class="voland-secondary-button" onClick={() => setPage("emulator")}>OPEN LOADER</button></div>
          <section class="voland-system-panel">
            <div class="voland-panel-heading"><h2>Switch game package inspector</h2><span>NSP · XCI</span></div>
            <p class="voland-system-copy">Select a game dump from your own Switch. Voland reads only the package headers and file tables to list contained files; it does not upload, extract, or decrypt the game.</p>
            <input ref={(el) => { packagePicker = el; }} class="voland-hidden-input" type="file" accept=".nsp,.xci,application/octet-stream" onChange={(event) => void inspectPackageFile(event.currentTarget.files?.[0])} />
            <div class="voland-system-actions"><button class="voland-primary-button" type="button" disabled={packageBusy()} onClick={() => packagePicker?.click()}>{packageBusy() ? "INSPECTING…" : "＋ SELECT NSP / XCI"}</button>{packageInfo() && <button class="voland-secondary-button" type="button" onClick={() => { setPackageInfo(null); setPackageName(""); setPackageStatus("Package selection cleared."); }}>CLEAR</button>}</div>
            <p class="voland-system-status" role="status">{packageStatus()}</p>
            {packageInfo() && <>
              <div class="voland-firmware-stats"><span><strong>{packageInfo()!.format}</strong> format</span><span><strong>{packageInfo()!.entries.length}</strong> files</span><span><strong>{packageInfo()!.ncaCount}</strong> NCA files</span><span><strong>{formatBytes(packageInfo()!.totalEntryBytes)}</strong> indexed</span></div>
              <p class="voland-settings-note">Selected: {packageName()}. Showing up to 20 entries from this package.</p>
              <div class="voland-log-scroll">{packageInfo()!.entries.slice(0, 20).map((entry) => <p class="log-line">{entry.path} · {formatBytes(entry.size)} · {entry.kind.toUpperCase()}</p>)}</div>
              <p class="voland-settings-note">This milestone is package inspection only. Encrypted NCA processing, title metadata, and launching a game are separate upcoming steps.</p>
            </>}
          </section>
        </main>
      )}

      {page() === "emulator" && (
        <main class="voland-emulator-page">
          <section class="voland-display-column">
            <div class="voland-section-heading"><div><p class="voland-eyebrow">LIVE OUTPUT</p><h1>Emulator</h1><p>Guest framebuffer output appears here.</p></div><div class="voland-display-controls"><button class={`voland-fps-toggle${showFps() ? " is-active" : ""}`} type="button" aria-pressed={showFps()} onClick={() => setShowFps((value) => !value)}>FPS {showFps() ? "ON" : "OFF"}</button><button class="voland-stop-button" type="button" disabled={emulatorStopped()} onClick={() => { props.onStopEmulator(); setEmulatorStopped(true); }}>■ STOP EMULATOR</button><span class="voland-display-state">{emulatorStopped() ? "STOPPED" : "GPU"}</span></div></div>
            <div class="voland-display-frame"><div class="voland-screen"><div class="voland-screen-placeholder">{emulatorStopped() ? "Emulator stopped · refresh to restart" : "Waiting for display output"}</div>{showFps() && <div class="voland-fps-counter">FPS <strong>{fps()}</strong></div>}</div></div>
            <div class="voland-display-caption"><span>DISPLAY</span><span>1280 × 720</span></div>
            <div class="voland-facts"><div><dt>CPU backend</dt><dd>{props.cpuBackend}</dd></div><div><dt>GPU adapter</dt><dd>{props.adapterLabel}</dd></div><div><dt>Guest RAM</dt><dd>{props.guestRamMiB} MiB</dd></div></div>
          </section>
          <aside class="voland-sidebar">
            <section class="voland-panel voland-navigation-panel"><div class="voland-panel-heading"><h2>Navigation</h2><span>MENU</span></div><label class="voland-toggle"><input type="checkbox" checked={showGoHome()} onChange={(event) => setShowGoHome(event.currentTarget.checked)} /><span class="voland-toggle-track" /><span class="voland-toggle-label">{showGoHome() ? "ON" : "OFF"}</span><strong>Show “Go to HOME Menu”</strong></label>{showGoHome() && <button class="voland-home-menu-button" type="button" onClick={() => setPage("home")}>⌂ GO TO HOME MENU</button>}<p class="voland-loader-note">Returns to Voland’s HOME-style launcher. Real Nintendo HOME Menu boot support is still in development.</p></section>
            <section class="voland-panel voland-loader"><div class="voland-panel-heading"><h2>Homebrew loader</h2><span>NRO</span></div><label class="voland-file-label" for="nro-file">Choose a homebrew file</label><input id="nro-file" class="voland-file-input" type="file" accept=".nro,application/octet-stream" onChange={(event) => setSelectedNro(event.currentTarget.files?.[0] ?? null)} /><div class="voland-file-name">{selectedNro()?.name ?? "No NRO selected"}</div><button class="voland-load-button" type="button" disabled={!selectedNro()} onClick={loadSelected}>Load NRO</button><For each={demos}>{(demo) => <a class="voland-sample-link" href={`${import.meta.env.BASE_URL}homebrew/${demo.file}`} download={demo.file}>Download {demo.name}</a>}</For><p class="voland-loader-note">NRO homebrew runs in the guest CPU. Your files stay in this browser session.</p></section>
            <section class="voland-panel voland-log"><div class="voland-panel-heading"><h2>Event log</h2><span>LIVE</span></div><div class="voland-log-scroll"><For each={logs()}>{(entry) => <p class={`log-line log-${entry.level}`}>{`[${entry.level.toUpperCase().padEnd(5, " ")}] ${entry.message}`}</p>}</For></div></section>
          </aside>
        </main>
      )}

      {page() === "settings" && (
        <main class="voland-page"><div class="voland-page-heading"><div><p class="voland-eyebrow">CONSOLE CONFIGURATION</p><h1>Settings</h1><p>Runtime information and locally stored system files.</p></div></div><div class="voland-settings-grid"><div class="voland-setting-card"><span>PROCESSOR</span><strong>{props.cpuBackend}</strong><small>Active guest CPU backend</small></div><div class="voland-setting-card"><span>GRAPHICS</span><strong>{props.adapterLabel}</strong><small>Active WebGPU adapter</small></div><div class="voland-setting-card"><span>GUEST RAM CAPACITY</span><strong>{props.guestRamMiB} MiB</strong><small>Configured guest RAM pool</small></div><div class="voland-setting-card"><span>RUNTIME</span><strong>{status()}</strong><small>Worker and emulator status</small></div></div><section class="voland-memory-monitor"><div class="voland-panel-heading"><h2>Memory monitor</h2><span>LIVE · 1 SEC</span></div><p class="voland-memory-description">Exact guest page-allocator counters, plus the size of Voland’s shared WebAssembly memory. This is not a measurement of total Chrome OS memory.</p><div class="voland-memory-grid"><div class="voland-memory-card"><span>GUEST RAM IN USE</span><strong>{memoryStats() ? formatBytes(memoryStats()!.allocatedBytes) : "Waiting…"}</strong></div><div class="voland-memory-card"><span>GUEST RAM AVAILABLE</span><strong>{memoryStats() ? formatBytes(memoryStats()!.freeBytes) : "Waiting…"}</strong></div><div class="voland-memory-card"><span>PEAK GUEST USAGE</span><strong>{memoryStats() ? formatBytes(memoryStats()!.peakBytes) : "Waiting…"}</strong></div><div class="voland-memory-card"><span>WASM MEMORY RESERVED</span><strong>{memoryStats() ? formatBytes(memoryStats()!.wasmMemoryBytes) : "Waiting…"}</strong></div></div><div class="voland-memory-meter"><div><span>Guest RAM utilization</span><strong>{memoryStats() ? `${(memoryStats()!.allocatedBytes / Math.max(1, memoryStats()!.totalBytes) * 100).toFixed(1)}%` : "—"}</strong></div><div class="voland-memory-track"><div class="voland-memory-fill" style={{ width: `${memoryStats() ? Math.min(100, memoryStats()!.allocatedBytes / Math.max(1, memoryStats()!.totalBytes) * 100) : 0}%` }} /></div><small>{memoryStats() ? `${formatBytes(memoryStats()!.allocatedBytes)} of ${formatBytes(memoryStats()!.totalBytes)} guest RAM` : "Waiting for CPU worker telemetry"}</small></div></section><section class="voland-system-panel voland-key-import-panel">
            <div class="voland-panel-heading"><h2>Encryption keys</h2><span>{prodKeyFile() && titleKeyFile() ? "BOTH LOADED" : "KEYS REQUIRED"}</span></div>
            <p class="voland-system-copy">Select the key files dumped from your own Switch. Both files are held in memory for this browser session only; they are not uploaded or saved to browser storage.</p>
            <div class="voland-key-file-grid">
              <div class="voland-key-file-card">
                <strong>prod.keys</strong>
                <input ref={(el) => { prodKeyPicker = el; }} class="voland-key-file-input" type="file" accept=".keys,.txt" onChange={(event) => void importKeyFile("prod", event.currentTarget.files?.[0])} />
                <span>{prodKeyFile() ? `${prodKeyFile()!.name} · ${prodKeyFile()!.entryCount} entries · ${formatBytes(prodKeyFile()!.byteLength)}` : "Not loaded"}</span>
                <div class="voland-key-actions"><button class="voland-secondary-button" type="button" onClick={() => prodKeyPicker?.click()}>SELECT FILE</button>{prodKeyFile() && <button class="voland-secondary-button" type="button" onClick={() => clearKeyFile("prod")}>CLEAR</button>}</div>
              </div>
              <div class="voland-key-file-card">
                <strong>title.keys</strong>
                <input ref={(el) => { titleKeyPicker = el; }} class="voland-key-file-input" type="file" accept=".keys,.txt" onChange={(event) => void importKeyFile("title", event.currentTarget.files?.[0])} />
                <span>{titleKeyFile() ? `${titleKeyFile()!.name} · ${titleKeyFile()!.entryCount} entries · ${formatBytes(titleKeyFile()!.byteLength)}` : "Not loaded"}</span>
                <div class="voland-key-actions"><button class="voland-secondary-button" type="button" onClick={() => titleKeyPicker?.click()}>SELECT FILE</button>{titleKeyFile() && <button class="voland-secondary-button" type="button" onClick={() => clearKeyFile("title")}>CLEAR</button>}</div>
              </div>
            </div>
            <p class="voland-system-status" role="status">{keyImportStatus()}</p>
            <p class="voland-settings-note">This first step checks the text-file structure only. It does not verify cryptographic keys or enable encrypted NSP/XCI loading yet. Homebrew NRO demos remain usable without keys.</p>
          </section>
          <section class="voland-system-panel"><div class="voland-panel-heading"><h2>System firmware</h2><span>{firmwareInfo() ? "STORED LOCALLY" : "NOT INSTALLED"}</span></div><p class="voland-system-copy">Import a firmware folder dumped from your own Switch. Voland stores the files in this browser's private local storage; nothing is sent to our servers or GitHub.</p><input ref={(el) => { firmwarePicker = el; el.setAttribute("webkitdirectory", ""); el.setAttribute("directory", ""); }} class="voland-hidden-input" type="file" multiple onChange={(event) => void importFirmware(event.currentTarget.files)} /><div class="voland-system-actions"><button class="voland-primary-button" type="button" disabled={firmwareBusy()} onClick={() => firmwarePicker?.click()}>{firmwareBusy() ? "IMPORTING…" : "＋ IMPORT FIRMWARE FOLDER"}</button>{firmwareInfo() && <button class="voland-secondary-button" type="button" disabled={firmwareBusy()} onClick={() => void clearFirmware()}>CLEAR LOCAL FILES</button>}</div><p class="voland-system-status">{firmwareStatus()}</p>{firmwareInfo() && <div class="voland-firmware-stats"><span><strong>{firmwareInfo()!.fileCount}</strong> files</span><span><strong>{formatBytes(firmwareInfo()!.totalBytes)}</strong> stored</span><span>Imported {new Date(firmwareInfo()!.importedAt).toLocaleString()}</span></div>}<p class="voland-settings-note">Firmware storage is implemented as a local import step. The emulator does not consume firmware yet; system-service integration is still under development.</p></section></main>
      )}
    </div>
  );
}
export default App;
