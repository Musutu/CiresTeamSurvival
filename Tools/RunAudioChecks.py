"""Offline audio render check for Cire's Team Survival.

Launches the game unattended with -CireAudioProbe. The probe (CireAudioTests.cpp) mutes every submix
output (nothing reaches the speakers), records the Music, Ambience and SFX bus submixes to WAV while it
drives: the four music states + the victory stinger, six district ambiences, every footstep armour class
and the main event cues. This script then measures RMS / peak for every timeline segment and fails if
any segment is silent.

  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/RunAudioChecks.py
Output: Saved/AudioChecks/<stamp>/{SMX_*.wav, timeline.json, levels.json, levels.md, probe.log}
"""
import argparse
import json
import math
import os
import struct
import subprocess
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SILENCE_DB = -60.0


def read_wav(path):
    """Minimal RIFF reader: PCM 16/24/32-bit and IEEE float 32-bit. Returns (rate, channels, mono floats)."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("not a WAV file: %s" % path)
    pos, fmt, frames = 12, None, None
    while pos + 8 <= len(data):
        chunk, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if chunk == b"fmt ":
            tag, channels, rate, _, _, bits = struct.unpack("<HHIIHH", body[:16])
            if tag == 0xFFFE and len(body) >= 26:
                tag = struct.unpack("<H", body[24:26])[0]
            fmt = (tag, channels, rate, bits)
        elif chunk == b"data":
            frames = body
        pos += 8 + size + (size & 1)
    tag, channels, rate, bits = fmt
    if tag == 3 and bits == 32:
        samples = struct.unpack("<%df" % (len(frames) // 4), frames[:len(frames) // 4 * 4])
    elif tag == 1 and bits == 16:
        samples = [s / 32768.0 for s in struct.unpack("<%dh" % (len(frames) // 2), frames[:len(frames) // 2 * 2])]
    elif tag == 1 and bits == 32:
        samples = [s / 2147483648.0 for s in struct.unpack("<%di" % (len(frames) // 4), frames[:len(frames) // 4 * 4])]
    elif tag == 1 and bits == 24:
        samples = [int.from_bytes(frames[i:i + 3], "little", signed=True) / 8388608.0 for i in range(0, len(frames) - 2, 3)]
    else:
        raise ValueError("unsupported WAV format tag=%d bits=%d" % (tag, bits))
    mono = [sum(samples[i:i + channels]) / channels for i in range(0, len(samples) - channels + 1, channels)]
    return rate, channels, mono


def level(mono, rate, start, end):
    a, b = max(0, int(start * rate)), min(len(mono), int(end * rate))
    if b <= a:
        return None, None
    seg = mono[a:b]
    rms = math.sqrt(sum(x * x for x in seg) / len(seg))
    peak = max(abs(x) for x in seg)
    to_db = lambda v: 20 * math.log10(max(v, 1e-9))
    return round(to_db(rms), 1), round(to_db(peak), 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--timeout", type=int, default=240)
    parser.add_argument("--analyze", type=Path, help="only analyse an existing output folder")
    args = parser.parse_args()
    if args.analyze:
        out = args.analyze
    else:
        stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        out = args.project.resolve().parent / "Saved" / "AudioChecks" / stamp
        out.mkdir(parents=True, exist_ok=False)
        log = out / "probe.log"
        command = [str(args.editor), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireAudioProbe",
                   "-CireAudioProbeOut=%s" % out, "-nullrhi", "-unattended", "-nop4", "-nosplash", "-NoLiveCoding",
                   "-ExecCmds=t.MaxFPS 60, au.NeverDisableSubmixes 1", "-abslog=%s" % log]
        print("Launching audio probe; output", out, flush=True)
        child = subprocess.Popen(command, cwd=args.project.resolve().parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                 creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0)
        try:
            child.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()
            print("FAIL: probe timed out after %ds" % args.timeout)
        text = log.read_text("utf-8", "replace") if log.exists() else ""
        if "CIRE_AUDIO_PROBE_DONE" not in text:
            print("FAIL: probe did not finish; see", log)
            return 1
        time.sleep(1.0)
    timeline = json.loads((out / "timeline.json").read_text("utf-8"))
    waves = {}
    for bus in ("Music", "Ambience", "SFX"):
        path = out / ("SMX_%s.wav" % bus)
        if path.exists():
            waves[bus] = read_wav(path)
    rows, failures = [], []
    for seg in timeline["segments"]:
        bus = seg["bus"]
        if bus not in waves:
            failures.append("%s: no recording for bus %s" % (seg["name"], bus))
            continue
        rate, _, mono = waves[bus]
        rms, peak = level(mono, rate, seg["start"], seg["end"])
        ok = rms is not None and rms > SILENCE_DB
        rows.append(dict(bus=bus, name=seg["name"], start=seg["start"], end=seg["end"], rmsDb=rms, peakDb=peak, audible=ok))
        if not ok:
            failures.append("%s/%s silent (rms %s dBFS)" % (bus, seg["name"], rms))
    summary = {"passed": not failures and bool(rows), "silenceThresholdDb": SILENCE_DB, "segments": rows, "failures": failures,
               "recordings": {b: {"seconds": round(len(w[2]) / w[0], 2), "rate": w[0], "channels": w[1]} for b, w in waves.items()},
               "footsteps": timeline.get("footsteps"), "ambienceOneShots": timeline.get("ambienceOneShots")}
    (out / "levels.json").write_text(json.dumps(summary, indent=2) + "\n", "utf-8")
    lines = ["| bus | segment | window (s) | RMS dBFS | peak dBFS |", "|---|---|---|---|---|"]
    lines += ["| %s | %s | %.1f-%.1f | %s | %s |" % (r["bus"], r["name"], r["start"], r["end"], r["rmsDb"], r["peakDb"]) for r in rows]
    (out / "levels.md").write_text("\n".join(lines) + "\n", "utf-8")
    print("\n".join(lines))
    print("PASS" if summary["passed"] else "FAIL: " + "; ".join(failures))
    print("Report:", out / "levels.json")
    return 0 if summary["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
