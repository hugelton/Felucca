// SPDX-License-Identifier: GPL-3.0-only
// Adapted from kmplngj/Felucca, commit 85cba5df8f1ef4aeaebec582e02afcaba5731f76.
// Test-only observation of the real page/worklet. Loaded by Playwright, never shipped.
export function installProbe() {
  const OriginalNode = globalThis.AudioWorkletNode;
  const probe = globalThis.__feluccaTest = { frames: 0, draws: 0, leds: [], errors: [], audio: [], samples: 0 };
  globalThis.AudioWorkletNode = class extends OriginalNode {
    constructor(context, name, options) {
      super(context, name, options);
      if (name !== "felucca") return;
      probe.context = context;
      probe.node = this;
      const post = this.port.postMessage.bind(this.port);
      this.port.postMessage = (message, ...args) => {
        if (message.type === "load") probe.restoredSectors = Object.keys(message.sectors || {}).length;
        return post(message, ...args);
      };
      this.addEventListener("processorerror", () => probe.errors.push("Felucca AudioWorklet processorerror"));
      this.port.addEventListener("message", ({ data: m }) => {
        if (m.type === "error") probe.errors.push(m.message);
        if (m.type === "frame") {
          probe.frames++;
          if (m.fb) probe.draws++;
          if (m.leds) probe.leds = m.leds;
        }
      });
      this.port.start();
    }
  };
  probe.listen = async () => {
    // A separate bounded PCM tap observes the real output; no synthetic tone or
    // replacement renderer. Its silent output keeps the tap in the audio graph.
    const source = `class Capture extends AudioWorkletProcessor {
      constructor() { super(); this.left = new Float32Array(2048); this.right = new Float32Array(2048); this.n = 0; }
      process(inputs) {
        const input = inputs[0];
        if (!input?.[0]) return true;
        for (let i = 0; i < input[0].length; i++) {
          this.left[this.n] = input[0][i]; this.right[this.n++] = (input[1] || input[0])[i];
          if (this.n === 2048) {
            this.port.postMessage({ left: this.left, right: this.right }, [this.left.buffer, this.right.buffer]);
            this.left = new Float32Array(2048); this.right = new Float32Array(2048); this.n = 0;
          }
        }
        return true;
      }
    }; registerProcessor("felucca-test-capture", Capture);`;
    const url = URL.createObjectURL(new Blob([source], { type: "text/javascript" }));
    try { await probe.context.audioWorklet.addModule(url); }
    finally { URL.revokeObjectURL(url); }
    const tap = new OriginalNode(probe.context, "felucca-test-capture", { outputChannelCount: [2] });
    tap.addEventListener("processorerror", () => probe.errors.push("PCM tap processorerror"));
    tap.port.onmessage = ({ data: block }) => {
      const left = Array.from(block.left), right = Array.from(block.right);
      let peak = 0, finite = true;
      const rms = [];
      for (const channel of [left, right]) {
        let sum = 0;
        for (const sample of channel) {
          finite &&= Number.isFinite(sample);
          peak = Math.max(peak, Math.abs(sample)); sum += sample * sample;
        }
        rms.push(Math.sqrt(sum / channel.length));
      }
      if ((!finite || peak > 1) && probe.errors.length < 20) probe.errors.push("Non-finite or out-of-range PCM");
      probe.latest = { peak, rms: Math.max(...rms), left_rms: rms[0], right_rms: rms[1], finite };
      probe.samples += left.length;
      probe.audio.push({ left, right });
      // Keep the last 8 seconds even when a UI wait times out.
      while (probe.audio.length > Math.ceil(probe.context.sampleRate * 8 / 2048)) probe.audio.shift();
    };
    probe.node.connect(tap);
    tap.connect(probe.context.destination);
  };
}
