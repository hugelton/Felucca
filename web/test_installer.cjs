// SPDX-License-Identifier: GPL-3.0-only
// Run against the output of make_site.py with Playwright's Chromium installed:
//   node web/test_installer.cjs /path/to/generated/site
const assert = require("node:assert/strict");
const { readFile } = require("node:fs/promises");
const { createServer } = require("node:http");
const { join } = require("node:path");
const { chromium } = require("playwright");

const site = process.argv[2];

function mockMidi({ scenario, product }) {
  const midi = Object.assign(new EventTarget(), { inputs: new Map(), outputs: new Map() });
  const stats = window.midiStats = { upgrades: [], probes: [], finish: 0, open: 0 };
  let serial = 0;
  function pack(data) {
    const out = [0xF0];
    let acc = 0, bits = 0;
    for (const b of data) {
      acc |= b << bits;
      bits += 8;
      while (bits >= 7) {
        out.push(acc & 127);
        acc >>>= 7;
        bits -= 7;
      }
    }
    if (bits) out.push(acc & 127);
    return [...out, 0xF7];
  }
  const changed = () => midi.dispatchEvent(new Event("statechange"));

  function plug(unit, name, identity) {
    const id = ++serial;
    const input = { id: "i" + id, name, state: "connected", connection: "closed", onmidimessage: null };
    const output = { id: "o" + id, name, state: "connected", connection: "closed" };
    for (const port of [input, output]) {
      port.open = async () => {
        if (port.connection === "closed") stats.open++;
        port.connection = "open";
        changed();
      };
      port.close = async () => {
        if (port.connection === "open") stats.open--;
        port.connection = "closed";
        changed();
      };
    }
    const notify = (frame) => setTimeout(() => input.onmidimessage?.({ data: Uint8Array.from(frame) }), 1);
    const request = (addr) => {
      const body = [0, 0x59, 0x30, 0, 0, 0, 0, 0, 0, 0, addr >>> 24, 8, 0, 0];
      body.push(~body.slice(6).reduce((sum, b) => sum + b, 0) & 0xff);
      notify(pack(body));
    };
    const remove = () => {
      input.state = output.state = "disconnected";
      midi.inputs.delete(input.id);
      midi.outputs.delete(output.id);
      changed();
    };
    const loader = identity.startsWith("ota-");
    let replies = 0;
    output.send = (frame) => {
      if (frame.length === 10 && frame[1] === 0) {
        stats.probes.push(identity);
        const text = [...new TextEncoder().encode(identity)];
        notify(pack([0, 0x59, 0x11, 0, 0, 0, ...text, ...new Array(28 - text.length).fill(0)]));
      } else if (frame.length === 6 && frame[1] === 0x22) {
        stats.upgrades.push([unit, identity]);
        request(0);
      } else if (++replies === 1) {
        request(loader ? 0xF0000000 : 0xE0000000);
      } else {
        if (scenario === "lost") throw new Error("finish send failed");
        stats.finish++;
        setTimeout(() => {
          remove();
          if (!loader) {
            plug(unit, "Felucca Update", "ota-FM-1_918");
          } else if (scenario === "another") {
            plug("B", "FM-1 B", product);
            setTimeout(() => plug("A", "Felucca", "FM-1_000"), 150);
          } else {
            plug(unit, "Felucca", scenario === "rescue" ? "FM-1_000" : product);
          }
        }, 100);
      }
    };
    midi.inputs.set(input.id, input);
    midi.outputs.set(output.id, output);
    changed();
  }

  const starting = scenario === "fresh" ? "FM-1_015" : "ota-FM-1_900";
  plug("A", "FM-1", starting);
  plug("keys", "MIDI Keyboard", "KEYS_001");
  if (scenario === "competing") plug("B", "FM-1 B", product);
  Object.defineProperty(navigator, "requestMIDIAccess", { value: async () => midi });
  window.installerStatuses = [];
  window.addEventListener("DOMContentLoaded", () => {
    const status = document.querySelector("#status");
    new MutationObserver(() => window.installerStatuses.push(status.textContent))
      .observe(status, { childList: true });
  });
}

async function run() {
  const html = await readFile(join(site, "webapp/installer/index.html"), "utf8");
  const product = /"product": "(FM-1_9\d\d)"/.exec(html)[1];
  const server = createServer(async (req, res) => {
    try {
      res.end(await readFile(join(site, req.url.endsWith("/") ? req.url + "index.html" : req.url)));
    } catch {
      res.writeHead(404).end();
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const url = `http://127.0.0.1:${server.address().port}/webapp/installer/`;
  let browser;
  try {
    browser = await chromium.launch();
    for (const [scenario, status] of [
      ["another", /Connect only the FM-1/],
      ["fresh", /^Done$/],
      ["resume", /^Done$/],
      ["rescue", /FM-1_000/],
      ["lost", /USB connection was lost/],
      ["competing", /Connect only the FM-1/],
      ["V15", /^Done$/],
      ["V16", /^Done$/],
    ]) {
      const page = await browser.newPage({ locale: "en-US" });
      try {
        const stock = { V15: [699956, "FM-1_015"], V16: [718196, "FM-1_016"] }[scenario];
        await page.addInitScript(mockMidi, { scenario, product: stock?.[1] || product });
        if (stock) {
          // Supply the metadata of a validated stock package. The vendor firmware is not a test fixture.
          await page.route(url, (route) => route.fulfill({ contentType: "text/html",
            body: html.replace("const meta = ", `validateStockPackage = async () => ({
              image: new Uint8Array(2048), version: "${scenario}", product: "${stock[1]}"
            });\nconst meta = `) }));
        }
        await page.goto(url);
        await page.locator("#go:enabled").waitFor();
        const button = stock ? "#stock-go" : "#go";
        if (stock) {
          await page.locator("details:has(#stock-file) > summary").click();
          await page.locator("#stock-file").setInputFiles({
            name: "FM-1.fwsc", mimeType: "application/octet-stream", buffer: Buffer.alloc(stock[0]),
          });
          await page.locator(button + ":enabled").click();
          await page.waitForFunction(() => document.querySelector("#status").textContent.includes("already saved"));
          assert.deepEqual(await page.evaluate(() => window.midiStats.upgrades), []);
          await page.locator("#stock-recovery").check();
          page.once("dialog", (dialog) => dialog.dismiss());
          await page.locator(button + ":enabled").click();
          await page.locator(button + ":enabled").waitFor();
          assert.deepEqual(await page.evaluate(() => window.midiStats.upgrades), []);
          page.once("dialog", (dialog) => dialog.accept());
        }
        await page.locator(button + ":enabled").click();
        await page.locator(button + ":enabled").waitFor({ timeout: 20000 });
        const text = await page.locator("#status").textContent();
        const log = await page.locator("#log").textContent();
        const progress = await page.locator("#bar").evaluate((bar) => bar.value);
        const { stats, statuses } = await page.evaluate(() => ({
          stats: window.midiStats, statuses: window.installerStatuses,
        }));
        console.log(scenario, text, JSON.stringify(stats.upgrades), JSON.stringify(stats.probes));
        assert.match(text, status);
        const done = text === "Done";
        assert.equal(statuses.filter((value) => value === "Done").length, done ? 1 : 0);
        assert.equal(log.split("\n").filter((line) => line.startsWith("done ")).length, done ? 1 : 0);
        assert.equal(progress === 100, done);
        assert.ok(!stats.probes.includes("KEYS_001"));
        assert.deepEqual(stats.upgrades.map(([unit]) => unit),
          { fresh: ["A", "A"], competing: [] }[scenario] || ["A"]);
        if (done || scenario === "another" || scenario === "rescue") assert.ok(statuses.includes("Restarting"));
        if (scenario !== "fresh") assert.equal(stats.open, 0);
      } finally {
        await page.close();
      }
    }
  } finally {
    await browser?.close();
    server.close();
  }
}

run().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
