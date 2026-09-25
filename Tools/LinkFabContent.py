"""Link locally installed Fab packs into every git worktree of this repo.

Fab content is licensed for use, not redistribution, and this repo is public, so
it must never be committed. It lives in ONE place, the main checkout
(`<main>/Content/<PackFolder>`, which is where "Add to Project" puts a pack) and
every other worktree (the cts-* agents) sees it through an NTFS directory
junction, `<worktree>/Content/<PackFolder>` -> `<main>/Content/<PackFolder>`.

What it does:
  * finds the main checkout from `git worktree list` (the first entry);
  * pack folders = the `folder` entries of Art/Fab/PurchasedPacks.json that
    exist, plus (with --discover, the default) any top-level Content folder of
    the main checkout that git does not track and that is not editor-local;
  * writes each pack folder to the shared `.git/info/exclude` (it applies to
    every worktree at once) so nothing can be committed even before the
    committed .gitignore block lists it;
  * creates the missing junctions in each worktree (or only --worktree PATH).

Safe to rerun. It never deletes or overwrites a real directory, and it never
copies licensed files.

Usage (any Python 3, e.g. the UE one):
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/LinkFabContent.py
  ... Tools/LinkFabContent.py --worktree C:/Users/Eric/Documents/GitHub/cts-new-thing
  ... Tools/LinkFabContent.py --check        # report only, exit 1 if something is unlinked/unignored
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / "Art" / "Fab" / "PurchasedPacks.json"

# Top-level Content folders that are editor-local or already handled elsewhere.
NOT_PACKS = {"Collections", "Developers", "__ExternalActors__", "__ExternalObjects__"}
# Content/Fab is the importer's output (already gitignored). With --include-fab
# it is linked too into worktrees that have none (opt-in: several agent
# worktrees carry their own copy).
OPTIONAL_LINK = ["Fab"]


def git(*args: str, cwd: Path = REPO) -> str:
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True,
                          text=True, encoding="utf-8").stdout


def worktrees() -> list[Path]:
    paths = []
    for line in git("worktree", "list", "--porcelain").splitlines():
        if line.startswith("worktree "):
            paths.append(Path(line[len("worktree "):].strip()))
    return paths


def is_junction(p: Path) -> bool:
    try:
        return p.is_junction() if hasattr(p, "is_junction") else bool(os.readlink(p))
    except OSError:
        return False


def junction_target(p: Path) -> Path | None:
    try:
        t = os.readlink(p)
    except OSError:
        return None
    if t.startswith("\\\\?\\"):
        t = t[4:]
    return Path(t)


def manifest_folders() -> list[str]:
    if not MANIFEST.exists():
        return []
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    out = []
    for pack in data.get("packs", []):
        out.extend(pack.get("folders") or [])
    return out


def discovered_folders(main: Path) -> list[str]:
    content = main / "Content"
    tracked = {line.split("/")[1] for line in git("ls-files", "Content", cwd=main).splitlines()
               if line.count("/") >= 2}
    found = []
    for child in sorted(content.iterdir()):
        if not child.is_dir() or child.name in NOT_PACKS or child.name in tracked:
            continue
        found.append(child.name)
    return found


def ensure_excluded(folders: list[str], check: bool) -> list[str]:
    common = Path(git("rev-parse", "--git-common-dir").strip())
    if not common.is_absolute():
        common = (REPO / common).resolve()
    exclude = common / "info" / "exclude"
    text = exclude.read_text(encoding="utf-8") if exclude.exists() else ""
    have = {l.strip() for l in text.splitlines()}
    # No trailing slash: git sees a junction as a link, not a directory.
    missing = [f"/Content/{f}" for f in folders if f"/Content/{f}" not in have]
    if missing and not check:
        exclude.parent.mkdir(parents=True, exist_ok=True)
        with exclude.open("a", encoding="utf-8") as fh:
            if "# Fab packs (LinkFabContent.py)" not in text:
                fh.write("\n# Fab packs (LinkFabContent.py): licensed, never commit\n")
            for m in missing:
                fh.write(m + "\n")
    return missing


def link(worktree: Path, main: Path, folder: str, check: bool) -> str:
    src = main / "Content" / folder
    dst = worktree / "Content" / folder
    if not src.is_dir():
        return f"skip   {folder}: not in main checkout"
    if is_junction(dst):
        tgt = junction_target(dst)
        if tgt and Path(os.path.normcase(tgt)) == Path(os.path.normcase(src)):
            return f"ok     {dst}"
        return f"WARN   {dst} is a junction to {tgt}, expected {src}"
    if dst.exists():
        return f"keep   {dst} (real folder, left untouched)"
    if check:
        return f"MISSING {dst}"
    r = subprocess.run(["cmd", "/c", "mklink", "/J", str(dst), str(src)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        return f"FAIL   {dst}: {r.stdout.strip()} {r.stderr.strip()}"
    return f"linked {dst} -> {src}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--worktree", type=Path, help="link only this worktree")
    ap.add_argument("--check", action="store_true", help="report only; exit 1 when anything is missing")
    ap.add_argument("--include-fab", action="store_true", help="also junction Content/Fab where a worktree has none")
    ap.add_argument("--no-discover", action="store_true", help="only use Art/Fab/PurchasedPacks.json")
    args = ap.parse_args()

    trees = worktrees()
    main_tree = trees[0]
    folders = list(dict.fromkeys(
        (OPTIONAL_LINK if args.include_fab else []) + manifest_folders()
        + ([] if args.no_discover else discovered_folders(main_tree))))
    folders = [f for f in folders if f not in OPTIONAL_LINK or args.include_fab]
    folders = [f for f in folders if (main_tree / "Content" / f).is_dir()]
    print(f"main checkout: {main_tree}")
    print(f"pack folders : {', '.join(folders) or '(none yet)'}")

    problems = 0
    missing = ensure_excluded([f for f in folders if f not in OPTIONAL_LINK], args.check)
    if missing:
        print(("NOT EXCLUDED " if args.check else "excluded ") + ", ".join(missing))
        problems += len(missing) if args.check else 0

    targets = [args.worktree.resolve()] if args.worktree else trees[1:]
    for tree in targets:
        if Path(os.path.normcase(tree)) == Path(os.path.normcase(main_tree)):
            continue
        if not (tree / "Content").is_dir():
            continue
        for f in folders:
            msg = link(tree, main_tree, f, args.check)
            if msg.startswith(("MISSING", "FAIL", "WARN")):
                problems += 1
            if not msg.startswith(("ok", "keep")):
                print(msg)
    print("done" if not problems else f"{problems} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
