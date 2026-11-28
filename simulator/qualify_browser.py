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
import subprocess
import sys
import threading
import time


def save_receipt(directory, receipt):
    receipt["screenshots"] = {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(directory.glob("*.png"))
    }
    (directory / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


def exercise_browser(playwright, args, base, receipt):
    browser = playwright.chromium.launch(
        headless=True, executable_path=args.chromium, args=["--no-sandbox"]
    )
    page = browser.new_page(
        viewport={"width": 1280, "height": 1080}, device_scale_factor=1
    )
    errors = []
    page.on("pageerror", lambda error: errors.append(str(error)))
    receipt["browser_version"] = browser.version
    try:
        # Observe the actual PCM handed to Web Audio without altering it.
        page.add_init_script("""window.__pcm={blocks:0,nonzero:0,peak:0,finite:true};
          const original=AudioBuffer.prototype.copyToChannel;
          AudioBuffer.prototype.copyToChannel=function(source,...args){
            __pcm.blocks++;for(const x of source){if(x!==0)__pcm.nonzero++;__pcm.peak=Math.max(__pcm.peak,Math.abs(x));if(!Number.isFinite(x))__pcm.finite=false;}
            return original.call(this,source,...args);};""")
        receipt["stage"] = "opening evaluator"
        page.goto(base)
        page.wait_for_function("state && state.profiles.length>=7")
        for instrument in ("xenoresonator-native", "zaaggenz-native", "grimoire-native"):
            receipt["stage"] = instrument + ": discovery"
            page.select_option("#profile", instrument)
            page.wait_for_function(
                "id => state && state.profile===id && state.companionReady && state.transport===3",
                arg=instrument,
            )
            # Page selection deliberately persists across instruments. Select
            # Play before testing a parameter, including the second instrument.
            page.click('[data-page="play"]')
            page.wait_for_function("state.page===0")
            receipt["stage"] = instrument + ": actual Web Audio PCM"
            page.click("#audioStart")
            page.wait_for_function("audioPlayer.running && audioPlayer.blocks>0")
            page.locator("#screen").focus()
            start = page.evaluate("JSON.parse(JSON.stringify(window.__pcm))")
            page.keyboard.down("Space")
            page.wait_for_function("state.noteHeld")
            page.wait_for_timeout(300)
            page.keyboard.up("Space")
            page.wait_for_function("!state.noteHeld")
            pcm = page.evaluate("JSON.parse(JSON.stringify(window.__pcm))")
            if pcm["nonzero"] <= start["nonzero"] or not pcm["finite"]:
                raise RuntimeError(instrument + " produced no finite nonzero Web Audio PCM")

            receipt["stage"] = instrument + ": captured touch to maximum"
            box = page.locator("#screen").bounding_box()
            def point(x, y):
                return (box["x"] + x * box["width"] / 320,
                        box["y"] + y * box["height"] / 240)
            # Shared control is x=8..155. End outside that rectangle to exercise
            # pointer capture and clamp, independent of the interior label.
            page.mouse.move(*point(15, 80))
            page.mouse.down()
            page.mouse.move(*point(170, 80), steps=8)
            page.mouse.up()
            page.wait_for_function("state.controls[0].value===127")

            receipt["stage"] = instrument + ": sequencer and bounded scheduling"
            page.click('[data-page="sequence"]')
            page.wait_for_function("state.page===4")
            page.evaluate('control("sequence")')
            page.wait_for_function("state.sequence.running")
            page.wait_for_timeout(1100)
            stats = page.evaluate("""({blocks:audioPlayer.blocks,
                underruns:audioPlayer.underruns,nodes:audioPlayer.nodes.size,
                ahead:audioPlayer.nextTime-audioPlayer.context.currentTime,
                context:audioPlayer.context.state,pcm:window.__pcm,source:state.audio,
                sequence:state.sequence})""")
            if stats["nodes"] > 12 or stats["ahead"] > 0.25 or stats["context"] != "running":
                raise RuntimeError("Browser audio scheduling exceeded bounds")
            if not stats["sequence"]["running"]:
                raise RuntimeError("Shared controller sequencer stopped unexpectedly")
            page.screenshot(path=str(args.output / (instrument + "-browser.png")), full_page=True)
            receipt["stage"] = instrument + ": panic and stop"
            page.click("#panic")
            page.wait_for_function("!state.noteHeld && !state.sequence.running")
            page.click("#audioStart")
            page.wait_for_function(
                "!audioPlayer.running && audioPlayer.nodes.size===0 && !state.audio.clockRunning"
            )
            receipt["instruments"].append({
                "id": instrument,
                "software_web_audio": stats,
                "note_pcm_nonzero_samples": pcm["nonzero"] - start["nonzero"],
                "touch_control_maximum": 127,
                "keyboard_release": True,
                "stop_flush": True,
            })
            save_receipt(args.output, receipt)

        receipt["stage"] = "chronomorph-native: actual touchscreen editor"
        page.click("#chronoMode")
        page.wait_for_function("state.chrono.active && !state.chrono.playing")
        box = page.locator("#screen").bounding_box()
        def chrono_tap(x, y):
            page.mouse.click(box["x"] + x * box["width"] / 320,
                             box["y"] + y * box["height"] / 240)
        for length in range(8, 17):
            chrono_tap(290, 30)
            page.wait_for_function("n => state.chrono.lanes[0].length===n", arg=length)
        chrono_tap(301, 48)
        page.wait_for_function("state.chrono.lanes[0].mask & 32768")
        page.click("#audioStart")
        page.wait_for_function("audioPlayer.running && state.audio.clockRunning")
        before = page.evaluate("window.__pcm.nonzero")
        chrono_tap(45, 220)
        page.wait_for_function("state.chrono.playing && state.chrono.lanes[0].ticks>3")
        page.wait_for_function("n => window.__pcm.nonzero>n", arg=before)
        stats = page.evaluate("""({source:state.audio, lanes:state.chrono.lanes,
            nodes:audioPlayer.nodes.size, ahead:audioPlayer.nextTime-audioPlayer.context.currentTime,
            blocks:audioPlayer.blocks, underruns:audioPlayer.underruns, pcm:window.__pcm})""")
        if stats["nodes"]>12 or stats["ahead"]>0.25 or not stats["pcm"]["finite"]:
            raise RuntimeError("Chronomorph Web Audio exceeded bounds")
        page.screenshot(path=str(args.output / "chronomorph-native-browser.png"), full_page=True)
        page.click("#panic")
        page.wait_for_function("!state.chrono.playing")
        page.click("#audioStart")
        page.wait_for_function("!audioPlayer.running && !state.audio.clockRunning && audioPlayer.nodes.size===0")
        page.click("#chronoMode")
        page.wait_for_function("!state.chrono.active")
        receipt["instruments"].append({"id":"chronomorph-native","software_web_audio":stats,
            "high_bit_edit":True,"native_clock":True,"stop_flush":True})
        save_receipt(args.output, receipt)

        receipt["stage"] = "ecophon-native: actual habitat editor"
        page.click("#ecoMode")
        page.wait_for_function("state.eco.active && !state.eco.running && !state.chrono.active")
        box = page.locator("#screen").bounding_box()
        chrono_tap(153, 158)
        page.wait_for_function("state.eco.fertility===255 && state.eco.pressure===255")
        chrono_tap(311, 120)
        page.wait_for_function("state.eco.sharing===255")
        chrono_tap(45, 193)
        page.wait_for_function("state.eco.fertility===180 && state.eco.sharing===200")
        page.click("#audioStart")
        page.wait_for_function("audioPlayer.running && state.audio.clockRunning")
        before = page.evaluate("window.__pcm.nonzero")
        chrono_tap(45, 220)
        page.wait_for_function("state.eco.running && state.eco.ticks>3")
        page.wait_for_function("n => window.__pcm.nonzero>n", arg=before)
        stats = page.evaluate("""({source:state.audio, ecology:state.eco,
            nodes:audioPlayer.nodes.size, ahead:audioPlayer.nextTime-audioPlayer.context.currentTime,
            blocks:audioPlayer.blocks, underruns:audioPlayer.underruns, pcm:window.__pcm})""")
        if stats["nodes"]>12 or stats["ahead"]>0.25 or not stats["pcm"]["finite"]:
            raise RuntimeError("Ecophon Web Audio exceeded bounds")
        if len(stats["ecology"]["agents"])!=8 or len(stats["ecology"]["resources"])!=4:
            raise RuntimeError("Ecophon telemetry dimensions changed")
        chrono_tap(195, 9)
        page.wait_for_function("state.eco.arcActive && state.eco.arcBlock>=2")
        chrono_tap(80, 127)
        page.wait_for_function("!state.eco.arcActive && state.eco.arcBlock===0")
        chrono_tap(195, 9)
        page.wait_for_function("state.eco.arcActive")
        page.screenshot(path=str(args.output / "ecophon-native-browser.png"), full_page=True)
        page.click("#panic")
        page.wait_for_function("!state.eco.running && !state.eco.arcActive && state.eco.arcBlock===0")
        page.click("#audioStart")
        page.wait_for_function("!audioPlayer.running && !state.audio.clockRunning && audioPlayer.nodes.size===0")
        page.click("#ecoMode")
        page.wait_for_function("!state.eco.active")
        receipt["instruments"].append({"id":"ecophon-native","software_web_audio":stats,
            "full_width_xy":True,"local_ecology":True,"stop_flush":True,"arc_manual_takeover":True})
        save_receipt(args.output, receipt)

        receipt["stage"] = "responsive browser shell"
        # The responsive shell still hosts the exact 320x240 framebuffer.
        page.set_viewport_size({"width": 390, "height": 844})
        page.screenshot(path=str(args.output / "mobile-browser.png"), full_page=True)
        if page.evaluate("document.documentElement.scrollWidth>window.innerWidth"):
            raise RuntimeError("Mobile shell overflows horizontally")
        if errors:
            raise RuntimeError("Browser JavaScript errors: " + str(errors))
        receipt["result"] = "PASS"
        receipt["stage"] = "complete"
        save_receipt(args.output, receipt)
    except Exception as error:
        receipt["result"] = "FAIL"
        receipt["error"] = type(error).__name__ + ": " + str(error)
        receipt["javascript_errors"] = errors
        # Capture before closing the context, including partial successes and
        # the actual shared controller state at the failed acceptance step.
        try:
            page.screenshot(path=str(args.output / "failure-browser.png"), full_page=True)
            receipt["failure_state"] = page.evaluate("""({
                state:typeof state==='undefined'?null:state,
                pcm:window.__pcm,
                viewport:{width:innerWidth,height:innerHeight},
                audio:typeof audioPlayer==='undefined'?null:{
                    running:audioPlayer.running,starting:audioPlayer.starting,
                    runToken:audioPlayer.runToken,nodes:audioPlayer.nodes.size,
                    blocks:audioPlayer.blocks,underruns:audioPlayer.underruns,
                    nextTime:audioPlayer.nextTime,
                    context:audioPlayer.context?{
                        state:audioPlayer.context.state,currentTime:audioPlayer.context.currentTime
                    }:null
                }
            })""")
        except Exception as capture_error:
            receipt["failure_capture_error"] = str(capture_error)
        save_receipt(args.output, receipt)
        print(json.dumps(receipt, indent=2), flush=True)
        raise
    finally:
        browser.close()


def main():
    from playwright.sync_api import sync_playwright
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/browser-evidence"))
    parser.add_argument("--chromium", help="Installed browser path; omitted uses Playwright bundled Chromium")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    server = subprocess.Popen([
        sys.executable, str(Path(__file__).with_name("run.py")), "--binary",
        str(args.binary.resolve()), "--no-open", "--port", "0"
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines = queue.Queue()
    def read_lines():
        for line in server.stdout:
            lines.put(line)
    threading.Thread(target=read_lines, daemon=True).start()
    receipt = {
        "kind": "actual Chromium + native C++ engines + Web Audio scheduling",
        "physical_audio_output_verified": False,
        "result": "RUNNING",
        "instruments": [],
    }
    try:
        deadline = time.monotonic() + 20
        base = None
        while time.monotonic() < deadline:
            try:
                line = lines.get(timeout=0.2)
            except queue.Empty:
                continue
            match = re.search(r"http://127\.0\.0\.1:\d+", line)
            if match:
                base = match.group()
                break
        if not base:
            raise RuntimeError("Browser qualification server startup failed")
        with sync_playwright() as playwright:
            exercise_browser(playwright, args, base, receipt)
        print(json.dumps(receipt, indent=2))
    finally:
        server.terminate()
        try:
            server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait(timeout=5)


if __name__ == "__main__":
    main()
