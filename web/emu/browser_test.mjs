// SPDX-License-Identifier: GPL-3.0-only
// Adapted from kmplngj/Felucca, commits 85cba5df8f1ef4aeaebec582e02afcaba5731f76
// and 5facc220d28234f30e93afb41982171c664519a8 (PR #391).
// A short real-browser gate. No model calls; one summary, media on failure only.
import assert from "node:assert/strict";
import { createHash } from "node:crypto";
import { createServer } from "node:http";
import { readFile, writeFile, mkdir, mkdtemp } from "node:fs/promises";
import { resolve, relative, extname, sep } from "node:path";
import { fileURLToPath } from "node:url";
import { installProbe } from "./browser_probe.js";

const root = fileURLToPath(new URL("../../", import.meta.url));
const build = resolve(root, "build"), site = resolve(build, "emu");
const keepMedia = process.argv.slice(2).includes("--artifacts=always");
assert(process.argv.slice(2).every((arg) => arg === "--artifacts=always"), "usage: npm test -- [--artifacts=always]");
await mkdir(resolve(build, "browser"), { recursive: true });
const out = await mkdtemp(resolve(build, "browser/run-"));
const report = { schema: 1, status: "fail", checks: [], errors: [], evidence: [], scope: "Chromium emulator; no physical FM-1" };
const hash = (data) => createHash("sha256").update(data).digest("hex");
let browser, page, server, capturedAudio, factoryEnvelope, current = "setup";
const screenshots = new Map();
const messages = [];

async function check(name, action) {
  current = name;
  const start = Date.now();
  const observed = await action();
  report.checks.push({ name, status: "pass", ms: Date.now() - start, ...(observed && { observed }) });
}
async function screen(name) {
  const pixels = JSON.parse(await page.locator("#screen").evaluate((canvas) => {
    // Read a copy so observation does not change the page canvas's rendering mode.
    const context = new OffscreenCanvas(240, 240).getContext("2d", { willReadFrequently: true });
    context.drawImage(canvas, 0, 0);
    const data = context.getImageData(0, 0, 240, 240).data;
    const colors = new Set();
    for (let i = 0; i < data.length; i += 4) colors.add((data[i] << 16) | (data[i + 1] << 8) | data[i + 2]);
    return JSON.stringify({ colors: colors.size, pixels: Array.from(data) });
  }));
  assert(pixels.colors > 4, `${name}: blank or incomplete LCD (${pixels.colors} colors)`);
  screenshots.set(name, await page.locator("#screen").screenshot());
  // ui.c's default graph panel is y=76..197; exclude transient header/card highlights.
  return { sha256: hash(Buffer.from(pixels.pixels)), colors: pixels.colors,
    graph_sha256: hash(Buffer.from(pixels.pixels.slice(76 * 240 * 4, 198 * 240 * 4))) };
}
async function waitFor(fn, arg) {
  const handle = await page.waitForFunction(fn, arg, { timeout: 8000 });
  const value = await handle.jsonValue();
  await handle.dispose();
  return value;
}
async function freshAudio(predicate) {
  const after = await page.evaluate(() => __feluccaTest.samples);
  return waitFor(({ after, sounding }) => {
    const p = __feluccaTest, a = p.latest;
    return p.samples >= after + 8192 && a?.finite &&
      (sounding ? Math.min(a.left_rms, a.right_rms) > 0.001 : a.rms < 0.0001) && a;
  }, { after, sounding: predicate });
}
async function tap(name) {
  // Keyboard focus on the real target exercises its DOM listeners. A held key
  // lasts long enough for the page -> worklet -> firmware input queue.
  const target = page.getByRole("button", { name, exact: true });
  await target.focus();
  await page.keyboard.down("Enter");
  await page.waitForTimeout(80);
  await page.keyboard.up("Enter");
  await page.waitForTimeout(80);
}
function wav(blocks, rate) {
  const frames = blocks.reduce((n, b) => n + b.left.length, 0);
  const data = Buffer.alloc(44 + frames * 4);
  data.write("RIFF"); data.writeUInt32LE(data.length - 8, 4); data.write("WAVEfmt ", 8);
  data.writeUInt32LE(16, 16); data.writeUInt16LE(1, 20); data.writeUInt16LE(2, 22);
  data.writeUInt32LE(rate, 24); data.writeUInt32LE(rate * 4, 28);
  data.writeUInt16LE(4, 32); data.writeUInt16LE(16, 34);
  data.write("data", 36); data.writeUInt32LE(frames * 4, 40);
  let offset = 44;
  for (const block of blocks) for (let i = 0; i < block.left.length; i++) for (const ch of ["left", "right"]) {
    const sample = Number.isFinite(block[ch][i]) ? block[ch][i] : 0;
    data.writeInt16LE(Math.round(Math.max(-1, Math.min(1, sample)) * 32767), offset); offset += 2;
  }
  return data;
}

try {
  report.inputs = {};
  for (const name of ["felucca.wasm", "index.html", "worklet.js"]) report.inputs[name] = hash(await readFile(resolve(site, name)));
  const { chromium } = await import("playwright");
  server = createServer(async (req, res) => {
    try {
      const path = new URL(req.url, "http://localhost").pathname;
      if (path === "/favicon.ico") { res.writeHead(204).end(); return; }
      const file = resolve(site, "." + decodeURIComponent(path === "/" ? "/index.html" : path));
      if (!file.startsWith(site + sep)) { res.writeHead(403).end(); return; }
      const bytes = await readFile(file);
      const mime = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".woff": "font/woff" };
      res.writeHead(200, { "Content-Type": mime[extname(file)] || "application/octet-stream", "Cache-Control": "no-store" }).end(bytes);
    } catch { res.writeHead(404).end(); }
  });
  await new Promise((res, rej) => { server.once("error", rej); server.listen(0, "127.0.0.1", res); });
  browser = await chromium.launch({ headless: true, ...(process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH && {
    executablePath: process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH,
  }) });
  report.browser = { version: browser.version(), playwright: JSON.parse(await readFile(new URL("node_modules/playwright/package.json", import.meta.url), "utf8")).version };
  page = await browser.newPage({ viewport: { width: 1200, height: 900 } });
  page.setDefaultTimeout(8000);
  // Keep errors bounded; never stream a console, framebuffer or PCM into an agent.
  const error = (message) => { if (messages.length < 20) messages.push(message.slice(0, 1000)); };
  page.on("pageerror", (e) => error(String(e)));
  page.on("console", (m) => { if (m.type() === "error" || m.type() === "warning") error(`${m.type()}: ${m.text()}`); });
  page.on("requestfailed", (r) => error(`request failed: ${r.url()} ${r.failure()?.errorText}`));
  page.on("response", (r) => { if (r.status() >= 400) error(`HTTP ${r.status()}: ${r.url()}`); });
  await page.addInitScript(installProbe);
  await check("boot and LCD", async () => {
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.evaluate(() => document.fonts.ready);
    await page.getByRole("button", { name: "Switch on", exact: true }).click();
    await page.locator("#start").waitFor({ state: "hidden" });
    await waitFor(() => __feluccaTest.frames > 5 && (__feluccaTest.leds[0] & (1 << 6)));
    assert.equal(await page.evaluate(() => __feluccaTest.context.state), "running");
    await page.evaluate(() => __feluccaTest.listen());
    assert.equal(await page.evaluate(() => __feluccaTest.context.sampleRate), 44100);
    return screen("home");
  });
  await check("keyboard note produces stereo PCM", async () => {
    await page.keyboard.down("z");
    const observed = await freshAudio(true);
    assert(observed.peak <= 1, `out-of-range audio: ${observed.peak}`);
    await page.keyboard.up("z");
    return observed;
  });
  await check("note release becomes quiet", () => freshAudio(false));
  await check("pointer opens ENV and encoder redraws LCD", async () => {
    const before = await screen("before-env");
    await page.getByRole("button", { name: "ENV", exact: true }).click();
    await waitFor(() => (__feluccaTest.leds[0] & (1 << 2)) !== 0);
    const env = await screen("env");
    factoryEnvelope = env.graph_sha256;
    assert.notEqual(env.sha256, before.sha256, "ENV did not change the LCD");
    // Capture the count before input; LED/load messages are not LCD updates.
    const draws = await page.evaluate(() => __feluccaTest.draws);
    // A distinctive attack change must affect the graph, not just knob emphasis.
    for (let i = 0; i < 24; i++) await page.getByRole("slider", { name: "KNOB 1", exact: true }).press("ArrowUp");
    await waitFor((n) => __feluccaTest.draws > n, draws);
    const edited = await screen("env-edited");
    assert.notEqual(edited.sha256, env.sha256, "encoder did not change the LCD");
    return edited;
  });
  await check("portrait layout remains playable", async () => {
    await page.setViewportSize({ width: 390, height: 844 });
    await waitFor(() => document.getElementById("panel").viewBox.baseVal.width === 600);
    const key = page.getByRole("button", { name: "F3", exact: true });
    await key.scrollIntoViewIfNeeded();
    const box = await key.boundingBox();
    assert(box && box.width > 0 && box.x >= 0 && box.x + box.width <= 391, "key outside portrait viewport");
    await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
    await page.mouse.down();
    const observed = await freshAudio(true);
    await page.mouse.up();
    await freshAudio(false);
    await screen("portrait");
    return observed;
  });
  await check("panel save reaches browser storage and reload", async () => {
    await page.setViewportSize({ width: 1200, height: 900 });
    await waitFor(() => document.getElementById("panel").viewBox.baseVal.width === 1000);
    const expected = await screen("before-save");
    assert.notEqual(expected.graph_sha256, factoryEnvelope, "saved attack must differ from factory state");
    await tap("SAVE"); await tap("OCT+"); await tap("OCT+");
    await waitFor(() => {
      const value = localStorage.getItem("felucca-emu-flash");
      return value && Object.keys(JSON.parse(value)).length && value;
    });
    capturedAudio = JSON.parse(await page.evaluate(() => JSON.stringify({ blocks: __feluccaTest.audio, rate: __feluccaTest.context.sampleRate })));
    messages.push(...await page.evaluate(() => __feluccaTest.errors));
    await page.reload();
    // Read the final persisted snapshot after reload: deferred settings writes
    // can legitimately arrive between the save gesture and navigation. The
    // restored graph below verifies that the user preset was applied.
    const saved = await page.evaluate(() => localStorage.getItem("felucca-emu-flash"));
    assert(saved && Object.keys(JSON.parse(saved)).length, "storage missing after reload");
    await page.getByRole("button", { name: "Switch on", exact: true }).click();
    await page.locator("#start").waitFor({ state: "hidden" });
    await waitFor(() => __feluccaTest.frames > 5 && (__feluccaTest.leds[0] & (1 << 6)));
    assert.equal(await page.evaluate(() => __feluccaTest.restoredSectors), Object.keys(JSON.parse(saved)).length,
      "saved sectors were not delivered to the restarted worklet");
    await screen("reloaded");
    // Load USER slot 1 through the real panel and observe the applied parameters.
    await tap("SAVE");
    await page.getByRole("slider", { name: "KNOB 2", exact: true }).press("ArrowUp");
    await tap("OCT+"); await tap("HOME"); await tap("ENV");
    await waitFor(() => (__feluccaTest.leds[0] & (1 << 2)) !== 0);
    const restored = await screen("restored-env");
    assert.equal(restored.graph_sha256, expected.graph_sha256, "saved envelope was not restored and applied");
    return { sectors: Object.keys(JSON.parse(saved)).length, storage_sha256: hash(saved),
      restored_graph_sha256: restored.graph_sha256 };
  });
  await check("no browser or worklet errors", async () => {
    messages.push(...await page.evaluate(() => __feluccaTest.errors));
    assert.deepEqual(messages, []);
  });
  report.status = "pass";
} catch (e) {
  report.checks.push({ name: current, status: "fail", error: String(e.message || e).slice(0, 2000) });
  // Startup/input failures can precede the normal error check. Keep the worklet's
  // actual cause in the small report even when the visible failure is a timeout.
  const probeErrors = page ? await page.evaluate(() => globalThis.__feluccaTest?.errors || []).catch(() => []) : [];
  report.errors = [...new Set([...probeErrors, ...messages])].slice(0, 20).map((s) => String(s).slice(0, 1000));
} finally {
  if (page && (report.status === "fail" || keepMedia)) {
    try {
      await page.screenshot({ path: resolve(out, "page.png"), fullPage: true, timeout: 2000 });
      report.evidence.push("page.png");
      for (const [name, png] of screenshots) {
        await writeFile(resolve(out, `${name}.png`), png); report.evidence.push(`${name}.png`);
      }
      const audio = capturedAudio || JSON.parse(await page.evaluate(() => JSON.stringify({ blocks: __feluccaTest.audio, rate: __feluccaTest.context?.sampleRate })));
      if (audio.blocks.length) {
        await writeFile(resolve(out, "audio.wav"), wav(audio.blocks, audio.rate)); report.evidence.push("audio.wav");
      }
    } catch (e) { report.errors.push(`evidence: ${String(e.message || e).slice(0, 500)}`); }
  }
  try { await browser?.close(); }
  catch (e) { report.status = "fail"; report.errors.push(`cleanup: ${String(e)}`); }
  if (server?.listening) {
    server.closeAllConnections();
    await new Promise((res) => server.close(res));
  }
  await writeFile(resolve(out, "summary.json"), JSON.stringify(report, null, 2) + "\n");
  const summary = `browser: ${report.status.toUpperCase()} (${report.checks.filter((c) => c.status === "pass").length} checks); ${relative(root, out)}/summary.json`;
  console.log(summary);
  if (report.status === "fail") console.error(`${current}: ${report.checks.at(-1).error}`);
  process.exitCode = report.status === "pass" ? 0 : 1;
}
