#!/usr/bin/env node
"use strict";
// Concurrency and framing unit tests with a deterministic Web Audio test double.
// Actual Chromium/Web Audio execution is qualified separately.
const assert=require("node:assert/strict");
const NativeAudioPlayer=require("./audio.js");
const delay=ms=>new Promise(resolve=>setTimeout(resolve,ms));
function deferred(){let resolve,reject;const promise=new Promise((done,fail)=>{resolve=done;reject=fail;});return{promise,resolve,reject};}
function block({rate=24000,generation=1,serial=1,frames=1024}={}){
  const result=new ArrayBuffer(24+frames*2),view=new DataView(result);
  view.setUint32(0,0x41445943,true);view.setUint32(4,1,true);view.setUint32(8,rate,true);
  view.setUint32(12,generation,true);view.setUint32(16,serial,true);view.setUint32(20,frames,true);
  for(let i=0;i<frames;i++)view.setInt16(24+i*2,i%2?8000:-8000,true);
  return result;
}
class Context {
  constructor(){this.currentTime=0;this.state="running";this.destination={};this.started=[];Context.latest=this;}
  resume(){return Context.resumeGate?Context.resumeGate.promise:Promise.resolve();}
  createGain(){return{gain:{value:0,setTargetAtTime(){}},connect(){}};}
  createBuffer(channels,frames,rate){assert.equal(channels,1);return{frames,rate,data:null,copyToChannel(data,channel){assert.equal(channel,0);this.data=data.slice();}};}
  createBufferSource(){const context=this;return{buffer:null,connect(){},disconnect(){},start(at){context.started.push({at,buffer:this.buffer});},stop(){if(this.onended)this.onended();}};}
}
global.window={AudioContext:Context};
async function main(){
  const parsed=NativeAudioPlayer.decode(block());
  assert.equal(parsed.rate,24000);assert.equal(parsed.frames,1024);
  assert.equal(parsed.samples[0],-8000/32768);
  for(const malformed of [new ArrayBuffer(10),block({rate:0}),block({frames:4097})])
    assert.throws(()=>NativeAudioPlayer.decode(malformed));
  const corrupt=block();new DataView(corrupt).setUint32(4,2,true);
  assert.throws(()=>NativeAudioPlayer.decode(corrupt));

  // Stop arriving while AudioContext.resume is pending must win the race.
  Context.resumeGate=deferred();const controls=[];
  const startRace=new NativeAudioPlayer(async(name,value)=>controls.push([name,value]),()=>{});
  const starting=startRace.start();await startRace.stop();Context.resumeGate.resolve();await starting;
  assert.equal(startRace.running,false);assert.equal(startRace.nodes.size,0);
  assert.deepEqual(controls.at(-1),["audio-clock","off"]);
  Context.resumeGate=null;

  // A late clock-on acknowledgement after pagehide/Stop cannot reopen an
  // abandoned lease, but its cleanup must never turn off a replacement run.
  for(const replacement of [false,true]){
    const clockOn=deferred();let first=true,clock=false;
    const command=async(name,value)=>{
      if(value==="on"&&first){first=false;await clockOn.promise;}
      clock=value==="on";
    };
    const pcm=deferred();global.fetch=()=>pcm.promise;
    const leaseRace=new NativeAudioPlayer(command,()=>{});
    const oldStart=leaseRace.start();await delay(1);
    await leaseRace.stop();
    if(replacement)await leaseRace.start();
    clockOn.resolve();await oldStart;
    assert.equal(clock,replacement,"late startup cleanup respects replacement ownership");
    assert.equal(leaseRace.running,replacement);
    if(replacement){await leaseRace.stop();pcm.resolve({ok:true,arrayBuffer:async()=>block()});await leaseRace.pumpTask;}
  }

  // A PCM response already in flight when Stop is pressed must be discarded.
  const pending=deferred();global.fetch=()=>pending.promise;
  const late=new NativeAudioPlayer(async()=>{},()=>{});await late.start();await late.stop();
  pending.resolve({ok:true,arrayBuffer:async()=>block()});await delay(5);
  assert.equal(Context.latest.started.length,0);assert.equal(late.nodes.size,0);

  // Stop/Restart is a distinct run: a retired fetch cannot create a second
  // pump or let its eventual failure turn off the newly running player.
  for(const failOld of [false,true]){
    const oldResponse=deferred(),newResponse=deferred();let fetches=0;
    global.fetch=()=>{fetches++;return fetches===1?oldResponse.promise:newResponse.promise;};
    const commands=[];
    const restarted=new NativeAudioPlayer(async(name,value)=>commands.push(value),()=>{});
    await restarted.start();await restarted.stop();const restarting=restarted.start();
    await delay(5);
    assert.equal(fetches,1,"new lease waits for retired native render");
    assert.deepEqual(commands,["on","off"]);
    if(failOld)oldResponse.reject(new Error("Retired connection failed"));
    else oldResponse.resolve({ok:true,arrayBuffer:async()=>block()});
    await restarting;await delay(10);
    assert.equal(fetches,2,"retired run must never issue another fetch");
    assert.equal(restarted.running,true,"retired failure must not stop a newer run");
    assert.equal(Context.latest.started.length,0);
    await restarted.stop();newResponse.resolve({ok:true,arrayBuffer:async()=>block()});await delay(5);
  }

  // A superseded preset/session must never schedule old-generation sound.
  global.fetch=async()=>({ok:true,arrayBuffer:async()=>block({generation:1})});
  const stale=new NativeAudioPlayer(async()=>{},()=>{});stale.synchronize(2);await stale.start();await delay(20);await stale.stop();
  assert.equal(Context.latest.started.length,0);

  // At a frozen output clock, the player must stop fetching at its scheduling
  // horizon, preserving the native sample rate and finite actual PCM values.
  let serial=0;global.fetch=async()=>({ok:true,arrayBuffer:async()=>block({serial:++serial})});
  const bounded=new NativeAudioPlayer(async()=>{},()=>{});bounded.synchronize(1);await bounded.start();await delay(35);
  assert.ok(Context.latest.started.length>=2 && Context.latest.started.length<=3);
  assert.ok(bounded.nodes.size<=3);assert.ok(bounded.nextTime<=0.2);
  for(const source of Context.latest.started){assert.equal(source.buffer.rate,24000);assert.equal(source.buffer.data[0],-8000/32768);}
  await bounded.stop();assert.equal(bounded.nodes.size,0);

  // A duplicate PCM serial is refused, rather than replayed as a new block.
  const reports=[];global.fetch=async()=>({ok:true,arrayBuffer:async()=>block({serial:1})});
  const duplicate=new NativeAudioPlayer(async()=>{},stats=>reports.push(stats));await duplicate.start();await delay(10);
  assert.equal(duplicate.running,false);assert.ok(reports.some(r=>r.error&&/Stale/.test(r.error.message)));
  assert.equal(duplicate.nodes.size,0);
  console.log("Audio player framing/concurrency: PASS (test double; real browser is a separate gate)");
}
main().catch(error=>{console.error(error);process.exitCode=1;});
