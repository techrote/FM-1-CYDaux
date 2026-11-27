#!/usr/bin/env python3
"""Exercise real Chromium rendering/Web Audio against the actual native engine.

Requires playwright plus Chromium. This measures software execution only; a
headless runner does not establish audible speaker output or hardware latency.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import shutil
import subprocess
import sys
import threading
import time


def main():
    from playwright.sync_api import sync_playwright
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",required=True,type=Path)
    parser.add_argument("--output",type=Path,default=Path("build/browser-evidence"))
    parser.add_argument("--chromium",help="Optional installed browser path; omitted uses Playwright bundled Chromium")
    args=parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    server=subprocess.Popen([sys.executable,str(Path(__file__).with_name("run.py")),"--binary",str(args.binary.resolve()),"--no-open","--port","0"],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    lines=queue.Queue()
    def read_lines():
        for line in server.stdout: lines.put(line)
    threading.Thread(target=read_lines,daemon=True).start()
    receipt={"kind":"actual Chromium + native C++ engines + Web Audio scheduling","physical_audio_output_verified":False,"instruments":[]}
    try:
        deadline=time.monotonic()+20;base=None
        while time.monotonic()<deadline:
            try: line=lines.get(timeout=0.2)
            except queue.Empty: continue
            match=re.search(r"http://127\.0\.0\.1:\d+",line)
            if match: base=match.group();break
        if not base: raise RuntimeError("Browser qualification server startup failed")
        with sync_playwright() as playwright:
            browser=playwright.chromium.launch(headless=True,executable_path=args.chromium,args=["--no-sandbox"])
            page=browser.new_page(viewport={"width":1280,"height":1080},device_scale_factor=1)
            errors=[];page.on("pageerror",lambda error:errors.append(str(error)))
            page.add_init_script("""window.__pcm={blocks:0,nonzero:0,peak:0,finite:true};
              const original=AudioBuffer.prototype.copyToChannel;
              AudioBuffer.prototype.copyToChannel=function(source,...args){
                __pcm.blocks++;for(const x of source){if(x!==0)__pcm.nonzero++;__pcm.peak=Math.max(__pcm.peak,Math.abs(x));if(!Number.isFinite(x))__pcm.finite=false;}
                return original.call(this,source,...args);};""")
            page.goto(base)
            page.wait_for_function("state && state.profiles.length>=6")
            for instrument in ("xenoresonator-native","zaaggenz-native"):
                page.select_option("#profile",instrument)
                page.wait_for_function("state && state.companionReady && state.transport===3")
                # Page state intentionally survives a companion profile change.
                # The previous instrument finishes on Sequence, not Play.
                page.click('[data-page="play"]')
                page.wait_for_function("state.page===0 && state.controls[0].enabled")
                page.click("#audioStart")
                page.wait_for_function("audioPlayer.running && audioPlayer.blocks>0")
                page.locator("#screen").focus()
                start=page.evaluate("JSON.parse(JSON.stringify(window.__pcm))")
                page.keyboard.down("Space")
                page.wait_for_function("state.noteHeld")
                page.wait_for_timeout(300)
                page.keyboard.up("Space")
                page.wait_for_function("!state.noteHeld")
                pcm=page.evaluate("JSON.parse(JSON.stringify(window.__pcm))")
                if pcm["nonzero"]<=start["nonzero"] or not pcm["finite"]:
                    raise RuntimeError(instrument+" produced no finite nonzero Web Audio PCM")
                # Shared renderer remains the touch surface; test final drag.
                box=page.locator("#screen").bounding_box()
                def point(x,y):return(box["x"]+x*box["width"]/320,box["y"]+y*box["height"]/240)
                page.mouse.move(*point(15,80));page.mouse.down()
                page.wait_for_function("state.controls[0].value===0")
                page.mouse.move(*point(149,80),steps=8);page.mouse.up()
                page.wait_for_function("state.controls[0].value===127")
                page.click('[data-page="sequence"]')
                page.wait_for_function("state.page===4")
                page.evaluate('control("sequence")')
                page.wait_for_timeout(1100)
                stats=page.evaluate("({blocks:audioPlayer.blocks,underruns:audioPlayer.underruns,nodes:audioPlayer.nodes.size,ahead:audioPlayer.nextTime-audioPlayer.context.currentTime,context:audioPlayer.context.state,pcm:window.__pcm,source:state.audio})")
                if stats["nodes"]>12 or stats["ahead"]>0.25 or stats["context"]!="running":
                    raise RuntimeError("Browser audio scheduling exceeded bounds")
                page.screenshot(path=str(args.output/(instrument+"-browser.png")),full_page=True)
                page.click("#panic")
                page.wait_for_function("!state.noteHeld")
                page.click("#audioStart")
                page.wait_for_function("!audioPlayer.running && audioPlayer.nodes.size===0 && !state.audio.clockRunning")
                receipt["instruments"].append({"id":instrument,"software_web_audio":stats,"touch_control":True,"keyboard_release":True,"stop_flush":True})
            # Responsive browser shell still hosts the exact 320x240 framebuffer.
            page.set_viewport_size({"width":390,"height":844})
            page.screenshot(path=str(args.output/"mobile-browser.png"),full_page=True)
            if page.evaluate("document.documentElement.scrollWidth>window.innerWidth"):
                raise RuntimeError("Mobile shell overflows horizontally")
            if errors: raise RuntimeError("Browser JavaScript errors: "+str(errors))
            receipt["browser_version"]=browser.version
            browser.close()
        receipt["result"]="PASS"
        receipt["screenshots"]={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(args.output.glob("*.png"))}
        (args.output/"receipt.json").write_text(json.dumps(receipt,indent=2)+"\n")
        print(json.dumps(receipt,indent=2))
    finally:
        server.terminate()
        try:server.wait(timeout=5)
        except subprocess.TimeoutExpired:server.kill();server.wait(timeout=5)


if __name__=="__main__":main()
