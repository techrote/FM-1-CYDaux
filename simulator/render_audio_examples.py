#!/usr/bin/env python3
"""Record actual shared-controller/native-core performances and framebuffer video."""
from __future__ import annotations
import argparse
import hashlib
import html
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import time
import wave

from run import Engine, ROOT, find_binary


def measure(data: bytes) -> dict:
    samples=struct.unpack("<"+str(len(data)//2)+"h",data)
    peak=max((abs(x) for x in samples),default=0)
    return {"frames":len(samples),"peak":peak,
            "rms":math.sqrt(sum(x*x for x in samples)/max(1,len(samples)))/32768,
            "clipped_frames":sum(abs(x)>=32767 for x in samples),
            "pcm_sha256":hashlib.sha256(data).hexdigest()}


def write_wav(path: Path, data: bytes, rate: int):
    with wave.open(str(path),"wb") as output:
        output.setnchannels(1);output.setsampwidth(2);output.setframerate(rate);output.writeframes(data)


def connect(engine: Engine, instrument: str):
    engine.command("profile "+instrument)
    engine.command("transport native")
    state=engine.command("connect")
    if not state["companionReady"]:raise RuntimeError("Native endpoint did not negotiate")
    engine.command("audio-clock on")
    return state


def render(engine: Engine, frames: int) -> bytes:
    chunks=[]
    while frames:
        count=min(frames,1024)
        chunks.append(engine.audio(count)[24:]);frames-=count
    return b"".join(chunks)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary")
    parser.add_argument("--output",type=Path,default=ROOT/"build"/"companion-media")
    parser.add_argument("--video",action="store_true",help="Encode actual-renderer/actual-PCM demonstrations using ffmpeg")
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    engine=Engine(find_binary(args.binary))
    receipt={"schema":1,"operation":"shared C++ CYD controller + pinned native instrument cores",
             "physical_recording":False,"subjective_listening_claim":False,"recordings":[],"demonstrations":[]}
    started=time.perf_counter()
    try:
        for instrument,count in (("xenoresonator-native",4),("zaaggenz-native",6),("grimoire-native",3)):
            state=connect(engine,instrument);rate=state["audio"]["sampleRate"]
            for preset in range(count):
                state=engine.command(f"preset {preset}")
                phrase=[]
                for pitch in ((48,55,59,51) if instrument=="grimoire-native" else (48,55,60,51)):
                    engine.command(f"note {pitch} 100")
                    phrase.append(render(engine,rate*2//5))
                    engine.command(f"note {pitch} 0")
                    phrase.append(render(engine,rate//5))
                phrase.append(render(engine,rate))
                data=b"".join(phrase)
                name=f"{instrument}-preset-{preset+1:02d}.wav"
                write_wav(args.output/name,data,rate)
                info=measure(data)
                if info["peak"]<=100 or info["clipped_frames"]:raise RuntimeError("Invalid authored audition dynamics")
                receipt["recordings"].append({"file":name,"instrument":instrument,"preset":preset,
                    "name":state["presetName"],"sample_rate":rate,"source_revision":state["audio"]["sourceRevision"],**info})
            # An eight-second local sequence, rendered from actual shared UI.
            state=engine.command("preset 0");engine.command("page sequence");engine.command("sequence")
            frames_dir=args.output/(instrument+"-frames");frames_dir.mkdir(exist_ok=True)
            pcm=[]
            for frame in range(96):
                if frame==32:engine.command("page xy")
                if frame>=32 and frame<64:
                    # The actual pad dispatch changes two real parameters. Gate
                    # remains off here so the sequencer owns articulation.
                    if frame==32:engine.command("tap 270 80")
                    x=20+(frame-32)*6;y=175-(frame-32)*3
                    engine.command(f"down {x} {y}")
                    engine.command(f"up {x} {y}")
                if frame==64:engine.command("page mix")
                pcm.append(render(engine,rate//12))
                (frames_dir/f"{frame:04d}.png").write_bytes(engine.frame())
            engine.command("panic");engine.command("audio-clock off")
            data=b"".join(pcm);name=instrument+"-performance.wav"
            write_wav(args.output/name,data,rate)
            info={"file":name,"instrument":instrument,"sample_rate":rate,"seconds":8,
                  "frame_rate":12,"frame_count":96,"frames_source":"actual shared C++ framebuffer",**measure(data)}
            if args.video:
                ffmpeg=shutil.which("ffmpeg")
                if not ffmpeg:raise RuntimeError("ffmpeg is required for --video")
                video=args.output/(instrument+"-performance.mp4")
                subprocess.run([ffmpeg,"-y","-loglevel","error","-framerate","12","-i",str(frames_dir/"%04d.png"),
                                "-i",str(args.output/name),"-vf","scale=960:720:flags=neighbor","-c:v","libx264","-preset","fast",
                                "-pix_fmt","yuv420p","-c:a","aac","-b:a","160k","-shortest","-movflags","+faststart",str(video)],check=True)
                info["video"]=video.name;info["video_sha256"]=hashlib.sha256(video.read_bytes()).hexdigest()
            shutil.rmtree(frames_dir)
            receipt["demonstrations"].append(info)
        receipt["host_render_wall_seconds"]=time.perf_counter()-started
        receipt["source_commit"]=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
        receipt["source_dirty"]=bool(subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True).strip())
        receipt["result"]="PASS"
        (args.output/"audio-evidence.json").write_text(json.dumps(receipt,indent=2)+"\n")
        entries=[]
        for item in receipt["recordings"]:
            entries.append('<article><h2>'+html.escape(item["name"] or item["file"])+ '</h2><p>'+html.escape(item["instrument"])+
                           ' · '+str(item["sample_rate"])+' Hz · peak '+str(item["peak"])+ '</p><audio controls preload="none" src="'+item["file"]+'"></audio></article>')
        demos=[]
        for item in receipt["demonstrations"]:
            if "video" in item:demos.append('<article><h2>'+html.escape(item["instrument"])+ '</h2><video controls preload="metadata" src="'+item["video"]+'"></video></article>')
        (args.output/"Listen.html").write_text('''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>CYDaux actual-core auditions</title><style>body{background:#0b121c;color:#dce7f1;font:16px/1.5 system-ui;max-width:1080px;margin:auto;padding:30px}h1{color:#48dfbd}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:18px}article{background:#142230;border:1px solid #304358;border-radius:12px;padding:16px}h2{font-size:17px}p{color:#a1b5c9;font-size:13px}audio,video{width:100%}a{color:#48dfbd}</style><h1>CYDaux actual-core auditions</h1><p>These recordings were generated by the same native instrument cores driven by the common C++ companion controller. ZaaggenZ uses procedural native mode; source-image assets are excluded. Xenoresonator is the actual six-mode VIBEWARE engine. Grimoire uses the original Orbit Gate three-material core. This is software output, not a physical FM-1 recording.</p><main>'''+''.join(demos+entries)+'''</main><p><a href="audio-evidence.json">Exact sources, PCM hashes and amplitude measurements</a></p></html>''',encoding="utf-8")
        print(json.dumps({"result":"PASS","output":str(args.output),"recordings":len(receipt["recordings"]),"demonstrations":len(receipt["demonstrations"]),"wall_seconds":receipt["host_render_wall_seconds"]}))
    finally:engine.close()


if __name__=="__main__":main()
