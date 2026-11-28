#!/usr/bin/env python3
"""Extract and execute a delivered evaluator, including its actual audio cores."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import queue
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from urllib.request import Request, urlopen
import zipfile

from package_contract import require_audio_rates, require_frame


def verify_hashes(directory: Path) -> dict:
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    for name, expected in manifest["files"].items():
        path = directory / name
        if path.resolve().is_relative_to(directory.resolve()) is False:
            raise RuntimeError("Manifest path escapes package")
        data = path.read_bytes()
        if len(data) != expected["size"] or hashlib.sha256(data).hexdigest() != expected["sha256"]:
            raise RuntimeError(f"Package integrity mismatch: {name}")
    for name in ("run.py", "index.html", "audio.js", "THIRD_PARTY.md"):
        if not (directory / name).is_file():
            raise RuntimeError(f"Incomplete evaluator: {name}")
    return manifest


def evaluate(directory: Path, standalone: bool = False) -> dict:
    suffix = ".exe" if os.name == "nt" else ""
    binary = directory / ("fm1-simulator" + suffix)
    if standalone:
        candidates = [p for p in directory.iterdir() if p.suffix.lower() == ".exe" and p.name != binary.name]
        if len(candidates) != 1:
            raise RuntimeError("Expected exactly one standalone evaluator executable")
        argv = [str(candidates[0]), "--no-open", "--port", "0"]
    else:
        argv = [sys.executable, str(directory / "run.py"), "--binary", str(binary), "--no-open", "--port", "0"]
    environment = os.environ.copy()
    if standalone and os.name == "nt":
        # Prove the bundled launcher does not discover the runner's installed
        # Python or compiler. Windows system DLL/utility paths remain present.
        system = Path(environment.get("SystemRoot", r"C:\Windows"))
        environment["PATH"] = os.pathsep.join((str(system / "System32"), str(system)))
        environment.pop("PYTHONHOME", None)
        environment.pop("PYTHONPATH", None)
    process = subprocess.Popen(argv, cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, encoding="utf-8", errors="replace", env=environment)
    lines = queue.Queue()
    def read_lines():
        for line in process.stdout:
            lines.put(line)
    reader = threading.Thread(target=read_lines, daemon=True)
    reader.start()
    receipt = {"launcher": "standalone" if standalone else "python", "path_with_spaces_unicode": True,
               "python_and_compilers_removed_from_path": standalone and os.name == "nt",
               "source_kind": "native-engine", "physical_qualification": False, "instruments": []}
    try:
        deadline = time.monotonic() + 30
        output = []
        base = None
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError("Extracted server exited: " + "".join(output))
            try:
                line = lines.get(timeout=0.25)
            except queue.Empty:
                continue
            output.append(line)
            match = re.search(r"http://127\.0\.0\.1:\d+", line)
            if match:
                base = match.group(0)
                break
        if not base:
            raise RuntimeError("Extracted server startup timeout: " + "".join(output))
        def get(path):
            with urlopen(base + path, timeout=5) as response:
                return response.read()
        def post(path, body):
            request = Request(base + path, json.dumps(body).encode(), {"Content-Type": "application/json"})
            with urlopen(request, timeout=5) as response:
                return response.read()
        def control(name, value=""):
            return json.loads(post("/api/control", {"name": name, "value": value}))
        if b"NativeAudioPlayer" not in get("/audio.js") or b"Enable sound" not in get("/"):
            raise RuntimeError("Extracted frontend is incomplete")
        for instrument, rate in (("xenoresonator-native",24000),("zaaggenz-native",44100),("grimoire-native",24000)):
            control("profile",instrument)
            control("transport","native")
            state=control("connect")
            if not state["companionReady"] or state["mockPeer"]:
                raise RuntimeError("Native handshake failed")
            control("page","play")
            control("audio-clock","on")
            state=json.loads(post("/api/touch",{"phase":"down","x":160,"y":177}))
            if not state["noteHeld"]:
                raise RuntimeError("Touchscreen did not hold a note")
            block=post("/api/audio",{"frames":4096})
            magic,version,sample_rate,generation,serial,frames=struct.unpack("<4sIIIII",block[:24])
            if (magic,version,sample_rate,frames)!=(b"CYDA",1,rate,4096) or len(block)!=8216:
                raise RuntimeError("Invalid extracted native audio block")
            pcm=struct.unpack("<4096h",block[24:])
            peak=max(abs(v) for v in pcm)
            if peak<=100:
                raise RuntimeError("Extracted engine produced no meaningful audio")
            frame=get("/api/frame.png")
            require_frame(frame, instrument)
            control("panic")
            quiet=post("/api/audio",{"frames":1024})[24:]
            if quiet!=bytes(2048):
                raise RuntimeError("Panic did not silence extracted engine")
            control("audio-clock","off")
            receipt["instruments"].append({"id":instrument,"rate":rate,"peak":peak,
                "pcm_sha256":hashlib.sha256(block[24:]).hexdigest(),
                "frame_sha256":hashlib.sha256(frame).hexdigest(),
                "source_revision":state["audio"]["sourceRevision"],
                "handshake":True,"touch_to_sound":True,"panic_silence":True})
        state=control("chrono","on")
        if not state["chrono"]["active"] or state["chrono"]["origin"]!="native-engine":
            raise RuntimeError("Extracted Chronomorph editor unavailable")
        def tap(x,y):
            return json.loads(post("/api/touch",{"phase":"tap","x":x,"y":y}))
        for _ in range(9):
            tap(290,30)
        edited=tap(301,48)
        if edited["chrono"]["lanes"][0]["length"]!=16 or not edited["chrono"]["lanes"][0]["mask"]&0x8000:
            raise RuntimeError("Extracted Chronomorph pattern edit failed")
        control("audio-clock","on")
        state=tap(45,220)
        if not state["chrono"]["playing"]:
            raise RuntimeError("Extracted Chronomorph start failed")
        block=post("/api/audio",{"frames":4096})
        magic,version,rate,generation,serial,frames=struct.unpack("<4sIIIII",block[:24])
        pcm=struct.unpack("<4096h",block[24:])
        peak=max(abs(v) for v in pcm)
        if (magic,version,rate,frames)!=(b"CYDA",1,24000,4096) or peak<=100:
            raise RuntimeError("Extracted Chronomorph PCM failed")
        frame=get("/api/frame.png")
        require_frame(frame, "chronomorph-native")
        control("panic")
        if post("/api/audio",{"frames":1024})[24:]!=bytes(2048):
            raise RuntimeError("Extracted Chronomorph panic failed")
        control("audio-clock","off")
        control("chrono","off")
        receipt["instruments"].append({"id":"chronomorph-native","rate":rate,"peak":peak,
            "pcm_sha256":hashlib.sha256(block[24:]).hexdigest(),
            "frame_sha256":hashlib.sha256(frame).hexdigest(),
            "source_revision":state["audio"]["sourceRevision"],"high_bit_edit":True,
            "touch_to_sound":True,"panic_silence":True,"native_only":True})
        state=control("eco","on")
        if not state["eco"]["active"] or state["eco"]["origin"]!="native-engine":
            raise RuntimeError("Extracted Ecophon habitat unavailable")
        edited=tap(153,158)
        if (edited["eco"]["fertility"],edited["eco"]["pressure"])!=(255,255):
            raise RuntimeError("Extracted Ecophon XY edit failed")
        tap(45,193)  # reset to Mutualism for actual audio witness
        control("audio-clock","on")
        state=tap(45,220)
        if not state["eco"]["running"]:
            raise RuntimeError("Extracted Ecophon start failed")
        block=post("/api/audio",{"frames":4096})
        magic,version,rate,generation,serial,frames=struct.unpack("<4sIIIII",block[:24])
        pcm=struct.unpack("<4096h",block[24:])
        peak=max(abs(v) for v in pcm)
        if (magic,version,rate,frames)!=(b"CYDA",1,24000,4096) or peak<=100:
            raise RuntimeError("Extracted Ecophon PCM failed")
        frame=get("/api/frame.png")
        require_frame(frame,"ecophon-native")
        control("panic")
        if post("/api/audio",{"frames":1024})[24:]!=bytes(2048):
            raise RuntimeError("Extracted Ecophon panic failed")
        control("audio-clock","off")
        control("eco","off")
        receipt["instruments"].append({"id":"ecophon-native","rate":rate,"peak":peak,
            "pcm_sha256":hashlib.sha256(block[24:]).hexdigest(),
            "frame_sha256":hashlib.sha256(frame).hexdigest(),
            "source_revision":state["audio"]["sourceRevision"],"full_width_xy":True,
            "touch_to_sound":True,"panic_silence":True,"native_only":True})
        receipt["result"]="PASS"
        return receipt
    finally:
        if os.name == "nt" and process.poll() is None:
            # A onefile bootloader owns a child interpreter, which owns the
            # native engine. Terminate this launched tree, not just its parent,
            # so an orphan cannot lock the extracted EXE during cleanup.
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                           timeout=10, check=False)
        else:
            process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill();process.wait(timeout=5)
        if process.stdout:
            process.stdout.close()
        reader.join(timeout=1)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package",type=Path)
    parser.add_argument("--output",type=Path)
    parser.add_argument("--standalone",action="store_true")
    args=parser.parse_args()
    source=args.package.resolve()
    with tempfile.TemporaryDirectory(prefix="CYD extracted ") as temporary:
        root=Path(temporary)
        archive=root/"evaluator.zip"
        with zipfile.ZipFile(archive,"w",zipfile.ZIP_DEFLATED) as output:
            for path in sorted(source.rglob("*")):
                if path.is_file():
                    output.write(path,path.relative_to(source).as_posix())
        directory=root/"Owner evaluator with spaces \u00fc"
        with zipfile.ZipFile(archive) as input_file:
            input_file.extractall(directory)
        if os.name!="nt":
            (directory/"fm1-simulator").chmod(0o755)
        manifest=verify_hashes(directory)
        receipt=evaluate(directory,args.standalone)
        require_audio_rates(manifest, receipt["instruments"])
        receipt["source_commit"]=manifest["source_commit"]
        receipt["archive_sha256"]=hashlib.sha256(archive.read_bytes()).hexdigest()
    encoded=json.dumps(receipt,indent=2)+"\n"
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(encoded,encoding="utf-8")
    print(encoded,end="")


if __name__=="__main__":
    main()
