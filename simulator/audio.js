/* Actual native-engine PCM player. No oscillators, synth emulation or mock sound.
 * The bounded queue is a desktop evaluation transport, not a radio-latency claim. */
"use strict";
class NativeAudioPlayer {
  constructor(control, report) {
    this.control = control;
    this.report = report;
    this.context = null;
    this.gain = null;
    this.running = false;
    this.epoch = 0;
    this.nextTime = 0;
    this.nodes = new Set();
    this.expectedGeneration = null;
    this.generation = null;
    this.serial = 0;
    this.underruns = 0;
    this.blocks = 0;
    this.volume = 0.6;
  }
  async start() {
    if (this.running) return;
    const Audio = window.AudioContext || window.webkitAudioContext;
    if (!Audio) throw new Error("This browser does not provide Web Audio");
    if (!this.context) {
      this.context = new Audio({latencyHint: "interactive"});
      this.gain = this.context.createGain();
      this.gain.gain.value = this.volume;
      this.gain.connect(this.context.destination);
    }
    await this.context.resume();
    await this.control("audio-clock", "on");
    this.running = true;
    this.flush();
    this.report({running: true, blocks: this.blocks, underruns: this.underruns, ahead: 0});
    this.pump();
  }
  async stop() {
    this.running = false;
    this.flush();
    this.report({running: false, blocks: this.blocks, underruns: this.underruns, ahead: 0});
    await this.control("audio-clock", "off");
  }
  setVolume(value) {
    this.volume = Math.min(1, Math.max(0, Number(value) || 0));
    if (this.gain) this.gain.gain.setTargetAtTime(this.volume, this.context.currentTime, 0.015);
  }
  synchronize(generation) {
    if (!Number.isInteger(generation)) return;
    if (generation !== this.expectedGeneration) {
      this.flush();
      this.expectedGeneration = generation;
    }
  }
  flush() {
    ++this.epoch;
    for (const node of this.nodes) {
      try { node.stop(); } catch (_) {}
      node.disconnect();
    }
    this.nodes.clear();
    this.generation = null;
    this.serial = 0;
    this.nextTime = this.context ? this.context.currentTime + 0.045 : 0;
  }
  static decode(data) {
    if (!(data instanceof ArrayBuffer) || data.byteLength < 26) throw new Error("Truncated native PCM block");
    const view = new DataView(data);
    if (view.getUint32(0, true) !== 0x41445943 || view.getUint32(4, true) !== 1)
      throw new Error("Incompatible native PCM block");
    const rate = view.getUint32(8, true), generation = view.getUint32(12, true);
    const serial = view.getUint32(16, true), frames = view.getUint32(20, true);
    if (![24000, 44100].includes(rate) || frames < 1 || frames > 4096 || data.byteLength !== 24 + frames * 2)
      throw new Error("Invalid native PCM dimensions");
    const samples = new Float32Array(frames);
    for (let i = 0; i < frames; ++i) samples[i] = view.getInt16(24 + i * 2, true) / 32768;
    return {rate, generation, serial, frames, samples};
  }
  async pump() {
    while (this.running) {
      const epoch = this.epoch;
      try {
        if (this.context.state !== "running") {
          await this.stop();
          return;
        }
        if (this.nextTime - this.context.currentTime > 0.11) {
          await new Promise(resolve => setTimeout(resolve, 12));
          continue;
        }
        const response = await fetch("/api/audio", {
          method: "POST", headers: {"Content-Type": "application/json"},
          body: JSON.stringify({frames: 1024})
        });
        if (!response.ok) throw new Error("Native audio transport failed");
        const block = NativeAudioPlayer.decode(await response.arrayBuffer());
        if (!this.running || epoch !== this.epoch) continue;
        if (this.expectedGeneration !== null && block.generation !== this.expectedGeneration) {
          // A late response from a superseded preset/session is never audible.
          await new Promise(resolve => setTimeout(resolve, 8));
          continue;
        }
        if (this.serial && ((block.serial - this.serial) >>> 0) > 0x7fffffff)
          throw new Error("Stale native audio sequence");
        this.serial = block.serial;
        if (this.generation !== block.generation) {
          this.generation = block.generation;
          this.nextTime = this.context.currentTime + 0.045;
        }
        if (this.nextTime < this.context.currentTime + 0.008) {
          ++this.underruns;
          this.nextTime = this.context.currentTime + 0.035;
        }
        // At most about 153 ms (24 kHz) of scheduling; never grow a backlog
        // while a tab is suspended. Web Audio performs rate conversion.
        if (this.nodes.size >= 12 || this.nextTime - this.context.currentTime > 0.24)
          throw new Error("Native audio queue exceeded its bound");
        const buffer = this.context.createBuffer(1, block.frames, block.rate);
        buffer.copyToChannel(block.samples, 0);
        const source = this.context.createBufferSource();
        source.buffer = buffer;
        source.connect(this.gain);
        this.nodes.add(source);
        source.onended = () => { this.nodes.delete(source); source.disconnect(); };
        source.start(this.nextTime);
        this.nextTime += block.frames / block.rate;
        ++this.blocks;
        this.report({running: true, blocks: this.blocks, underruns: this.underruns,
                     ahead: this.nextTime - this.context.currentTime, rate: block.rate});
      } catch (error) {
        this.running = false;
        this.flush();
        this.report({running: false, error, blocks: this.blocks, underruns: this.underruns, ahead: 0});
        try { await this.control("audio-clock", "off"); } catch (_) {}
      }
    }
  }
}
if (typeof module !== "undefined") module.exports = NativeAudioPlayer;
