"use strict";
// Deterministic delayed-fetch regression. No browser/hardware/PCM quality claim.
const assert = require("node:assert/strict");
const NativeAudioPlayer = require("./audio.js");
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return {promise, resolve, reject};
};
const requests = [], commands = [], reports = [];
let resumeGate = null;
global.window = {AudioContext: class {
  constructor() { this.currentTime = 0; this.state = "running"; this.destination = {}; }
  createGain() { return {gain: {}, connect() {}}; }
  async resume() { if (resumeGate) await resumeGate.promise; }
  createBuffer() { return {copyToChannel() {}}; }
  createBufferSource() { return {connect() {}, disconnect() {}, start() {}, stop() {}}; }
}};
global.fetch = () => {
  const request = deferred(); requests.push(request); return request.promise;
};
const turn = () => new Promise(resolve => setImmediate(resolve));
async function main() {
  const p = new NativeAudioPlayer(async (name, value) => commands.push([name,value]), r => reports.push(r));
  await p.start();
  assert.equal(requests.length,1);
  await p.stop();
  const restart = p.start();
  await turn();
  assert.equal(requests.length,1,"restart must wait for old PCM request");
  assert.deepEqual(commands.map(c=>c[1]),["on","off"]);
  requests[0].reject(new Error("old fetch failed"));
  await restart;
  assert.equal(p.running,true,"old rejection must not stop new lease");
  assert.equal(requests.length,2);
  assert.deepEqual(commands.map(c=>c[1]),["on","off","on"]);
  assert.equal(reports.filter(r=>r.error).length,0);
  const pcm = new ArrayBuffer(24+2048);
  const view = new DataView(pcm);
  [0x41445943,1,24000,1,1,1024].forEach((v,i)=>view.setUint32(i*4,v,true));
  requests[1].resolve({ok:true,arrayBuffer:async()=>pcm});
  await turn();
  assert.equal(p.blocks,1);
  assert.equal(requests.length,3,"exactly one live pump");
  await p.stop();
  requests[2].reject(new Error("stopped"));
  await p.pumpTask;
  assert.equal(p.nodes.size,0);
  assert.equal(reports.filter(r=>r.error).length,0);
  resumeGate = deferred();
  const interrupted = p.start();
  await turn();
  assert.equal(p.starting,true);
  await p.stop();
  resumeGate.resolve();
  await interrupted;
  assert.equal(p.running,false);
  assert.equal(p.starting,false);
  assert.equal(requests.length,3,"stop during resume must not launch a pump");
  assert.deepEqual(commands.map(c=>c[1]),["on","off","on","off","off"]);
  console.log("Audio lifecycle: PASS (old fetch drained, rejection isolated, single pump, interrupted start)");
}
main().catch(e=>{console.error(e);process.exitCode=1;});
