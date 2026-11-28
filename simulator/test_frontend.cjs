#!/usr/bin/env node
"use strict";

// Exercise the shipped frontend against the actual HTTP/native engine. The DOM
// shim checks event behavior; it is deliberately not a browser rendering test.
// Requires Node 18+. Usage: node simulator/test_frontend.cjs [native-binary]
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const assert = require("node:assert/strict");
const {spawn} = require("node:child_process");

class Element {
  constructor(tag = "DIV") {
    Object.assign(this, {tagName: tag, listeners: {}, dataset: {}, options: [],
      style: {}, value: "", textContent: "", checked: true, scrollHeight: 0});
  }
  addEventListener(name, callback) { this.listeners[name] = callback; }
  appendChild(node) { this.options.push(node); }
  append(...nodes) { this.options.push(...nodes); }
  replaceChildren(...nodes) { this.options = nodes; }
  focus() {}
  setPointerCapture() {}
  getContext() { return {drawImage() {}, imageSmoothingEnabled: false}; }
  getBoundingClientRect() { return {left: 0, top: 0, width: 640, height: 480}; }
}

const root = path.resolve(__dirname, "..");
const python = process.env.PYTHON || (process.platform === "win32" ? "python" : "python3");
const args = [path.join(__dirname, "run.py"), "--no-open", "--port", "0"];
if (process.argv[2]) args.push("--binary", path.resolve(process.argv[2]));
const server = spawn(python, args, {cwd: root, stdio: ["ignore", "pipe", "inherit"]});

async function main() {
  const base = await new Promise((resolve, reject) => {
    let output = "";
    const timeout = setTimeout(() => reject(new Error("Simulator startup timed out")), 30000);
    server.on("error", error => { clearTimeout(timeout); reject(error); });
    server.on("exit", code => {
      clearTimeout(timeout);
      reject(new Error("Simulator stopped: " + code));
    });
    server.stdout.on("data", data => {
      output += data;
      const match = output.match(/http:\/\/127\.0\.0\.1:\d+/);
      if (match) { clearTimeout(timeout); resolve(match[0]); }
    });
  });

  const ids = ["screen", "profile", "transport", "description", "version", "setupHint",
    "notice", "connect", "disconnect", "counts", "values", "midi", "echo", "error", "audioStart", "audioStatus", "volume", "panic", "chronoMode", "ecoMode"];
  const elements = Object.fromEntries(ids.map(id => [id,
    new Element(id === "screen" ? "CANVAS" : "DIV")]));
  elements.transport.options = [{}, {}, {}, {}];
  const document = new Element();
  Object.assign(document, {hidden: false,
    getElementById: id => elements[id],
    createElement: tag => new Element(tag.toUpperCase()),
    querySelectorAll: () => []});
  const context = {document, window: new Element(), Image: class {},
    fetch: (url, options) => fetch(base + url, options),
    // Delay every pointer move to force the coalesced-release edge case.
    setTimeout: () => 1, clearTimeout() {}, setInterval() {}, console};
  vm.createContext(context);
  const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
  vm.runInContext(fs.readFileSync(path.join(__dirname, "audio.js"), "utf8"), context);
  vm.runInContext(html.match(/<script>([\s\S]*?)<\/script>/)[1], context);
  const run = async code => await vm.runInContext(code, context);
  const event = (x, y, extra = {}) => ({pointerId: 1, button: 0,
    clientX: x * 2, clientY: y * 2, preventDefault() {},
    target: elements.screen, ...extra});

  await run('control("profile","melodee")');
  await run('control("transport","wifi")');
  await run('control("connect")');
  await run('control("echo","off")');
  await run('control("page","play")');
  assert.equal(elements.error.textContent, "");

  elements.screen.listeners.pointerdown(event(15, 80));
  elements.screen.listeners.pointermove(event(120, 80));
  elements.screen.listeners.pointerup(event(149, 80));
  await run("queue");
  const state = await run("state");
  assert.equal(state.controls[0].value, 127, "release carries the final drag value");
  assert.equal(state.connection, 2);
  assert.equal(state.mockPeer, true, "simulation stays explicitly marked");

  document.listeners.keydown(event(0, 0, {code: "Space", repeat: false}));
  await run("queue");
  assert.equal((await run("state")).noteHeld, true);
  document.listeners.keyup(event(0, 0, {code: "Space"}));
  await run("queue");
  assert.equal((await run("state")).noteHeld, false);

  elements.screen.listeners.pointerdown(event(160, 177));
  await run("queue");
  assert.equal((await run("state")).noteHeld, true);
  elements.screen.listeners.lostpointercapture();
  await run("queue");
  assert.equal((await run("state")).noteHeld, false);

  const revision = (await run("state")).revision;
  await run('update({...state,revision:state.revision-1,profile:"invalid"})');
  assert.equal((await run("state")).revision, revision);
  assert.equal(elements.error.textContent, "");
  await elements.chronoMode.listeners.click();
  assert.equal((await run("state")).chrono.active, true);
  elements.screen.listeners.pointerdown(event(15, 48));
  elements.screen.listeners.pointerup(event(15, 48));
  await run("queue");
  assert.equal((await run("state")).chrono.lanes[0].mask, 0x48);
  await elements.chronoMode.listeners.click();
  assert.equal((await run("state")).chrono.active, false);
  await elements.ecoMode.listeners.click();
  assert.equal((await run("state")).eco.active, true);
  elements.screen.listeners.pointerdown(event(20, 110));
  elements.screen.listeners.pointermove(event(90, 120));
  elements.screen.listeners.pointerup(event(153, 158));
  await run("queue");
  assert.equal((await run("state")).eco.fertility, 255);
  assert.equal((await run("state")).eco.pressure, 255);
  await elements.chronoMode.listeners.click();
  assert.equal((await run("state")).eco.active, false);
  assert.equal((await run("state")).chrono.active, true);
  await elements.chronoMode.listeners.click();
  assert.equal((await run("state")).chrono.active, false);
  console.log("Frontend events: PASS (HTTP/native controller, final drag, keyboard note, pointer cancellation, stale state)");
}

main().catch(error => { console.error(error); process.exitCode = 1; })
  .finally(() => server.kill("SIGINT"));
