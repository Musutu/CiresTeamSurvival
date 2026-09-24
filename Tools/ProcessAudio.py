"""Turn the decoded CC0 sources into the WAVs imported by Tools/BuildAudioContent.py.

Pure standard-library DSP (wave + audioop), so it runs on the engine's bundled Python:
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/ProcessAudio.py

Inputs are the WAVs written by Tools/DecodeAudioSources.py. Every output is resampled to
48 kHz / 16-bit. Positional sounds (footsteps, one-shots, emitters) are mono so Unreal can
spatialise them; ambience beds are stereo seamless loops (equal-power tail->head crossfade).
Footsteps are cut from continuous walking recordings by onset detection. A JSON report of
every output (source keys, duration, RMS/peak dBFS) is written next to the files.
"""
import audioop
import json
import math
import random
import sys
import wave
import warnings
from pathlib import Path

warnings.filterwarnings("ignore", category=DeprecationWarning)
ROOT = Path(__file__).resolve().parents[1]
DECODED = ROOT / "Art" / "Downloads" / "Audio" / "decoded"
OUT = ROOT / "Art" / "Downloads" / "Audio" / "processed"
REPORT_PATH = ROOT / "Art" / "Audio" / "ProcessReport.json"
RATE = 48000
W = 2  # 16-bit


class Clip:
    def __init__(self, data, channels, sources):
        self.data, self.ch, self.sources = data, channels, list(sources)

    @property
    def frames(self):
        return len(self.data) // (W * self.ch)

    @property
    def seconds(self):
        return self.frames / RATE


def load(key, provider="freesound"):
    path = DECODED / provider / (key + ".wav")
    with wave.open(str(path)) as w:
        ch, sr, sw, n = w.getnchannels(), w.getframerate(), w.getsampwidth(), w.getnframes()
        data = w.readframes(n)
    if sw != W:
        data = audioop.lin2lin(data, sw, W)
    if sr != RATE:
        data, _ = audioop.ratecv(data, W, ch, sr, RATE, None)
    return Clip(data, ch, [key if provider == "freesound" else "kenney:" + key])


def kenney(name):
    return load(name, "kenney")


def mono(c):
    if c.ch == 1:
        return c
    return Clip(audioop.tomono(c.data, W, .5, .5), 1, c.sources)


def stereo(c):
    if c.ch == 2:
        return c
    return Clip(audioop.tostereo(c.data, W, 1, 1), 2, c.sources)


def cut(c, start, end=None):
    a = int(start * RATE) * W * c.ch
    b = len(c.data) if end is None else min(len(c.data), int(end * RATE) * W * c.ch)
    return Clip(c.data[a:b], c.ch, c.sources)


def gain_blocks(c, fn, block=64):
    """Apply a smooth gain curve fn(t in 0..1) in small blocks (audioop has no per-sample ramps)."""
    step = block * W * c.ch
    total = max(1, len(c.data))
    out = bytearray()
    for i in range(0, len(c.data), step):
        out += audioop.mul(c.data[i:i + step], W, fn((i + step / 2) / total))
    return Clip(bytes(out), c.ch, c.sources)


def fade(c, fin=.005, fout=.05):
    dur = max(c.seconds, 1e-6)
    fi, fo = fin / dur, fout / dur

    def g(t):
        v = 1.0
        if fi > 0 and t < fi:
            v *= t / fi
        if fo > 0 and t > 1 - fo:
            v *= max(0.0, (1 - t) / fo)
        return v
    return gain_blocks(c, g, 32)


def db(x):
    return 20 * math.log10(max(x, 1) / 32768.0)


def normalize(c, peak_db=None, rms_db=None, limit_db=-1.0):
    peak, rms = audioop.max(c.data, W), audioop.rms(c.data, W)
    g = 1.0
    if rms_db is not None:
        g = 10 ** ((rms_db - db(rms)) / 20)
    if peak_db is not None:
        g = 10 ** ((peak_db - db(peak)) / 20) if rms_db is None else min(g, 10 ** ((peak_db - db(peak)) / 20))
    g = min(g, 10 ** ((limit_db - db(peak)) / 20))
    return Clip(audioop.mul(c.data, W, g), c.ch, c.sources)


def mix(*clips, gains=None):
    gains = gains or [1.0] * len(clips)
    ch = max(c.ch for c in clips)
    clips = [stereo(c) if ch == 2 else c for c in clips]
    n = max(len(c.data) for c in clips)
    acc = b"\x00" * n
    for c, g in zip(clips, gains):
        acc = audioop.add(acc, audioop.mul(c.data, W, g) + b"\x00" * (n - len(c.data)), W)
    return Clip(acc, ch, [s for c in clips for s in c.sources])


def concat(*clips):
    return Clip(b"".join(c.data for c in clips), clips[0].ch, [s for c in clips for s in c.sources])


def pitch(c, factor):
    """Resample so playback is `factor` x the pitch (and 1/factor x the length)."""
    data, _ = audioop.ratecv(c.data, W, c.ch, int(RATE * factor), RATE, None)
    return Clip(data, c.ch, c.sources)


def lowpass(c, cutoff_hz):
    """One-pole low-pass (per-sample; only for short or mono material)."""
    import array
    a = array.array("h", c.data)
    k = math.exp(-2 * math.pi * cutoff_hz / RATE)
    state = [0.0] * c.ch
    for i in range(len(a)):
        ch = i % c.ch
        state[ch] = (1 - k) * a[i] + k * state[ch]
        a[i] = int(max(-32768, min(32767, state[ch])))
    return Clip(a.tobytes(), c.ch, c.sources)


def loop(c, seconds=None, xfade=2.0, start=0.0):
    """Seamless loop: take seconds+xfade of material, crossfade the tail into the head (equal power)."""
    total = c.seconds - start
    seconds = min(seconds or (total - xfade), total - xfade)
    body = cut(c, start, start + seconds + xfade)
    xf = int(xfade * RATE) * W * body.ch
    head, mid, tail = body.data[:xf], body.data[xf:len(body.data) - xf], body.data[len(body.data) - xf:]
    # the old tail fades out while the new head fades in, so the seam matches the loop start
    tail_c = gain_blocks(Clip(tail, body.ch, []), lambda t: math.cos(t * math.pi / 2))
    head_c = gain_blocks(Clip(head, body.ch, []), lambda t: math.sin(t * math.pi / 2))
    seam = audioop.add(tail_c.data, head_c.data, W)
    return Clip(seam + mid, body.ch, c.sources)


def envelope(c, win=.01):
    m = mono(c)
    step = int(win * RATE) * W
    return [audioop.rms(m.data[i:i + step], W) for i in range(0, len(m.data) - step, step)]


def onsets(c, win=.01, rise=3.0, floor_ratio=.12, refractory=.2):
    env = envelope(c, win)
    if not env:
        return []
    ordered = sorted(env)
    median = ordered[len(ordered) // 2]
    thr = max(median * rise, ordered[-1] * floor_ratio)
    found, last = [], -1e9
    for i in range(1, len(env)):
        if env[i] >= thr and env[i - 1] < thr and (i * win - last) >= refractory:
            # step back to the start of the attack
            j = i
            while j > 0 and env[j - 1] > median * 1.5 and (i - j) < 5:
                j -= 1
            found.append(j * win)
            last = i * win
    return found


def slices(c, count, max_len=.4, min_len=.1, pre=.008, tail=.06, spread=True, seed=1, **kw):
    """Cut individual transients (footsteps, caws, strikes) out of a continuous recording."""
    points = onsets(c, **kw)
    cand = []
    for i, t in enumerate(points):
        end = points[i + 1] - .015 if i + 1 < len(points) else c.seconds
        end = min(end, t + max_len)
        if end - t < min_len:
            continue
        s = cut(c, max(0, t - pre), end)
        cand.append((audioop.max(s.data, W), t, s))
    if not cand:
        raise RuntimeError("no onsets in %s" % c.sources)
    # prefer strong, unclipped events spread over the recording
    cand = [x for x in cand if x[0] < 32700] or cand
    cand.sort(key=lambda x: -x[0])
    pool = cand[:max(count * 3, count)]
    if spread:
        pool.sort(key=lambda x: x[1])
        stride = max(1, len(pool) // count)
        chosen = pool[::stride][:count]
    else:
        chosen = pool[:count]
    return [fade(s, pre * .6, tail) for _, _, s in chosen]


REPORT = []


def save(name, clip, category, **meta):
    path = OUT / category / (name + ".wav")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(clip.ch)
        w.setsampwidth(W)
        w.setframerate(RATE)
        w.writeframes(clip.data)
    REPORT.append(dict(name=name, category=category, file=path.relative_to(ROOT).as_posix(),
                       seconds=round(clip.seconds, 3), channels=clip.ch,
                       rmsDb=round(db(audioop.rms(clip.data, W)), 1), peakDb=round(db(audioop.max(clip.data, W)), 1),
                       sources=sorted(set(clip.sources)), **meta))


def series(prefix, clips, category, **norm):
    for i, c in enumerate(clips, 1):
        save("%s_%02d" % (prefix, i), normalize(c, **norm), category)


# ----------------------------------------------------------------------------------------
def build_ambience():
    cat = "Ambience"
    save("AMB_WindRolling", normalize(stereo(load("wind_rolling")), rms_db=-24), cat, loop=True)
    save("AMB_WindHowl", normalize(loop(load("wind_howl"), xfade=1.5), rms_db=-27), cat, loop=True)
    save("AMB_MarketFestival", normalize(loop(load("market_festival"), 60, 3.0, start=4), rms_db=-23), cat, loop=True)
    save("AMB_MarketSquare", normalize(loop(load("market_square"), 60, 3.0, start=30), rms_db=-24), cat, loop=True)
    save("AMB_Hearth", normalize(loop(load("hearth"), 40, 2.0, start=5), rms_db=-26), cat, loop=True)
    save("AMB_NightCrickets", normalize(loop(load("night_crickets_owl"), 60, 3.0, start=10), rms_db=-30), cat, loop=True)
    save("AMB_FountainBed", normalize(loop(load("fountain_small"), xfade=1.0), rms_db=-27), cat, loop=True)
    save("AMB_Flag", normalize(loop(load("flag"), 45, 2.0, start=5), rms_db=-26), cat, loop=True)
    save("AMB_WindChimes", normalize(loop(load("wind_chimes"), 60, 3.0, start=10), rms_db=-28), cat, loop=True)
    save("AMB_Pigeons", normalize(loop(load("pigeons"), xfade=2.0), rms_db=-28), cat, loop=True)
    # Positional emitters (mono loops).
    save("EMT_Fountain", normalize(mono(loop(load("fountain"), xfade=1.5)), rms_db=-20), cat, loop=True, positional=True)
    save("EMT_FireCrackle", normalize(mono(loop(load("fire_loop"), xfade=1.0)), rms_db=-21), cat, loop=True, positional=True)
    save("EMT_Hearth", normalize(mono(loop(load("hearth"), 30, 1.5, start=45)), rms_db=-21), cat, loop=True, positional=True)
    well = load("well")
    save("EMT_Well", normalize(mono(fade(cut(well, 2, 16), .3, 1.5)), rms_db=-22), cat, positional=True)

    # One-shots (mono) placed around the listener by the district scheduler.
    crows = mono(load("crows"))
    series("AMB_Crow", slices(crows, 5, max_len=1.2, min_len=.25, tail=.25, refractory=.6), cat, rms_db=-20)
    series("AMB_Chains", slices(mono(load("chains")), 4, max_len=2.2, min_len=.6, tail=.5, refractory=1.0), cat, rms_db=-22)
    creak = mono(load("gate_creak"))
    save("AMB_GateCreak_01", normalize(fade(cut(creak, 0, 3.4), .05, .6), rms_db=-21), cat)
    save("AMB_GateCreak_02", normalize(fade(cut(creak, 3.4, 7.3), .05, .6), rms_db=-21), cat)
    calls = lowpass(mono(load("merchant_calls")), 3200)
    series("AMB_MerchantCall", slices(calls, 4, max_len=2.8, min_len=.8, tail=.6, refractory=2.0, rise=2.2), cat, rms_db=-22)
    cart = mono(load("ox_cart"))
    save("AMB_Cart_01", normalize(fade(cut(cart, 3, 15), 1.2, 2.5), rms_db=-22), cat)
    save("AMB_Cart_02", normalize(fade(cut(cart, 25, 38), 1.2, 2.5), rms_db=-22), cat)
    dogs = mono(load("dogs_distant"))
    series("AMB_Dog", slices(dogs, 3, max_len=1.6, min_len=.3, tail=.3, refractory=1.0), cat, rms_db=-22)
    save("AMB_Dog_04", normalize(fade(mono(load("dog_corgi")), .01, .3), rms_db=-22), cat)
    for i, n in enumerate(["doorOpen_1", "doorClose_2", "doorOpen_2", "doorClose_4"], 1):
        save("AMB_Door_%02d" % i, normalize(mono(kenney(n)), rms_db=-22), cat)
    save("AMB_Chop_01", normalize(mono(kenney("chop")), rms_db=-21), cat)
    bell = mono(load("bell_short"))
    # The recording tolls every ~1.4 s with overlapping decays, so each one-shot is a natural three-toll phrase.
    series("AMB_Bell", [fade(cut(bell, a - .05, a + 4.2), .01, 1.4) for a in (0.86, 6.48, 12.14)], cat, rms_db=-18)
    save("AMB_BellDistant_01", normalize(fade(mono(load("bell_funeral")), .05, 3.0), rms_db=-21), cat)
    anvil = mono(load("anvil"))
    series("AMB_Anvil", slices(anvil, 4, max_len=1.4, min_len=.3, tail=.5, refractory=.5), cat, rms_db=-20)
    save("AMB_Blacksmith_01", normalize(fade(mono(cut(load("blacksmith"), 20, 36)), 1.0, 2.0), rms_db=-21), cat)
    march = mono(load("guards_march"))
    save("AMB_GuardsMarch_01", normalize(fade(cut(march, 27, 42.4), 2.0, 2.5), rms_db=-22), cat)


def build_footsteps():
    cat = "Footsteps"
    knights = mono(load("knight_walk"))
    series("FS_Plate_Stone", slices(knights, 8, max_len=.42, min_len=.12, tail=.12, refractory=.22), cat, peak_db=-3)
    plate_dirt = [mono(load("chainmail_grass")), mono(load("chainmail_gravel"))]
    series("FS_Plate_Dirt", [fade(c, .002, .1) for c in plate_dirt] + [mix(mono(kenney("footstep_grass_00%d" % i)), mono(kenney("impactPlate_light_00%d" % i)), gains=[1, .35]) for i in range(3)], cat, peak_db=-3)
    rattles = slices(mono(load("armor_chinks")), 4, max_len=.35, min_len=.08, tail=.1, refractory=.4)
    rattles += [mono(kenney("impactPlate_light_00%d" % i)) for i in range(4)]
    series("FS_ArmorRattle", rattles, cat, peak_db=-6)
    boots = mono(load("boots_pavement"))
    series("FS_Leather_Stone", slices(boots, 8, max_len=.38, min_len=.1, tail=.1, refractory=.25), cat, peak_db=-3)
    dirt = mono(load("steps_dirt"))
    series("FS_Leather_Dirt", slices(dirt, 8, max_len=.38, min_len=.1, tail=.1, refractory=.25), cat, peak_db=-3)
    series("FS_LeatherCreak", slices(mono(load("leather_creak")), 4, max_len=.45, min_len=.12, tail=.15, refractory=.5, rise=2.0), cat, peak_db=-8)
    series("FS_Cloth", [mono(kenney("cloth%d" % i)) for i in range(1, 5)], cat, peak_db=-8)
    series("FS_Soft_Stone", [mono(kenney("footstep_concrete_00%d" % i)) for i in range(5)], cat, peak_db=-4)
    series("FS_Soft_Dirt", [mono(kenney("footstep_grass_00%d" % i)) for i in range(5)], cat, peak_db=-4)
    # Bear / beasts: real stomps plus a pitched-down punch for weight and bass.
    stomps = slices(mono(load("monster_stomp")), 3, max_len=.7, min_len=.2, tail=.25, refractory=.5)
    stomps += slices(pitch(mono(load("bass_stomp")), .82), 3, max_len=.6, min_len=.2, tail=.25, refractory=.5)
    series("FS_Beast_Stone", stomps, cat, peak_db=-2)
    series("FS_Beast_Dirt", slices(mono(load("monster_gravel")), 6, max_len=.6, min_len=.15, tail=.2, refractory=.35), cat, peak_db=-2)
    series("FS_Thump", [fade(pitch(mono(kenney("impactPunch_heavy_00%d" % i)), .62), .002, .12) for i in range(5)], cat, peak_db=-3)
    series("FS_Golem", [mix(pitch(mono(kenney("impactMining_00%d" % i)), .7), pitch(mono(kenney("impactPunch_heavy_00%d" % i)), .6), gains=[.8, .7]) for i in range(5)], cat, peak_db=-2)
    hooves = slices(mono(load("hooves_cobble")), 5, max_len=.28, min_len=.07, tail=.08, refractory=.15, rise=2.5)
    hooves += slices(mono(load("hooves_2")), 3, max_len=.28, min_len=.07, tail=.08, refractory=.15, rise=2.5)
    series("FS_Hoof_Stone", hooves, cat, peak_db=-3)
    series("FS_Hoof_Dirt", slices(mono(load("horses_dirt")), 6, max_len=.3, min_len=.08, tail=.1, refractory=.18, rise=2.5), cat, peak_db=-3)
    shimmer = mono(load("shimmer"))
    save("FS_Shimmer_01", normalize(fade(cut(shimmer, 0, 1.6), .05, .8), peak_db=-10), cat)
    save("FS_Shimmer_02", normalize(fade(pitch(cut(shimmer, .3, 2.2), 1.12), .05, .8), peak_db=-10), cat)


def build_events():
    cat = "SFX"
    save("SFX_WarHorn", normalize(fade(load("war_horn"), .01, .6), peak_db=-2), cat)
    save("SFX_WarHornDistant", normalize(fade(cut(load("war_horn_distant"), 0, 11), .05, 2.5), peak_db=-3), cat)
    save("SFX_WarDrums", normalize(fade(cut(load("war_drums"), 0, 9.5), .01, 2.0), peak_db=-2), cat)
    bell = mono(load("bell_short"))
    save("SFX_PrepBell", normalize(fade(cut(bell, .8, 8.6), .01, 1.8), peak_db=-3), cat)  # five tolls
    save("SFX_PackLeaderRoar", normalize(fade(mix(mono(load("roar")), pitch(mono(load("roar_growl")), .85), gains=[1, .45]), .005, .6), peak_db=-1.5), cat, positional=True)
    save("SFX_PackLeaderGrowl", normalize(fade(mono(load("roar_growl")), .01, .6), peak_db=-3), cat, positional=True)
    save("SFX_TeleportArrive", normalize(fade(cut(load("teleport"), 0, 4.5), .01, 1.5), peak_db=-3), cat)
    save("SFX_TeleportChannel", normalize(loop(load("teleport_hum"), 16, 2.0, start=20), rms_db=-20), cat, loop=True)
    save("SFX_AggroWarning", normalize(fade(cut(load("sword_draw"), 0, 1.6), .003, .5), peak_db=-3), cat)
    save("SFX_LevelUp", normalize(fade(cut(load("fanfare"), 0, 4.2), .005, 1.2), peak_db=-2), cat)
    save("SFX_CoinsBuy", normalize(concat(stereo(load("coins")), stereo(kenney("handleCoins"))), peak_db=-3), cat)
    save("SFX_CoinsSell", normalize(stereo(kenney("handleCoins2")), peak_db=-3), cat)
    save("SFX_LootPickup", normalize(mix(stereo(kenney("handleSmallLeather")), stereo(load("coins")), gains=[1, .5]), peak_db=-3), cat)


def build_ui():
    cat = "UI"
    series("UI_Click", [mono(kenney("click_001")), mono(kenney("click1")), mono(kenney("click3"))], cat, peak_db=-6)
    series("UI_Hover", [mono(kenney("rollover%d" % i)) for i in (1, 2, 3, 4)], cat, peak_db=-12)
    save("UI_Open", normalize(mono(kenney("select_001")), peak_db=-6), cat)
    save("UI_Close", normalize(mono(kenney("back_001")), peak_db=-6), cat)
    save("UI_Confirm", normalize(mono(kenney("confirmation_002")), peak_db=-6), cat)


# aura-vfx: buff / aura / empowered-attack sounds -> Content/Audio/Auras (mono, positional).
def build_auras():
    cat = "Auras"
    one = dict(positional=True)
    beat = fade(cut(mono(load("aura_heartbeat")), 0, .92), .003, .08)
    save("AUR_Heartbeat", normalize(concat(beat, beat, beat, beat), rms_db=-16), cat, loop=True, **one)
    save("AUR_Snarl", normalize(fade(cut(mono(load("aura_snarl")), .2, 1.55), .01, .35), peak_db=-2), cat, **one)
    splat = mono(load("aura_blood_splat"))
    for i, f in enumerate((1.0, .9, 1.12), 1):
        save("AUR_BloodSplat_%02d" % i, normalize(fade(pitch(cut(splat, 0, .9), f), .002, .15), peak_db=-2), cat, **one)
    save("AUR_Swing", normalize(fade(cut(mono(load("aura_swing")), 1.65, 2.2), .003, .12), peak_db=-3), cat, **one)
    ice = mono(load("aura_ice_break"))
    for i, (a, b) in enumerate(((4.95, 5.65), (6.45, 7.15), (5.62, 6.3)), 1):
        save("AUR_IceShatter_%02d" % i, normalize(fade(cut(ice, a, b), .003, .2), peak_db=-2), cat, **one)
    save("AUR_IceCrack", normalize(fade(cut(mono(load("aura_ice_crack")), .05, 1.05), .003, .25), peak_db=-3), cat, **one)
    save("AUR_Chime", normalize(fade(cut(mono(load("aura_chime")), 8.6, 10.6), .02, .8), peak_db=-4), cat, **one)
    save("AUR_Choir", normalize(fade(cut(mono(load("aura_choir")), .1, 2.3), .03, .7), peak_db=-3), cat, **one)
    save("AUR_Sparkle", normalize(fade(cut(mono(load("aura_sparkle")), 0, 1.6), .003, .6), peak_db=-4), cat, **one)
    clang = mono(load("aura_clang"))
    for i, a in enumerate((.55, 3.35, 6.38), 1):
        save("AUR_Clang_%02d" % i, normalize(fade(cut(clang, a, a + 1.3), .002, .5), peak_db=-2), cat, **one)
    bubbles = mono(load("aura_bubbles"))
    save("AUR_Bubble", normalize(fade(cut(bubbles, 10.9, 11.9), .01, .3), peak_db=-5), cat, **one)
    save("AUR_BubblesLoop", normalize(loop(bubbles, 6, 1.2, start=9.4), rms_db=-24), cat, loop=True, **one)
    save("AUR_FlameBurst", normalize(fade(cut(mono(load("aura_flame")), 0, 1.6), .003, .6), peak_db=-2), cat, **one)
    save("AUR_FireLoop", normalize(loop(mono(load("fire_loop")), 8, 1.5, start=5), rms_db=-22), cat, loop=True, **one)
    save("AUR_ForceField", normalize(fade(cut(mono(load("aura_forcefield")), 0, 2.4), .05, .9), peak_db=-4), cat, **one)
    save("AUR_Hourglass", normalize(fade(cut(mono(load("aura_hourglass")), .9, 3.6), .15, .9), peak_db=-5), cat, **one)
    save("AUR_Gong", normalize(fade(cut(mono(load("aura_gong")), .45, 2.35), .003, 1.0), peak_db=-2), cat, **one)
    save("AUR_WarCry", normalize(fade(cut(mono(load("aura_warcry")), 0, 1.7), .05, .6), peak_db=-3), cat, **one)
    save("AUR_Whoosh", normalize(fade(mono(load("aura_whoosh")), .003, .15), peak_db=-3), cat, **one)


def main():
    random.seed(7)
    only = set(sys.argv[1:])
    for name, fn in (("ambience", build_ambience), ("footsteps", build_footsteps), ("events", build_events), ("ui", build_ui), ("auras", build_auras)):
        if not only or name in only:
            fn()
            print("built", name)
    report = REPORT_PATH
    old = json.loads(report.read_text("utf-8")) if report.exists() and only else {"outputs": []}
    keep = {r["name"]: r for r in old.get("outputs", [])}
    keep.update({r["name"]: r for r in REPORT})
    report.write_text(json.dumps({"rate": RATE, "folder": OUT.relative_to(ROOT).as_posix(), "outputs": sorted(keep.values(), key=lambda r: r["file"])}, indent=1) + "\n", "utf-8")
    print("outputs", len(keep))


if __name__ == "__main__":
    main()
