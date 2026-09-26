"""Install the One-File-Per-Actor data of Fab packs that "Add to Project" left behind.

Some packs keep level actors as external packages in `Content/__ExternalActors__/<Pack>` and
`Content/__ExternalObjects__/<Pack>`. The Medieval Kingdom pack (Content/CastleTown) is one: its World Partition landscape
(SL_Landscape) and several Level Instances (curtain walls, front gate, full castle, some buildings) are external
actors. The Fab/launcher "Add to Project" copied Content/CastleTown but not those folders, so the town had no terrain,
and loading SL_Landscape asserts in -game (Docs/CastleTown.md "Requirements").

What it does, for every pack folder in the main checkout's Content (or --pack NAME):
  * finds the pack in the Epic launcher VaultCache (`<vault>/*/data/Content/<Pack>`, where the launcher keeps what it
    downloaded) and, when that entry carries `__ExternalActors__/<Pack>` or `__ExternalObjects__/<Pack>`, COPIES them
    into `<main>/Content/__ExternalActors__/<Pack>` (resp. __ExternalObjects__). Existing files are never overwritten;
  * junctions `<worktree>/Content/__External*__/<Pack>` -> the main checkout's copy in every other worktree (a real
    folder already there is left untouched).
Both `Content/__ExternalActors__` and `Content/__ExternalObjects__` are gitignored: licensed content, never committed.

Idempotent and safe to rerun. Called by Tools/LinkFabContent.py; standalone:
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/InstallFabExternals.py [--check] [--pack CastleTown]
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VAULT = Path(os.environ.get("CIRE_EPIC_VAULT", "C:/ProgramData/Epic/EpicGamesLauncher/VaultCache"))
KINDS = ("__ExternalActors__", "__ExternalObjects__")


def git(*args: str, cwd: Path = REPO) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True, encoding="utf-8").stdout


def worktrees() -> list[Path]:
    return [Path(l[len("worktree "):].strip()) for l in git("worktree", "list", "--porcelain").splitlines() if l.startswith("worktree ")]


def vault_sources(pack: str, vault: Path) -> dict[str, Path]:
    """kind -> vault folder with that pack's external data (first vault entry that has the pack and the data)."""
    out: dict[str, Path] = {}
    if not vault.is_dir():
        return out
    for entry in sorted(vault.iterdir()):
        content = entry / "data" / "Content"
        if not (content / pack).is_dir():
            continue
        for kind in KINDS:
            src = content / kind / pack
            if kind not in out and src.is_dir() and any(src.iterdir()):
                out[kind] = src
    return out


def copy_missing(src: Path, dst: Path, check: bool) -> int:
    """Copy files that do not exist yet. Returns how many were (or, with check, would be) copied."""
    count = 0
    for root, _dirs, files in os.walk(src):
        rel = Path(root).relative_to(src)
        for name in files:
            target = dst / rel / name
            if target.exists():
                continue
            count += 1
            if not check:
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(Path(root) / name, target)
    return count


def is_junction(p: Path) -> bool:
    try:
        return p.is_junction() if hasattr(p, "is_junction") else bool(os.readlink(p))
    except OSError:
        return False


def run(main: Path, targets: list[Path], check: bool, packs: list[str] | None = None, vault: Path = VAULT) -> int:
    """Install into main, junction into targets. Returns the number of problems (only counted with check)."""
    problems = 0
    if packs is None:
        packs = [p.name for p in sorted((main / "Content").iterdir())
                 if p.is_dir() and not p.name.startswith("__") and p.name not in ("Collections", "Developers")]
    for pack in packs:
        sources = vault_sources(pack, vault)
        for kind, src in sources.items():
            dst = main / "Content" / kind / pack
            if is_junction(dst):
                print(f"keep   {dst} (junction)")
                continue
            n = copy_missing(src, dst, check)
            if n:
                print(("MISSING " if check else "copied ") + f"{n} file(s) {src} -> {dst}")
                problems += n if check else 0
        for kind in KINDS:
            src = main / "Content" / kind / pack
            if not src.is_dir():
                continue
            for tree in targets:
                if os.path.normcase(str(tree)) == os.path.normcase(str(main)) or not (tree / "Content").is_dir():
                    continue
                dst = tree / "Content" / kind / pack
                if dst.exists() or is_junction(dst):
                    continue
                if check:
                    print(f"MISSING {dst}")
                    problems += 1
                    continue
                dst.parent.mkdir(parents=True, exist_ok=True)
                r = subprocess.run(["cmd", "/c", "mklink", "/J", str(dst), str(src)], capture_output=True, text=True)
                print(f"linked {dst} -> {src}" if r.returncode == 0 else f"FAIL   {dst}: {r.stdout.strip()} {r.stderr.strip()}")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true", help="report only; exit 1 when something is missing")
    ap.add_argument("--pack", action="append", help="only this pack folder (repeatable)")
    ap.add_argument("--vault", type=Path, default=VAULT, help="Epic launcher VaultCache folder")
    args = ap.parse_args()
    trees = worktrees()
    problems = run(trees[0], trees[1:], args.check, args.pack, args.vault)
    print("done" if not problems else f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
