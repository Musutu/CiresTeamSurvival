"""Synthesize the draft / skill-offer UI sounds (original, standard library only).

Writes 48 kHz mono 16-bit WAVs to Content/UI/Draft/Sounds/src and, with --import,
imports them as /Game/UI/Draft/Sounds/<name> through UnrealEditor-Cmd.
  S_SkillLearned  rising metallic shimmer + low thump + bright bell (card picked)
  S_SkillOffer    soft airy swell with a two-note chime (offer panel opens)
  S_SkillHover    tiny wooden tick (card hover)
"""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import random
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parent.parent
RATE = 48000


def env(t, attack, decay):
    return min(1.0, t / attack) * math.exp(-t / decay) if t >= 0 else 0.0


def bell(t, f, decay, amp):
    partials = ((1, 1.0), (2.01, .45), (2.76, .30), (5.40, .12), (8.93, .05))
    return sum(amp * a * env(t, .004, decay / (1 + i * .6)) * math.sin(2 * math.pi * f * p * t) for i, (p, a) in enumerate(partials))


def render(length, fn):
    rnd = random.Random(3)
    samples = [fn(i / RATE, rnd) for i in range(int(length * RATE))]
    peak = max(1e-6, max(abs(s) for s in samples))
    return [s / peak * .89 for s in samples]


def learned(t, rnd):
    s = 0.0
    # Low impact thump.
    s += .9 * env(t, .003, .09) * math.sin(2 * math.pi * (70 + 60 * math.exp(-t * 30)) * t)
    # Rising shimmer arpeggio.
    for k, (dt, f) in enumerate(((0.0, 523.25), (.06, 659.25), (.12, 783.99), (.18, 1046.5))):
        s += bell(t - dt, f, .55, .35 - k * .03)
    # Final bright bell with a gentle detuned pair.
    s += bell(t - .26, 1318.5, 1.1, .32) + bell(t - .26, 1322.0, 1.1, .18)
    # Airy sparkle noise that fades out.
    s += .05 * env(t - .05, .05, .25) * rnd.uniform(-1, 1) * (0.5 + 0.5 * math.sin(2 * math.pi * 9 * t))
    return s


def offer(t, rnd):
    swell = .25 * min(1.0, t / .35) * math.exp(-max(0.0, t - .35) / .25) * rnd.uniform(-1, 1)
    return swell * .5 + bell(t - .18, 587.33, .8, .35) + bell(t - .32, 880.0, 1.0, .35)


def hover(t, rnd):
    return env(t, .001, .018) * (math.sin(2 * math.pi * 1900 * t) * .6 + rnd.uniform(-1, 1) * .4)


SOUNDS = {'S_SkillLearned': (1.6, learned), 'S_SkillOffer': (1.3, offer), 'S_SkillHover': (.08, hover)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--import', dest='do_import', action='store_true')
    parser.add_argument('--editor', type=Path, default=Path('F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe'))
    args = parser.parse_args()
    out = ROOT / 'Content/UI/Draft/Sounds/src'
    out.mkdir(parents=True, exist_ok=True)
    for name, (length, fn) in SOUNDS.items():
        data = render(length, fn)
        with wave.open(str(out / f'{name}.wav'), 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(RATE)
            w.writeframes(b''.join(struct.pack('<h', int(s * 32767)) for s in data))
    if args.do_import:
        script = ROOT / 'Saved/ImportDraftSounds.py'
        script.parent.mkdir(exist_ok=True)
        script.write_text(
            'import unreal\n'
            f'src = r"{out}"\n'
            'tasks = []\n'
            f'for n in {list(SOUNDS)!r}:\n'
            '    t = unreal.AssetImportTask(); t.filename = src + "/" + n + ".wav"; t.destination_path = "/Game/UI/Draft/Sounds"\n'
            '    t.destination_name = n; t.replace_existing = True; t.automated = True; t.save = True; tasks.append(t)\n'
            'unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)\n'
            f'ok = all(unreal.EditorAssetLibrary.does_asset_exist("/Game/UI/Draft/Sounds/" + n) for n in {list(SOUNDS)!r})\n'
            'unreal.log("CIRE_DRAFT_SOUNDS_IMPORT_" + ("PASS" if ok else "FAIL"))\n', encoding='utf-8')
        log = ROOT / 'Saved/ImportDraftSounds.log'
        subprocess.run([str(args.editor), str(ROOT / 'CiresTeamSurvival.uproject'), '-run=pythonscript', f'-script={script}',
                        '-unattended', '-nosplash', '-nosound', '-nop4', '-NoLiveCoding', f'-abslog={log}'],
                       cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=900)
        text = log.read_text(encoding='utf-8', errors='replace')
        print('import', 'PASS' if 'CIRE_DRAFT_SOUNDS_IMPORT_PASS' in text else 'FAIL', log)
    print('wrote', ', '.join(SOUNDS), 'to', out)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
