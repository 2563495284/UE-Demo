"""Cook and publish the playable Mac app and its matching download together."""

import csv
import ctypes
import fcntl
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "Builds/TreasureIsland/Mac"
APP_NAME = "远征岛.app"
ZIP_NAME = "远征岛-Mac.zip"
GUIDE = ROOT / "Docs/试玩说明.txt"
PREVIEWS = (ROOT / "Scripts/试看沉船.command", ROOT / "Scripts/全岛巡游.command")
EOS_CONFIG = ROOT / "ActDemo/Config/Mac/MacEngine.ini"
EOS_SECTION = "eossdk.platform.expeditionisland"
EOS_FIELDS = ("ProductId", "SandboxId", "DeploymentId", "ClientId", "ClientSecret")


def read_eos_config(path):
    if not path.is_file():
        raise RuntimeError("Mac internet play requires the Island client configuration in ActDemo/Config/Mac/MacEngine.ini")
    values = {}
    section = ""
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw.strip()
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1].casefold()
        elif section == EOS_SECTION and "=" in line and not line.startswith((";", "#")):
            key, value = line.split("=", 1)
            values[key.strip().casefold()] = value.strip().strip('"')
    missing = [key for key in EOS_FIELDS if not values.get(key.casefold())]
    if missing:
        raise RuntimeError("Missing Mac EOS client setting(s): " + ", ".join(missing))
    return tuple(values[key.casefold()] for key in EOS_FIELDS)


def check_eos_package(app, unreal_pak, scratch, expected):
    sdk = "libEOSSDK-Mac-Shipping.dylib"
    if not any(path.is_file() and path.stat().st_size > 0 for path in app.rglob(sdk)):
        raise RuntimeError(f"The archived Mac game is missing {sdk}")
    # Cook strips certain INI entries. Read the copy that the packaged game loads.
    extracted = scratch / "eos-config"
    extracted.mkdir(mode=0o700)
    for pak in (app / "Contents/UE/ActDemo/Content/Paks").glob("*.pak"):
        subprocess.run([unreal_pak, pak, "-Extract", extracted, "-Filter=*Config/Mac/MacEngine.ini"],
                       check=True, stdout=subprocess.DEVNULL)
    staged = list(extracted.rglob("MacEngine.ini"))
    if len(staged) != 1 or read_eos_config(staged[0]) != expected:
        raise RuntimeError("The packaged Mac configuration does not match the configured Island client")


def check_island_content(app, unreal_pak, scratch):
    # Media Framework opens these tutorials as ordinary files in the signed bundle.
    movies = Path("Content/Movies")
    for feature in (ROOT / "ActDemo/Plugins/GameFeatures").glob("Party*/*.uplugin"):
        relative = movies / (feature.stem + ".mp4")
        source = ROOT / "ActDemo" / relative
        staged = app / "Contents/UE/ActDemo" / relative
        if not source.is_file() or not staged.is_file() or staged.read_bytes() != source.read_bytes():
            raise RuntimeError(f"Missing or stale packaged minigame tutorial: {relative}")
    required = {"actdemo/audio/ms_excavationecho.uasset", "actdemo/audio/rockecho.uasset",
                "treasureisland/l_islandfront.umap", "treasureisland/l_expeditionisland.umap",
                "treasureisland/fx/m_islandpassage.uasset", "treasureisland/fx/m_islandground.uasset",
                "treasureisland/fx/m_arenaground.uasset", "treasureisland/art/m_lagoon.uasset",
                "treasureisland/art/harbour/sm_reefshelf.uasset", "treasureisland/art/neutral/sm_windcanvas.uasset",
                "treasureisland/art/neutral/m_wreckfinish.uasset"}
    required.update(f"treasureisland/art/crew/sm_{animal}.uasset" for animal in ("pig", "monkey", "fox", "calf"))
    for animal in ("monkey", "fox", "calf"):
        folder = f"treasureisland/art/crew/{animal}"
        required.update((f"{folder}/sk_expedition{animal}.uasset", f"{folder}/sk_expedition{animal}_skeleton.uasset",
                         f"{folder}/sm_{animal}fist.uasset"))
    required.update({
        "treasureisland/fx/m_routepigment.uasset",
        "treasureisland/art/harbour/sm_rescueraft.uasset",
        "treasureisland/art/harbour/sm_archlintel.uasset",
        "treasureisland/art/harbour/sm_signalmast.uasset",
        "treasureisland/art/harbour/sm_quarryderrick.uasset",
        "treasureisland/art/harbour/sm_fossilheel.uasset",
        "treasureisland/art/neutral/sm_sheavewheel.uasset",
        "treasureisland/art/neutral/sm_tidepool.uasset",
        "treasureisland/art/neutral/sm_rolledcanvas.uasset",
        "treasureisland/art/neutral/m_clodpigment.uasset",
        "treasureisland/art/crew/sm_cofferhasp.uasset",
        "treasureisland/art/crew/sm_cofferkeyway.uasset",
        "treasureisland/art/crew/sm_cofferdial.uasset",
        "treasureisland/art/crew/pig/sk_expeditionpig.uasset",
        "treasureisland/art/crew/pig/sk_expeditionpig_skeleton.uasset",
        "treasureisland/art/crew/pig/m_pigpaint.uasset",
        "treasureisland/art/crew/pig/sm_pigfist.uasset",
    })
    for arena in ("reefrun", "beaconclimb", "crystalhunt", "bridgedash", "floodgates", "crownhold", "mazechase"):
        plugin = f"plugins/gamefeatures/party{arena}/content/"
        required.update((plugin + f"maps/l_{arena}.umap", plugin + f"party{arena}.uasset"))
    retired = ("/l_ashencourt.umap", "/paragonkwang/", "/da_combat.uasset",
               "/content/actdemo/input/", "/mm_attack_01.uasset")
    for index, container in enumerate(sorted((app / "Contents/UE").rglob("*.utoc"))):
        if container.name == "global.utoc":
            continue
        listing = scratch / f"assets-{index}.csv"
        subprocess.run([unreal_pak, f"-ListContainer={container}", f"-Csv={listing}"], check=True)
        with listing.open(encoding="utf-8-sig", newline="") as source:
            for row in csv.DictReader(source, skipinitialspace=True):
                name = "/" + row["Filename"].replace("\\", "/").strip().lower()
                if any(fragment in name for fragment in retired):
                    raise RuntimeError(f"Retired action demo asset in {container.name}: {row['Filename']}")
                required.discard(name.rsplit("/content/", 1)[-1])
                required.discard(name.rsplit("/actdemo/", 1)[-1])
    if required:
        raise RuntimeError(f"Missing island content in the cooked game: {', '.join(sorted(required))}")


def check_voxel_archive(app, unreal_pak, scratch):
    # This file is raw gameplay geometry in the UFS pak, outside the cooked asset registry.
    relative = Path("TreasureIsland/VoxelSources/SceneryTriangles.bin")
    source = ROOT / "ActDemo/Content" / relative
    extracted = scratch / "voxel-source-check"
    extracted.mkdir()
    for pak in (app / "Contents/UE/ActDemo/Content/Paks").glob("*.pak"):
        subprocess.run([unreal_pak, pak, "-Extract", extracted, f"-Filter=*{relative.as_posix()}"],
                       check=True, stdout=subprocess.DEVNULL)
    copies = list(extracted.rglob(relative.name))
    if len(copies) != 1 or not source.is_file() or copies[0].read_bytes() != source.read_bytes():
        raise RuntimeError("Cooked expedition voxel triangles differ from the authored island meshes")


def prepare_download(directory, previews=PREVIEWS):
    app = directory / APP_NAME
    (directory / "ActDemo.app").rename(app)
    with (app / "Contents/Info.plist").open("rb") as source:
        info = plistlib.load(source)
    if info.get("CFBundleIdentifier") != "game.expeditionisland.mac" or info.get("CFBundleDisplayName") != "远征岛":
        raise RuntimeError(f"Unexpected product identity in {app}/Contents/Info.plist")
    executable = "Contents/MacOS/" + info["CFBundleExecutable"]
    if not os.access(app / executable, os.X_OK) or not any((app / "Contents/UE").rglob("*.ucas")):
        raise RuntimeError(f"Unreal did not archive a playable Mac application in {app}")
    subprocess.run(["/usr/bin/codesign", "--verify", "--deep", "--strict", app], check=True)

    guide = directory / GUIDE.name
    shutil.copyfile(GUIDE, guide)
    for preview in previews:
        shutil.copy2(preview, directory / preview.name)
    archive = directory / ZIP_NAME
    # Apple's bsdtar marks Chinese member paths as UTF-8 and keeps executable modes and symlinks.
    subprocess.run(["/usr/bin/tar", "--format", "zip", "--mac-metadata", "--options", "zip:hdrcharset=UTF-8",
                    "-cf", archive, "-C", directory, APP_NAME, guide.name, *(p.name for p in previews)], check=True)
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() or bundle.read(guide.name) != guide.read_bytes():
            raise RuntimeError(f"Archive verification failed for {archive}")
        bundle.getinfo(f"{APP_NAME}/{executable}")
        for preview in previews:
            if bundle.read(preview.name) != preview.read_bytes() or not (bundle.getinfo(preview.name).external_attr >> 16) & 0o111:
                raise RuntimeError(f"{preview.name} is missing or not executable in {archive}")

    # These two names were distributed in earlier releases and are also used by QA.
    (directory / "ActDemo.app").symlink_to(APP_NAME, target_is_directory=True)
    (directory / "ActDemo.zip").symlink_to(ZIP_NAME)


def replace_entry(source, destination):
    if not os.path.lexists(destination):
        source.rename(destination)
        return
    # macOS exchanges directories in a single operation. Readers get the complete
    # old release or the complete new one, even if packaging is interrupted.
    renamex = ctypes.CDLL(None, use_errno=True).renamex_np
    renamex.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
    rename_swap = 0x00000002  # RENAME_SWAP from <sys/stdio.h>
    if renamex(os.fsencode(source), os.fsencode(destination), rename_swap) != 0:
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(destination))


def check_legacy_identity(info, path):
    if info.get("CFBundleIdentifier") != "com.YourCompany.ActDemo" or info.get("CFBundleExecutable") != "ActDemo":
        raise RuntimeError(f"Unrelated app in {path}; move it before packaging")


def legacy_link_target(alias):
    target = Path(os.path.relpath(OUTPUT, alias.parent))
    if alias.is_symlink():
        if alias.readlink() == target:
            return None
        if alias.resolve() != OUTPUT.resolve():
            raise RuntimeError(f"Other release link at {alias}: {alias.readlink()}")
    elif alias.exists():
        if not alias.is_dir():
            raise RuntimeError(f"Existing file at {alias}; move it before packaging")
        entries = {item.name for item in alias.iterdir()} - {".DS_Store"}
        # Only retire recognizable generated products. Handwritten material must survive.
        unknown = entries - {"ActDemo.app", "ActDemo.zip"}
        if unknown:
            raise RuntimeError(f"Move unrecognized files out of {alias} before packaging: {sorted(unknown)}")
        if "ActDemo.app" in entries:
            with (alias / "ActDemo.app/Contents/Info.plist").open("rb") as source:
                check_legacy_identity(plistlib.load(source), alias)
        if "ActDemo.zip" in entries:
            with zipfile.ZipFile(alias / "ActDemo.zip") as archive:
                other_files = {name.split("/")[0] for name in archive.namelist()} - {"ActDemo.app", "__MACOSX", GUIDE.name}
                if other_files:
                    raise RuntimeError(f"Other files in {alias / 'ActDemo.zip'}: {sorted(other_files)}")
                check_legacy_identity(plistlib.loads(archive.read("ActDemo.app/Contents/Info.plist")), alias)
    return target


def main():
    engine = Path(sys.argv[1]) / "Engine"
    uat = engine / "Build/BatchFiles/RunUAT.sh"
    samples = {
        "--pig-sample": ("ExpeditionPunchPig", "试玩探险小猪.command"),
        "--crew-sample": ("ExpeditionCrew", "试玩港口船员.command"),
    }
    requested = [flag for flag in samples if flag in sys.argv[2:]]
    if len(requested) > 1:
        raise ValueError("Select one playable sample per build")
    sample = samples[requested[0]] if requested else None
    output = ROOT / "Builds" / sample[0] / "Mac" if sample else OUTPUT
    previews = PREVIEWS + (ROOT / "Scripts" / sample[1],) if sample else PREVIEWS
    uat_args = [arg for arg in sys.argv[2:] if arg not in samples]
    expected = read_eos_config(EOS_CONFIG)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Serialize cooks in this checkout and keep the sample separate from the public playtest.
    with (ROOT / "ActDemo/Saved/.island-cook.lock").open("a") as cook_lock, (output.parent / ".package.lock").open("a") as lock:
        fcntl.flock(cook_lock, fcntl.LOCK_EX)
        fcntl.flock(lock, fcntl.LOCK_EX)
        with tempfile.TemporaryDirectory(prefix=".package-", dir=output.parent) as scratch:
            directory = Path(scratch) / "Mac"
            project = ROOT / "ActDemo/ActDemo.uproject"
            if sample:
                # UAT puts its -ubtargs inside a single target; a multi-target build
                # needs the mutex argument on the outer UBT invocation.
                subprocess.run([engine / "Build/BatchFiles/Mac/Build.sh",
                                f'-Target=ActDemoEditor Mac Development -Project="{project}"',
                                f'-Target=ActDemo Mac Development -Project="{project}"',
                                "-WaitMutex", "-NoHotReload", "-NoUBA", "-MaxParallelActions=6",
                                f"-log={ROOT / 'ActDemo/Saved/Logs/PunchPigReleaseBuild.log'}"], check=True)
            subprocess.run([
                engine / "Binaries/Mac/UnrealEditor-Cmd", project,
                "-run=ActIslandVoxelSources", "-unattended", "-nullrhi", "-nosound", "-NoSplash",
                f"-abslog={ROOT / 'ActDemo/Saved/Logs/IslandVoxelArchive.log'}",
            ], check=True)
            subprocess.run([
                uat, "BuildCookRun", f"-project={ROOT / 'ActDemo/ActDemo.uproject'}",
                "-noP4", "-platform=Mac", "-clientconfig=Development",
                "-skipbuild" if sample else "-build", "-cook", "-stage", "-pak", "-package", "-archive",
                f"-archivedirectory={directory}", "-utf8output", "-unattended", *uat_args,
            ], check=True)
            app = directory / "ActDemo.app"
            unreal_pak = engine / "Binaries/Mac/UnrealPak"
            check_eos_package(app, unreal_pak, Path(scratch), expected)
            check_island_content(app, unreal_pak, Path(scratch))
            check_voxel_archive(app, unreal_pak, Path(scratch))
            prepare_download(directory, previews)
            aliases = [] if sample else [ROOT / "Builds/Mac", ROOT / "Builds/VoxelPlayground/Mac"]
            pending = [(path, legacy_link_target(path)) for path in aliases]
            if output.is_dir():
                extras = {item.name for item in output.iterdir()} - {APP_NAME, ZIP_NAME, "ActDemo.app", "ActDemo.zip", GUIDE.name, *(p.name for p in previews), ".DS_Store"}
                if extras:
                    raise RuntimeError(f"Move unrelated files out of {output} before packaging: {sorted(extras)}")
            replace_entry(directory, output)
            for index, (alias, target) in enumerate(pending):
                if target is not None:
                    alias.parent.mkdir(parents=True, exist_ok=True)
                    link = Path(scratch) / f"previous-{index}"
                    link.symlink_to(target, target_is_directory=True)
                    replace_entry(link, alias)
    print(output / ZIP_NAME)


if __name__ == "__main__":
    main()
