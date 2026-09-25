"""Run Tools/IntegrateTripoRaces.py with temporary skeleton core redirects (the Bridge animation-redirector workaround).

The Tripo Bridge imports animations against a transient 'tripo_convert_<id>_Skeleton' and renames that skeleton without
saving a redirector, so saved clips point at a package that never existed. This wrapper scans each pending
/Game/TripoModels/CTS_Race_* import, appends [CoreRedirects] mapping the transient skeleton to the saved
<Export>_Skeleton, runs the editor integration, restores DefaultEngine.ini and removes the emptied Bridge folders.
The Bridge watcher editor (Tools/OpenTripoBridgeBatch03.py) must be closed first.
Usage: python Tools/RunTripoRacesIntegration.py
"""
from pathlib import Path
import json
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
    backup = INI.with_suffix(".ini.tripo-races.bak")
    shutil.copy2(INI, backup)
    try:
        extra = redirects()
        if extra:
            with INI.open("a", encoding="utf-8", newline="\r\n") as stream:
                stream.write("\n[CoreRedirects]\n" + "\n".join(extra) + "\n")
        subprocess.run([ENGINE + "UnrealEditor.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-unattended", "-nosplash", "-Multiprocess",
                        "-NoLiveCoding", "-RenderOffscreen", "-nosound",
                        "-ExecCmds=py " + (ROOT / "Tools" / "IntegrateTripoRaces.py").as_posix(),
                        "-abslog=" + str(ROOT / "Saved" / "Logs" / "TripoRaces-Integrate.log")], check=True, timeout=2400)
    finally:
        shutil.copy2(backup, INI)
        backup.unlink()
    report = json.loads((ROOT / "Saved" / "TripoRacesIntegration.json").read_text(encoding="utf-8"))
    print("units", len(report["units"]), "errors", len(report["errors"]))
    for error in report["errors"]:
        print(error)
    # With core redirects active the rename leaves the original Bridge packages behind; drop the moved ones.
    moved = {m["from"].rsplit("/", 1)[1] for m in report.get("moved", [])}
    for folder in (ROOT / "Content" / "TripoModels").glob("CTS_*"):
        if folder.name in moved:
            shutil.rmtree(folder)


if __name__ == "__main__":
    main()
