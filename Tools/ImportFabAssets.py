"""Import website-downloaded Fab assets (FBX/OBJ/GLB + PBR textures) into Content/Fab.

Usage:
    python Tools/ImportFabAssets.py            # import every catalog item whose download exists
    python Tools/ImportFabAssets.py --plan     # read-only: show what would be imported
    python Tools/ImportFabAssets.py --only castle_wall,old_stone_well

Source of truth is Art/Fab/FabCatalog.json. For every entry with
"delivery": "download", place the ZIP(s) Fab gives you in
    Art/Downloads/Fab/<category>/<folder>/      (gitignored)
The ZIPs are extracted in place (asset/texture file types only; nothing is ever
executed), then imported inside the isolated Tools/ContentBuilder project with
the legacy FBX factory, as ImportTripo.py does. Results are copied to
Content/Fab/<category>/<folder>/ and Content/Data/TownAssetSlots.fab.json is
refreshed so slots point at the imported meshes/materials.

Per asset: one MI_<folder> built from the shared M_FabPBR master (base colour,
normal, roughness/AO/metallic or packed ORM), Nanite for meshes above
NANITE_TRIANGLES, a simple box collision if the mesh has none, and a scale
check (meshes authored in metres are re-imported at 100x).
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import zipfile

SCRIPT = Path(__file__).resolve()
ROOT = SCRIPT.parent.parent
STAGING = SCRIPT.parent / "ContentBuilder"
CATALOG = ROOT / "Art/Fab/FabCatalog.json"
DOWNLOADS = ROOT / "Art/Downloads/Fab"
SLOTS = ROOT / "Content/Data/TownAssetSlots.fab.json"
REQUEST = STAGING / "Saved/FabImportRequest.json"
REPORT = STAGING / "Saved/FabImportReport.json"
PACKAGE_ROOT = "/Game/Fab"
NANITE_TRIANGLES = 20000
MESH_EXT = {".fbx", ".obj", ".glb", ".gltf"}
TEX_EXT = {".png", ".jpg", ".jpeg", ".tga", ".tif", ".tiff", ".exr", ".bmp"}
SAFE_EXT = MESH_EXT | TEX_EXT | {".mtl", ".bin", ".json", ".txt", ".pdf", ".html"}
SAFE_NAME = re.compile(r"[A-Za-z0-9_]{1,64}")

# Texture role by filename keyword (Megascans + common DCC conventions).
ROLES = [
    ("orm", r"(?:^|[_\-. ])(orm|arm|occlusionroughnessmetallic)(?:[_\-. ]|$)"),
    ("basecolor", r"(basecolou?r|albedo|diffuse|_col(?:or)?(?:[_\-. ]|$)|_b(?:[_\-. ]|$)|_d(?:[_\-. ]|$))"),
    ("normal", r"(normal|_nrm|_nor(?:[_\-. ]|$)|_n(?:[_\-. ]|$))"),
    ("roughness", r"(roughness|_rough|_r(?:[_\-. ]|$))"),
    ("ao", r"(ambientocclusion|occlusion|_ao(?:[_\-. ]|$))"),
    ("metallic", r"(metallic|metalness|_metal|_m(?:[_\-. ]|$))"),
    ("opacity", r"(opacity|alpha|_mask(?:[_\-. ]|$))"),
]
SKIP_TEX = re.compile(r"(displacement|height|_disp|bump|cavity|gloss|specular|fuzz|translucency|preview|thumb)", re.I)


def load_catalog() -> dict:
    document = json.loads(CATALOG.read_text(encoding="utf-8"))
    if document.get("schemaVersion") != 1:
        raise ValueError("FabCatalog schemaVersion 1 expected")
    for item in document["items"]:
        for key in ("category", "folder"):
            if not SAFE_NAME.fullmatch(item.get(key, "")):
                raise ValueError(f"Unsafe {key} in catalog item {item.get('id')}")
    return document


def extract_zip(archive_path: Path, destination: Path) -> list[str]:
    extracted = []
    with zipfile.ZipFile(archive_path) as archive:
        members = archive.infolist()
        if len(members) > 20000 or sum(m.file_size for m in members) > 8 * 1024**3:
            raise ValueError(f"{archive_path.name} is unexpectedly large; inspect it manually")
        for item in members:
            relative = PurePosixPath(item.filename.replace("\\", "/"))
            if relative.is_absolute() or ".." in relative.parts or any(":" in p for p in relative.parts):
                raise ValueError(f"Unsafe member path {item.filename} in {archive_path.name}")
            if stat.S_ISLNK(item.external_attr >> 16) or item.is_dir():
                continue
            if relative.suffix.lower() not in SAFE_EXT:
                continue  # executables, scripts and unknown payloads are never extracted
            if relative.suffix.lower() == ".zip":
                continue
            target = destination.joinpath(*relative.parts).resolve()
            if not target.is_relative_to(destination.resolve()):
                raise ValueError(f"Member escapes folder: {item.filename}")
            target.parent.mkdir(parents=True, exist_ok=True)
            with archive.open(item) as src, target.open("wb") as dst:
                shutil.copyfileobj(src, dst)
            extracted.append(str(target))
    return extracted


def texture_role(name: str) -> str | None:
    lowered = Path(name).stem.lower()
    if SKIP_TEX.search(lowered):
        return None
    for role, pattern in ROLES:
        if re.search(pattern, lowered):
            return role
    return None


def pick_mesh(files: list[Path]) -> Path | None:
    meshes = [p for p in files if p.suffix.lower() in MESH_EXT]
    if not meshes:
        return None
    lod0 = [p for p in meshes if re.search(r"lod0|_high|highpoly", p.stem, re.I)]
    no_low = [p for p in meshes if not re.search(r"lod[1-9]|_low", p.stem, re.I)]
    order = {".fbx": 0, ".obj": 1, ".glb": 2, ".gltf": 3}
    pool = lod0 or no_low or meshes
    return sorted(pool, key=lambda p: (order[p.suffix.lower()], -p.stat().st_size))[0]


def pick_textures(files: list[Path]) -> dict[str, str]:
    chosen: dict[str, Path] = {}
    for path in files:
        if path.suffix.lower() not in TEX_EXT:
            continue
        role = texture_role(path.name)
        if not role:
            continue
        # Prefer 2K (budget in Docs/AssetPipeline.md), then the largest file.
        rank = (0 if re.search(r"2k", path.stem, re.I) else 1, -path.stat().st_size)
        if role not in chosen or rank < (0 if re.search(r"2k", chosen[role].stem, re.I) else 1, -chosen[role].stat().st_size):
            chosen[role] = path
    return {role: str(path) for role, path in chosen.items()}


def stage(catalog: dict, only: set[str] | None) -> list[dict]:
    jobs = []
    for item in catalog["items"]:
        if item.get("delivery") != "download" or (only and item["id"] not in only):
            continue
        folder = DOWNLOADS / item["category"] / item["folder"]
        if not folder.is_dir():
            continue
        for archive in sorted(folder.glob("*.zip")):
            marker = folder / (archive.name + ".extracted")
            if not marker.exists():
                extract_zip(archive, folder / "extracted" / archive.stem)
                marker.write_text("ok", encoding="utf-8")
        files = [p for p in folder.rglob("*") if p.is_file()]
        mesh = pick_mesh(files)
        textures = pick_textures(files)
        if not mesh and not textures:
            continue
        jobs.append({"id": item["id"], "category": item["category"], "folder": item["folder"],
                     "kind": item.get("kind", "mesh"), "mesh": str(mesh) if mesh else None,
                     "textures": textures, "tiling": item.get("tiling", 1.0),
                     "expectedMaxCm": item.get("expectedMaxCm"),
                     "collision": item.get("collision", True)})
    return jobs


# ----------------------------------------------------------------- in editor
def import_in_editor(u) -> None:
    request = json.loads(REQUEST.read_text(encoding="utf-8"))
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    shared = PACKAGE_ROOT + "/Shared"
    lib.make_directory(shared)
    master = build_master(u, shared)
    results = []
    for job in request["jobs"]:
        dest = f"{PACKAGE_ROOT}/{job['category']}/{job['folder']}"
        lib.make_directory(dest)
        entry = {"id": job["id"], "package": dest}
        try:
            textures = {}
            for role, file in job["textures"].items():
                name = f"T_{job['folder']}_{role}"
                task = u.AssetImportTask()
                for k, v in {"filename": file, "destination_path": dest, "destination_name": name,
                             "automated": True, "replace_existing": True, "save": True}.items():
                    task.set_editor_property(k, v)
                tools.import_asset_tasks([task])
                tex = lib.load_asset(f"{dest}/{name}")
                if not tex:
                    raise RuntimeError(f"Texture import failed: {file}")
                if role == "normal":
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_NORMALMAP)
                    tex.set_editor_property("srgb", False)
                elif role in ("roughness", "ao", "metallic", "orm", "opacity"):
                    # Linear colour matches the master's LINEAR_COLOR samplers and the linear white default.
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_DEFAULT)
                    tex.set_editor_property("srgb", False)
                # Budget (Docs/AssetPipeline.md): 2K in game; the source resolution stays in the package.
                tex.set_editor_property("max_texture_size", 2048)
                lib.save_loaded_asset(tex, only_if_is_dirty=False)
                textures[role] = tex
            mi_name = f"MI_{job['folder']}"
            mi_path = f"{dest}/{mi_name}"
            mi = lib.load_asset(mi_path) if lib.does_asset_exist(mi_path) else tools.create_asset(
                mi_name, dest, u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
            edit.set_material_instance_parent(mi, master)
            for role, param in (("basecolor", "BaseColorMap"), ("normal", "NormalMap"), ("roughness", "RoughnessMap"),
                                ("ao", "AOMap"), ("metallic", "MetallicMap"), ("orm", "ORMMap")):
                if role in textures:
                    edit.set_material_instance_texture_parameter_value(mi, param, textures[role])
            edit.set_material_instance_scalar_parameter_value(mi, "UsePackedORM", 1.0 if "orm" in textures else 0.0)
            edit.set_material_instance_scalar_parameter_value(mi, "HasMetallicMap", 1.0 if "metallic" in textures else 0.0)
            edit.set_material_instance_scalar_parameter_value(mi, "Tiling", float(job["tiling"]))
            lib.save_loaded_asset(mi, only_if_is_dirty=False)
            entry["material"] = mi.get_path_name()
            entry["textures"] = {r: t.get_path_name() for r, t in textures.items()}
            if job["mesh"]:
                mesh, scale = import_mesh(u, job, dest, 1.0), 1.0
                size = mesh_size(mesh)
                if max(size) < 20.0 and (job.get("expectedMaxCm") or 100) > 50:
                    mesh, scale = import_mesh(u, job, dest, 100.0), 100.0  # authored in metres
                    size = mesh_size(mesh)
                for index in range(len(mesh.get_editor_property("static_materials"))):
                    mesh.set_material(index, mi)
                # With Nanite on, LOD0 render data is the Nanite fallback, so measure with Nanite off first
                # (a re-import keeps the previous Nanite setting).
                settings = mesh.get_editor_property("nanite_settings")
                if settings.get_editor_property("enabled"):
                    settings.set_editor_property("enabled", False)
                    mesh.set_editor_property("nanite_settings", settings)
                tri = mesh_triangles(u, mesh)
                nanite = tri > NANITE_TRIANGLES
                settings.set_editor_property("enabled", nanite)
                mesh.set_editor_property("nanite_settings", settings)
                if job["collision"] and collision_count(mesh) == 0:
                    u.EditorStaticMeshLibrary.add_simple_collisions(mesh, u.ScriptingCollisionShapeType.BOX)
                if job["collision"] and collision_count(mesh) == 0:
                    raise RuntimeError("mesh has no simple collision after import and box fallback")
                lib.save_loaded_asset(mesh, only_if_is_dirty=False)
                b = mesh.get_bounds(); entry["boundsOriginCm"] = [round(b.origin.x, 2), round(b.origin.y, 2), round(b.origin.z, 2)]
                entry.update(mesh=mesh.get_path_name(), importScale=scale, sizeCm=size,
                             vertices=int(mesh.get_num_vertices(0)), triangles=tri, nanite=nanite,
                             simpleCollisions=collision_count(mesh))
            entry["status"] = "imported"
        except Exception as error:  # keep going; the report records the failure
            entry.update(status="failed", error=str(error))
            u.log_error(f"CIRE_FAB_IMPORT_ERROR {job['id']}: {error}")
        results.append(entry)
    lib.save_directory(PACKAGE_ROOT, only_if_is_dirty=False, recursive=True)
    REPORT.write_text(json.dumps({"results": results}, indent=2), encoding="utf-8")
    ok = sum(r["status"] == "imported" for r in results)
    u.log(f"CIRE_FAB_IMPORT_PASS imported={ok} failed={len(results) - ok}")


def mesh_size(mesh) -> list[float]:
    extent = mesh.get_bounds().box_extent
    return [round(extent.x * 2, 2), round(extent.y * 2, 2), round(extent.z * 2, 2)]


def mesh_triangles(u, mesh) -> int:
    return int(mesh.get_num_triangles(0))


def collision_count(mesh) -> int:
    body = mesh.get_editor_property("body_setup")
    if not body:
        return 0
    geometry = body.get_editor_property("agg_geom")
    return sum(len(geometry.get_editor_property(kind)) for kind in ("box_elems", "sphere_elems", "sphyl_elems", "convex_elems"))


def import_mesh(u, job, dest, scale):
    name = f"SM_{job['folder']}"
    suffix = Path(job["mesh"]).suffix.lower()
    task = u.AssetImportTask()
    props = {"filename": job["mesh"], "destination_path": dest, "destination_name": name, "automated": True,
             "save": True, "replace_existing": True, "replace_existing_settings": True}
    if suffix in (".fbx", ".obj"):
        options = u.FbxImportUI()
        for k, v in {"import_mesh": True, "import_as_skeletal": False, "import_animations": False,
                     "import_materials": False, "import_textures": False,
                     "mesh_type_to_import": u.FBXImportType.FBXIT_STATIC_MESH,
                     "automated_import_should_detect_type": False}.items():
            options.set_editor_property(k, v)
        data = options.get_editor_property("static_mesh_import_data")
        for k, v in {"import_uniform_scale": scale, "combine_meshes": True, "generate_lightmap_u_vs": False,
                     "auto_generate_collision": True, "convert_scene": True, "convert_scene_unit": True}.items():
            data.set_editor_property(k, v)
        props.update(options=options, factory=u.FbxFactory())
    for k, v in props.items():
        task.set_editor_property(k, v)
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes = [o for o in task.get_objects() if isinstance(o, u.StaticMesh)]
    if not meshes:
        loaded = u.EditorAssetLibrary.load_asset(f"{dest}/{name}")
        meshes = [loaded] if isinstance(loaded, u.StaticMesh) else []
    if not meshes:
        raise RuntimeError(f"No static mesh imported from {job['mesh']}")
    return meshes[0]


def build_master(u, shared):
    """Shared PBR master: textures default to neutral engine textures when a map is absent."""
    lib, edit = u.EditorAssetLibrary, u.MaterialEditingLibrary
    path = f"{shared}/M_FabPBR"
    if lib.does_asset_exist(path):
        mat = lib.load_asset(path)
        edit.delete_all_material_expressions(mat)  # rebuilt every run so graph fixes always apply
    else:
        mat = u.AssetToolsHelpers.get_asset_tools().create_asset("M_FabPBR", shared, u.Material, u.MaterialFactoryNew())
    for usage in (u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, u.MaterialUsage.MATUSAGE_NANITE):
        edit.set_base_material_usage(mat, usage, True)
    white = u.load_object(None, "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")
    flat = u.load_object(None, "/Engine/EngineMaterials/DefaultNormal.DefaultNormal")
    linear_white = linear_white_texture(u, shared)

    def node(kind, x, y, **props):
        n = edit.create_material_expression(mat, getattr(u, "MaterialExpression" + kind), x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    tiling = node("ScalarParameter", -1400, 0, parameter_name="Tiling", default_value=1.0)
    uv = node("TextureCoordinate", -1400, -120)
    mul = node("Multiply", -1200, -60)
    edit.connect_material_expressions(uv, "", mul, "A")
    edit.connect_material_expressions(tiling, "", mul, "B")

    def tex(name, y, texture, sampler):
        t = node("TextureSampleParameter2D", -900, y, parameter_name=name, texture=texture, sampler_type=sampler)
        edit.connect_material_expressions(mul, "", t, "UVs")
        return t

    color = tex("BaseColorMap", -600, white, u.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tint = node("VectorParameter", -900, -800, parameter_name="Tint", default_value=u.LinearColor(1, 1, 1, 1))
    tinted = node("Multiply", -500, -650)
    edit.connect_material_expressions(color, "RGB", tinted, "A")
    edit.connect_material_expressions(tint, "", tinted, "B")
    edit.connect_material_property(tinted, "", u.MaterialProperty.MP_BASE_COLOR)
    normal = tex("NormalMap", -350, flat, u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    edit.connect_material_property(normal, "RGB", u.MaterialProperty.MP_NORMAL)
    linear = u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    rough = tex("RoughnessMap", -100, linear_white, linear)
    ao = tex("AOMap", 150, linear_white, linear)
    metal = tex("MetallicMap", 400, linear_white, linear)
    orm = tex("ORMMap", 650, linear_white, linear)
    packed = node("ScalarParameter", -600, 900, parameter_name="UsePackedORM", default_value=0.0)
    has_metal = node("ScalarParameter", -600, 1000, parameter_name="HasMetallicMap", default_value=0.0)
    rough_scale = node("ScalarParameter", -600, 1100, parameter_name="RoughnessScale", default_value=1.0)

    def lerp(a, a_pin, b, b_pin, alpha, x, y):
        n = node("LinearInterpolate", x, y)
        edit.connect_material_expressions(a, a_pin, n, "A")
        edit.connect_material_expressions(b, b_pin, n, "B")
        edit.connect_material_expressions(alpha, "", n, "Alpha")
        return n

    zero = node("Constant", -600, 1200, r=0.0)
    metal_sep = lerp(zero, "", metal, "R", has_metal, -350, 450)
    ao_out = lerp(ao, "R", orm, "R", packed, -200, 150)
    rough_mix = lerp(rough, "R", orm, "G", packed, -200, -100)
    rough_out = node("Multiply", 0, -100)
    edit.connect_material_expressions(rough_mix, "", rough_out, "A")
    edit.connect_material_expressions(rough_scale, "", rough_out, "B")
    metal_out = lerp(metal_sep, "", orm, "B", packed, -200, 450)
    edit.connect_material_property(rough_out, "", u.MaterialProperty.MP_ROUGHNESS)
    edit.connect_material_property(ao_out, "", u.MaterialProperty.MP_AMBIENT_OCCLUSION)
    edit.connect_material_property(metal_out, "", u.MaterialProperty.MP_METALLIC)
    edit.recompile_material(mat)
    lib.save_loaded_asset(mat, only_if_is_dirty=False)
    return mat


def linear_white_texture(u, shared):
    """4x4 white PNG imported as a linear (non-sRGB) texture: the neutral default for mask-type samplers."""
    import struct
    import zlib
    name = "T_FabLinearWhite"
    if u.EditorAssetLibrary.does_asset_exist(f"{shared}/{name}"):
        return u.EditorAssetLibrary.load_asset(f"{shared}/{name}")
    raw = b"".join(b"\x00" + b"\xff" * 12 for _ in range(4))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    source = STAGING / "Saved" / f"{name}.png"
    source.write_bytes(png)
    task = u.AssetImportTask()
    for k, v in {"filename": str(source), "destination_path": shared, "destination_name": name,
                 "automated": True, "replace_existing": True, "save": True}.items():
        task.set_editor_property(k, v)
    u.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = u.EditorAssetLibrary.load_asset(f"{shared}/{name}")
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_DEFAULT)
    u.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)
    return texture


# ------------------------------------------------------------------ outside UE
def refresh_slots(catalog: dict, results: list[dict]) -> None:
    by_id = {r["id"]: r for r in results if r.get("status") == "imported"}
    document = json.loads(SLOTS.read_text(encoding="utf-8")) if SLOTS.exists() else {"schemaVersion": 1, "slots": {}}
    for slot_id, slot in document.get("slots", {}).items():
        imported = by_id.get(slot.get("catalogId"))
        if imported:
            key = "material" if slot.get("kind", slot.get("type")) == "material" else "mesh"
            if imported.get(key):
                slot[key] = imported[key]
                slot["status"] = "imported"
    SLOTS.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def run_editor(engine: Path, request: dict, name: str, marker: str) -> str:
    REQUEST.parent.mkdir(parents=True, exist_ok=True)
    REQUEST.write_text(json.dumps(request, indent=2), encoding="utf-8")
    logs = STAGING / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / f"{name}-commandlet.log"
    command = [str(engine / "Engine/Binaries/Win64/UnrealEditor-Cmd.exe"), str(STAGING / "ContentBuilder.uproject"),
               "-nullrhi", "-unattended", "-nosplash", "-nosound", "-nop4", "-run=pythonscript", f"-script={SCRIPT}",
               "-ExecCmds=Interchange.FeatureFlags.Import.FBX 0", f"-abslog={log}", "-stdout", "-FullStdOutLogOutput"]
    print(f"{name}: Unreal log {log}", flush=True)
    flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    with (logs / f"{name}-console.log").open("w", encoding="utf-8") as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=1800, **flags)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    if result.returncode or marker not in text or "Failed to compile Material" in text:
        raise RuntimeError(f"{name} failed (exit {result.returncode}); see {log}. Nothing copied.")
    return text


def selftest_jobs() -> list[dict]:
    """Repo-owned sources only (original Props01 OBJ + authored PNG); proves the pipeline without Fab downloads."""
    return [{"id": "selftest_barrel", "category": "SelfTest", "folder": "SelfTestBarrel", "kind": "mesh",
             "mesh": str(ROOT / "Art/Environment/Props01Sources/SM_CooperedBarrel.obj"),
             "textures": {"basecolor": str(ROOT / "Art/Environment/Textures01/T_AshenMasonry.png")},
             "tiling": 1.0, "expectedMaxCm": 120, "collision": True},
            {"id": "selftest_surface", "category": "SelfTest", "folder": "SelfTestSurface", "kind": "material",
             "mesh": None, "textures": {"basecolor": str(ROOT / "Art/Environment/Textures01/T_BasaltRoad.png")},
             "tiling": 2.0, "expectedMaxCm": None, "collision": False}]


def run_local() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--plan", action="store_true", help="read-only; list what would be imported")
    parser.add_argument("--only", help="comma-separated catalog ids")
    parser.add_argument("--selftest", action="store_true",
                        help="import repo-owned sample sources into the staging project only (no copy, no slot changes)")
    parser.add_argument("--engine", type=Path, default=Path("F:/UE_5.8"))
    options = parser.parse_args()
    catalog = load_catalog()
    only = set(options.only.split(",")) if options.only else None
    if options.plan:
        for item in catalog["items"]:
            if item.get("delivery") == "download" and (not only or item["id"] in only):
                folder = DOWNLOADS / item["category"] / item["folder"]
                print(f"{item['id']:<24} {'present' if folder.is_dir() else 'MISSING':<8} {folder}")
        return 0
    jobs = selftest_jobs() if options.selftest else stage(catalog, only)
    if not jobs:
        print(f"Nothing to import: no downloads found under {DOWNLOADS}. Run with --plan for expected folders.")
        return 1
    run_editor(options.engine, {"jobs": jobs}, "FabImport", "CIRE_FAB_IMPORT_PASS")
    report = json.loads(REPORT.read_text(encoding="utf-8"))
    if options.selftest:
        print(json.dumps(report, indent=2))
        return 0 if all(r["status"] == "imported" for r in report["results"]) else 2
    shutil.copytree(STAGING / "Content/Fab", ROOT / "Content/Fab", dirs_exist_ok=True,
                    ignore=shutil.ignore_patterns("SelfTest"))
    (ROOT / "Saved").mkdir(exist_ok=True)
    shutil.copy2(REPORT, ROOT / "Saved/FabImportReport.json")
    refresh_slots(catalog, report["results"])
    print(json.dumps(report, indent=2))
    return 0 if all(r["status"] == "imported" for r in report["results"]) else 2


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        raise SystemExit(run_local())
    else:
        import_in_editor(unreal)
