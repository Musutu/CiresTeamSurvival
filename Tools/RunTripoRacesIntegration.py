"""Run Tools/IntegrateTripoRaces.py with temporary skeleton core redirects (the Bridge animation-redirector workaround).

The Tripo Bridge imports animations against a transient 'tripo_convert_<id>_Skeleton' and renames that skeleton without
saving a redirector, so saved clips point at a package that never existed. This wrapper scans each pending
/Game/TripoModels/CTS_Race_* import, appends [CoreRedirects] mapping the transient skeleton to the saved
<Export>_Skeleton, runs the editor integration, restores DefaultEngine.ini and removes the emptied Bridge folders.
The Bridge watcher editor (Tools/OpenTripoBridgeBatch03.py) must be closed first.
Usage: python Tools/RunTripoRacesIntegration.py
"""
import os
from pathlib import Path
import json
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
ENGINE = "F:/UE_5.8/Engine/Binaries/Win64/"
INI = ROOT / "Config" / "DefaultEngine.ini"


def run_editor(command, timeout, env=None, **kwargs) -> subprocess.CompletedProcess:
    """subprocess.run() for an editor that skips UBT SDK setup and kills the whole process tree on timeout."""
    # AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
    # and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Editors here target Win64.
    child = subprocess.Popen(command, env={**(env or os.environ), "UE_SKIP_UBT_SDK_SETUP": "1"}, **kwargs)
    try:
        return subprocess.CompletedProcess(command, child.wait(timeout=timeout))
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        child.wait()
        raise


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


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
        run_editor([ENGINE + "UnrealEditor.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-unattended", "-nosplash", "-Multiprocess",
                    "-NoLiveCoding", "-RenderOffscreen", "-nosound",
                    "-ExecCmds=py " + (ROOT / "Tools" / "IntegrateTripoRaces.py").as_posix(),
                    "-abslog=" + str(ROOT / "Saved" / "Logs" / "TripoRaces-Integrate.log")], 2400).check_returncode()
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
