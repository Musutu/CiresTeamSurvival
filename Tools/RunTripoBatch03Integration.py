"""Run Tools/IntegrateTripoBatch03.py (and the skeletal material fix) with temporary skeleton core redirects.

The Tripo Bridge (1.0.5) imports animations against a transient 'tripo_convert_<id>_Skeleton' and renames that
skeleton afterwards without saving a redirector, so saved clips point at a package that never existed on disk.
This wrapper scans each pending /Game/TripoModels/CTS_* import, adds [CoreRedirects] entries mapping the
transient skeleton to the saved <Export>_Skeleton, runs the editor integration, then restores DefaultEngine.ini.
Usage: python Tools/RunTripoBatch03Integration.py
"""
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
ENGINE = "F:/UE_5.8/Engine/Binaries/Win64/"
INI = ROOT / "Config" / "DefaultEngine.ini"


def redirects():
    lines = []
    for folder in sorted((ROOT / "Content" / "TripoModels").glob("CTS_*")):
        new = "/Game/TripoModels/%s/%s_Skeleton" % (folder.name, folder.name)
        olds = set()
        for clip in folder.glob("Animations/*.uasset"):
            olds |= {m.decode() for m in re.findall(rb"/Game/TripoModels/[A-Za-z0-9_]+/tripo_convert_[0-9a-f\-]+_Skeleton", clip.read_bytes())}
        for old in sorted(olds):
            lines.append('+PackageRedirects=(OldName="%s",NewName="%s")' % (old, new))
            lines.append('+ObjectRedirects=(OldName="%s.%s",NewName="%s.%s_Skeleton")' % (old, old.rsplit("/", 1)[1], new, folder.name))
    return lines


def main():
    backup = INI.with_suffix(".ini.tripo-batch03.bak")
    shutil.copy2(INI, backup)
    try:
        extra = redirects()
        if extra:
            with INI.open("a", encoding="utf-8", newline="\r\n") as stream:
                stream.write("\n[CoreRedirects]\n" + "\n".join(extra) + "\n")
        subprocess.run([ENGINE + "UnrealEditor.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-unattended", "-nosplash",
                        "-NoLiveCoding", "-RenderOffscreen", "-nosound",
                        "-ExecCmds=py " + (ROOT / "Tools" / "IntegrateTripoBatch03.py").as_posix(),
                        "-abslog=" + str(ROOT / "Saved" / "Logs" / "TripoBatch03-Integrate.log")], check=True, timeout=1800)
    finally:
        shutil.copy2(backup, INI)
        backup.unlink()
    subprocess.run([ENGINE + "UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-run=pythonscript",
                    "-script=" + (ROOT / "Tools" / "FixTripoBatch03SkeletalMaterials.py").as_posix(), "-unattended",
                    "-nullrhi", "-nosplash", "-nop4"], check=True, timeout=1800)
    # The rename leaves the original (now superseded) Bridge packages behind when core redirects are active.
    for folder in (ROOT / "Content" / "TripoModels").glob("CTS_*"):
        shutil.rmtree(folder)


if __name__ == "__main__":
    main()
