/**
 * GPU rendering worker. Phase 0 display milestone: acquire WebGPU and draw
 * a visible diagnostic pattern to the transferred canvas. Guest Maxwell
 * command buffers and framebuffer presentation are still future work.
 */
import type { MemoryLayout } from "@bindings/layout";
import type { GPUToMainMessage, MainToGPUMessage } from "@bindings/protocol";

const self: DedicatedWorkerGlobalScope =
  globalThis as unknown as DedicatedWorkerGlobalScope;

function log(level: "debug" | "info" | "warn" | "error", message: string): void {
  const msg: GPUToMainMessage = { type: "log", level, message };
  self.postMessage(msg);
}

const DISPLAY_WIDTH = 1280;
const DISPLAY_HEIGHT = 720;

const DISPLAY_SHADER = /* wgsl */ \`
struct VertexOutput {
  @builtin(position) position: vec4f,
};

@vertex
fn vertexMain(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
  var positions = array<vec2f, 3>(
    vec2f(-1.0, -1.0),
    vec2f( 3.0, -1.0),
    vec2f(-1.0,  3.0)
  );
  var output: VertexOutput;
  output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
  return output;
}

@fragment
fn fragmentMain(@builtin(position) position: vec4f) -> @location(0) vec4f {
  let uv = position.xy / vec2f(1280.0, 720.0);
  let glow = 0.5 + 0.5 * sin(uv.x * 5.0 + uv.y * 3.0);
  var color = mix(
    vec3f(0.018, 0.025, 0.065),
    vec3f(0.055, 0.025, 0.105),
    uv.y
  );

  let gridX = 1.0 - smoothstep(0.0, 0.012, abs(fract(uv.x * 16.0) - 0.5));
  let gridY = 1.0 - smoothstep(0.0, 0.012, abs(fract(uv.y * 9.0) - 0.5));
  color += vec3f(0.025, 0.045, 0.085) * max(gridX, gridY) * 0.28;

  let circleA = 1.0 - smoothstep(0.105, 0.112, distance(uv, vec2f(0.25, 0.48)));
  let circleB = 1.0 - smoothstep(0.075, 0.082, distance(uv, vec2f(0.73, 0.38)));
  let circleC = 1.0 - smoothstep(0.052, 0.059, distance(uv, vec2f(0.64, 0.72)));
  color = mix(color, vec3f(0.08, 0.58, 1.0), circleA);
  color = mix(color, vec3f(0.72, 0.22, 1.0), circleB);
  color = mix(color, vec3f(0.12, 0.92, 0.72), circleC);

  let bar = step(0.18, uv.x) * step(0.76, uv.y)
          * step(uv.y, 0.79) * step(uv.x, 0.46);
  color = mix(color, vec3f(0.15, 0.48, 0.95) * (0.65 + glow * 0.35), bar);

  let marker = step(0.82, uv.x) * step(0.78, uv.y)
             * step(uv.y, 0.80) * step(uv.x, 0.84);
  color = mix(color, vec3f(0.35, 0.95, 0.78), marker);

  return vec4f(color, 1.0);
}
\`;

async function init(canvas: OffscreenCanvas, memory: WebAssembly.Memory, layout: MemoryLayout): Promise<void> {
  log("debug", \`layout handshake: framebufferSlotBase=0x\${layout.framebufferSlotBase.toString(16)}\`);
  void memory; // guest framebuffer-slot presentation arrives with Maxwell emulation

  if (!("gpu" in navigator)) {
    const err: GPUToMainMessage = { type: "error", message: "WebGPU unavailable inside worker" };
    self.postMessage(err);
    return;
  }

  const gpu = (navigator as Navigator & { gpu: GPU }).gpu;
  const adapter = await gpu.requestAdapter();
  if (!adapter) {
    const err: GPUToMainMessage = { type: "error", message: "requestAdapter() returned null" };
    self.postMessage(err);
    return;
  }

  const device = await adapter.requestDevice();
  canvas.width = DISPLAY_WIDTH;
  canvas.height = DISPLAY_HEIGHT;
  const context = canvas.getContext("webgpu") as GPUCanvasContext | null;
  if (!context) {
    const err: GPUToMainMessage = { type: "error", message: "getContext('webgpu') returned null" };
    self.postMessage(err);
    return;
  }

  const format = gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: "opaque" });

  const shader = device.createShaderModule({ label: "Voland display test shader", code: DISPLAY_SHADER });
  const pipeline = device.createRenderPipeline({
    label: "Voland display test pipeline",
    layout: "auto",
    vertex: { module: shader, entryPoint: "vertexMain" },
    fragment: { module: shader, entryPoint: "fragmentMain", targets: [{ format }] },
    primitive: { topology: "triangle-list" },
  });

  const encoder = device.createCommandEncoder();
  const pass = encoder.beginRenderPass({
    colorAttachments: [{
      view: context.getCurrentTexture().createView(),
      clearValue: { r: 0.02, g: 0.02, b: 0.05, a: 1 },
      loadOp: "clear",
      storeOp: "store",
    }],
  });
  pass.setPipeline(pipeline);
  pass.draw(3);
  pass.end();
  device.queue.submit([encoder.finish()]);

  log(\`info\`, \`WebGPU display test rendered (\${DISPLAY_WIDTH}x\${DISPLAY_HEIGHT}), format=\${format}\`);
  const ready: GPUToMainMessage = {
    type: "ready",
    adapterName: ((adapter as unknown as { info?: { vendor?: string } }).info?.vendor) ?? null,
  };
  self.postMessage(ready);
}

self.addEventListener("message", (event: MessageEvent<MainToGPUMessage>) => {
  if (event.data.type === "init") {
    init(event.data.canvas, event.data.memory, event.data.layout).catch((e: unknown) => {
      const err: GPUToMainMessage = {
        type: "error",
        message: \`init threw: \${e instanceof Error ? e.message : String(e)}\`,
      };
      self.postMessage(err);
    });
  }
});
