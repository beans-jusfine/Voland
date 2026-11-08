/**
 * WebGPU presenter for Voland's shared double-buffered RGBA8 framebuffer.
 */
import type { MemoryLayout } from "@bindings/layout";
import type { GPUToMainMessage, MainToGPUMessage } from "@bindings/protocol";
const self: DedicatedWorkerGlobalScope = globalThis as unknown as DedicatedWorkerGlobalScope;
function log(level: "debug" | "info" | "warn" | "error", message: string): void {
  self.postMessage({ type: "log", level, message } satisfies GPUToMainMessage);
}
const WIDTH = 1280, HEIGHT = 720;
const HEADER_BYTES = 128, MAX_WIDTH = 1920, MAX_HEIGHT = 1080;
const SLOT_BYTES = MAX_WIDTH * MAX_HEIGHT * 4;
const SHADER = `
struct VertexOutput { @builtin(position) position: vec4f, };
@group(0) @binding(0) var frameTexture: texture_2d<f32>;
@group(0) @binding(1) var frameSampler: sampler;
@vertex
fn vertexMain(@builtin(vertex_index) index: u32) -> VertexOutput {
  var positions = array<vec2f, 3>(vec2f(-1.0,-1.0),vec2f(3.0,-1.0),vec2f(-1.0,3.0));
  var output: VertexOutput; output.position = vec4f(positions[index],0.0,1.0); return output;
}
@fragment
fn fragmentMain(@builtin(position) position: vec4f) -> @location(0) vec4f {
  let uv = position.xy / vec2f(1280.0,720.0);
  return textureSample(frameTexture,frameSampler,uv);
}`;
async function init(canvas: OffscreenCanvas, memory: WebAssembly.Memory, layout: MemoryLayout): Promise<void> {
  if (!("gpu" in navigator)) throw new Error("WebGPU unavailable inside worker");
  const gpu = (navigator as Navigator & { gpu: GPU }).gpu;
  const adapter = await gpu.requestAdapter();
  if (!adapter) throw new Error("requestAdapter() returned null");
  const device = await adapter.requestDevice();
  canvas.width = WIDTH; canvas.height = HEIGHT;
  const context = canvas.getContext("webgpu") as GPUCanvasContext | null;
  if (!context) throw new Error("getContext('webgpu') returned null");
  const format = gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: "opaque" });
  let texture: GPUTexture | null = null;
  let textureWidth = 0;
  let textureHeight = 0;
  // Nearest-neighbor upscaling keeps low-resolution guest framebuffers crisp instead of smearing pixels.
  const sampler = device.createSampler({ magFilter: "nearest", minFilter: "nearest" });
  const shader = device.createShaderModule({ label: "Voland framebuffer blit", code: SHADER });
  const pipeline = device.createRenderPipeline({
    label: "Voland framebuffer presentation pipeline", layout: "auto",
    vertex: { module: shader, entryPoint: "vertexMain" },
    fragment: { module: shader, entryPoint: "fragmentMain", targets: [{ format }] },
    primitive: { topology: "triangle-list" },
  });
  let bindGroup: GPUBindGroup | null = null;
  const ensureTexture = (width: number, height: number) => {
    if (textureWidth === width && textureHeight === height) return;
    if (texture) texture.destroy();
    textureWidth = width;
    textureHeight = height;
    texture = device.createTexture({
      label: "Voland guest framebuffer",
      size: { width, height },
      format: "rgba8unorm",
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST,
    });
    bindGroup = device.createBindGroup({
      layout: pipeline.getBindGroupLayout(0),
      entries: [{ binding: 0, resource: texture.createView() }, { binding: 1, resource: sampler }],
    });
  };
  ensureTexture(WIDTH, HEIGHT);
  const base = layout.framebufferSlotBase;
  const counters = new Uint32Array(memory.buffer, base, 2);
  let presented = 0;
  let fpsWindowStart = performance.now();
  let fpsWindowFrames = 0;
  const draw = () => {
    const encoder = device.createCommandEncoder();
    const pass = encoder.beginRenderPass({ colorAttachments: [{
      view: context.getCurrentTexture().createView(),
      clearValue: { r: 0.015, g: 0.018, b: 0.03, a: 1 }, loadOp: "clear", storeOp: "store",
    }] });
    if (!bindGroup) { pass.end(); return; }
    pass.setPipeline(pipeline); pass.setBindGroup(0, bindGroup); pass.draw(3); pass.end();
    device.queue.submit([encoder.finish()]);
  };
  const poll = () => {
    const published = Atomics.load(counters, 0);
    if (published !== 0 && published !== presented) {
      const slot = (published - 1) & 1;
      const metadata = new DataView(memory.buffer, base + 8 + slot * 16, 16);
      const width = metadata.getUint32(0, true), height = metadata.getUint32(4, true);
      const pixelFormat = metadata.getUint32(8, true), byteLength = metadata.getUint32(12, true);
      if (width > 0 && width <= MAX_WIDTH && height > 0 && height <= MAX_HEIGHT &&
          pixelFormat === 1 && byteLength === width * height * 4 && byteLength <= SLOT_BYTES) {
        ensureTexture(width, height);
        if (!texture) return;
        const offset = base + HEADER_BYTES + slot * SLOT_BYTES;
        const pixels = new Uint8Array(memory.buffer, offset, byteLength);
        device.queue.writeTexture({ texture }, pixels,
          { offset: 0, bytesPerRow: width * 4, rowsPerImage: height },
          { width, height, depthOrArrayLayers: 1 });
        presented = published;
        fpsWindowFrames++;
        Atomics.store(counters, 1, published);
        draw();
        log("info", `presented shared framebuffer frame ${published} (${width}x${height}, RGBA8)`);
      } else {
        log("error", `rejected framebuffer metadata: ${width}x${height}, format=${pixelFormat}, bytes=${byteLength}`);
        presented = published;
      }
    }
    const now = performance.now();
    const elapsed = now - fpsWindowStart;
    if (elapsed >= 1000) {
      self.postMessage({ type: "fps", value: Math.round(fpsWindowFrames * 1000 / elapsed) } satisfies GPUToMainMessage);
      fpsWindowFrames = 0;
      fpsWindowStart = now;
    }
    self.setTimeout(poll, 16);
  };
  draw(); poll();
  log("info", `WebGPU shared framebuffer presenter ready (${WIDTH}x${HEIGHT})`);
  self.postMessage({
    type: "ready",
    adapterName: ((adapter as unknown as { info?: { vendor?: string } }).info?.vendor) ?? null,
  } satisfies GPUToMainMessage);
}
self.addEventListener("message", (event: MessageEvent<MainToGPUMessage>) => {
  if (event.data.type === "init") init(event.data.canvas, event.data.memory, event.data.layout).catch((error: unknown) => {
    self.postMessage({ type: "error", message: `init threw: ${error instanceof Error ? error.message : String(error)}` } satisfies GPUToMainMessage);
  });
});
