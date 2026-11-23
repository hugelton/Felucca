// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// The FM-1 USB-MIDI update protocol over WebMIDI (after tools/fm1_install.py).
// Step 1: the running firmware (official
// or Felucca) reads parts of the package and stages the update loader. Step 2:
// the loader reads the whole image and writes it. Both steps are "the device
// asks, we answer": cmd 0x30 read requests on the logical image.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const IS_OTA = (id) => /^ota-/i.test(id.model);
const FM1_PORT = /fm-1|felucca|ota|composite|sinco|usb[- ]midi/i;
const REBOOT_SETTLE_MS = 3000;

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// mismatch (detail: the identity the device reports), ambiguous, badreq
function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : null;
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output, opened) {
    this.input = input; this.output = output; this.opened = opened; this.q = []; this.waiter = null;
    input.onmidimessage = (ev) => {
      if (ev.data[0] !== 0xF0) return;
      this.q.push(Array.from(ev.data));
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    try { this.output.send(bytes); return true; } catch (_) { return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
  async release() {
    this.close();
    await Promise.allSettled(this.opened.map(async (port) => port.close()));
  }
}

async function openLink(input, output) {
  const opened = [];
  try {
    for (const port of [input, output]) {
      if (port.connection !== "open") {
        opened.push(port);
        await port.open();
      }
    }
    return new Link(input, output, opened);
  } catch (_) {
    await Promise.allSettled(opened.map(async (port) => port.close()));
    return null;
  }
}

async function handshake(link, tries = 3) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + 1000;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

function candidatePorts(ports) {
  return [...ports.values()].filter((port) => port.state !== "disconnected" && FM1_PORT.test(port.name || ""));
}

const samePair = (a, b) => a?.input === b?.input && a?.output === b?.output;

// Web MIDI does not expose a USB serial shared by the app and loader. Track
// one pair before writing and at most one replacement through re-enumeration.
class ResumeTarget {
  constructor(access, loader) {
    this.access = access;
    this.loader = loader;
    this.onChange = () => {
      try { this.inspect(); } catch (error) { this.error = error; }
    };
    access.addEventListener("statechange", this.onChange);
  }

  inspect() {
    if (this.error) throw this.error;
    const [inputs, outputs] = [this.access.inputs, this.access.outputs].map(candidatePorts);
    const [input] = inputs, [output] = outputs;
    const ambiguous = () => fail("ambiguous", "Connect only the FM-1 you want to update.");
    if (inputs.length > 1 || outputs.length > 1) throw ambiguous();
    if (!input || !output) return null;
    if (input.name !== output.name) throw ambiguous();
    const pair = { input, output };
    if (samePair(pair, this.loader)) {
      if (this.returning) throw ambiguous();
      return pair;
    }
    if (!this.rebootAt || input === this.loader.input || output === this.loader.output) throw ambiguous();
    if (this.returning && !samePair(pair, this.returning)) throw ambiguous();
    if (!this.returning) {
      this.returning = pair;
      this.rebootAt = Date.now() + REBOOT_SETTLE_MS;
    }
    return pair;
  }

  active() { return samePair(this.inspect(), this.loader) && !this.loader.lost; }
  restarting() { this.rebootAt = Date.now() + REBOOT_SETTLE_MS; }
  close() { this.access.removeEventListener("statechange", this.onChange); }
}

export class Updater {
  constructor(access, log) {
    this.access = access;
    this.log = log || (() => {});
  }

  // every in/out pair with the same port name that answers the handshake
  async find(filter) {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected") continue;
      if (!FM1_PORT.test(input.name || "")) continue;   // never probe other gear
      const output = outs.find((o) => o.name === input.name && o.state !== "disconnected");
      if (!output) continue;
      const dev = await this.probe({ input, output }, filter);
      if (dev) return dev;
    }
    return null;
  }

  async probe({ input, output }, filter) {
    const link = await openLink(input, output);
    if (!link) return null;
    let keep = false;
    try {
      const id = await handshake(link, 2);
      if (!id || link.lost || (filter && !filter(id))) return null;
      keep = true;
      return { link, id, name: input.name };
    } finally {
      if (!keep) await link.release();
    }
  }

  async waitFor(filter, ms, target) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      if (!target) {
        const dev = await this.find(filter);
        if (dev) return dev;
      } else {
        const pair = target.inspect();
        if (pair && Date.now() >= target.rebootAt) {
          const dev = await this.probe(pair, filter);
          if (dev) {
            await dev.link.release();
            if (samePair(pair, target.inspect())) return dev;
          }
        }
      }
      await sleep(target ? 250 : 1000);
    }
    return null;
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress, active = () => !link.lost) {
    let served = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!active()) return { served, finished: false, lost: true };
      if (!pkt) {
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        if (!link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]))) {
          return { served, finished: false, lost: true };
        }
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!active() || !link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      if (onProgress) onProgress(served, r.addr);
    }
  }

  // full install: running firmware -> loader -> new firmware
  async install(image, product, onStep) {
    const step = onStep || (() => {});
    const dev = await this.find((id) => !IS_OTA(id));
    if (!dev) throw fail("notfound", "FM-1 not found (USB cable, and no other app using it?)");
    const [model] = product.split("_");
    if (dev.id.model !== model) { dev.link.close(); throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`); }
    step("start", dev.id.text);
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n) => step("verify", n));
    dev.link.close();
    if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(3000);
    const ota = await this.waitFor((id) => id.model === "ota-" + model, 30000);
    if (!ota) throw fail("noloader", "the update loader did not appear. Replug the USB cable and press Install again: the device stays in update mode until it is finished.");
    const s2 = await this.write(ota, image, step);
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `installed, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step, active = () => !ota.link.lost) {
    try {
      if (!active() || !ota.link.send(UPGRADE)) return { served: 0, finished: false, lost: true };
      await sleep(2000);
      return await this.serve(ota.link, image, 0xF0000000, 180000, (n) => step("write", n), active);
    } finally {
      ota.link.close();
    }
  }

  // Resume on the selected loader, then verify the app on the returning unit.
  async resume(image, onStep, product, selected) {
    const model = /^([^_]+)_\d+$/.exec(product)?.[1];
    const loader = (id) => id.model === "ota-" + model;
    let ota = selected, target;
    try {
      if (!model) throw fail("model", "An expected FM-1 product is required to resume.");
      ota ??= await this.find(loader);
      if (!ota) return false;
      if (!loader(ota.id)) throw fail("model", "The loader does not match the expected FM-1 model.");

      target = new ResumeTarget(this.access, ota.link);
      const step = onStep || (() => {});
      const result = await this.write(ota, image, step, () => target.active());
      if (result.lost) throw fail("lost", "The connection to the loader was lost during the write.");
      if (!result.finished) return false;

      target.restarting();
      step("reboot");
      const back = await this.waitFor((id) => id.model === model, REBOOT_SETTLE_MS + 40000, target);
      if (!back) throw fail("noreturn", "The FM-1 did not return after resuming the write.");
      if (back.id.text !== product) throw fail("mismatch", "The FM-1 reports a different firmware after resuming.", back.id.text);
      step("done", back.id.text);
      return true;
    } finally {
      target?.close();
      await ota?.link.release();
    }
  }
}
