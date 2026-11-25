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
    required = {"treasureisland/l_islandfront.umap", "treasureisland/l_expeditionisland.umap",
                "treasureisland/fx/m_islandpassage.uasset", "treasureisland/fx/m_partyground.uasset"}
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


def prepare_download(directory):
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
    archive = directory / ZIP_NAME
    # Apple's bsdtar marks Chinese member paths as UTF-8 and keeps executable modes and symlinks.
    subprocess.run(["/usr/bin/tar", "--format", "zip", "--mac-metadata", "--options", "zip:hdrcharset=UTF-8",
                    "-cf", archive, "-C", directory, APP_NAME, guide.name], check=True)
    with zipfile.ZipFile(archive) as bundle:
        if bundle.testzip() or bundle.read(guide.name) != guide.read_bytes():
            raise RuntimeError(f"Archive verification failed for {archive}")
        bundle.getinfo(f"{APP_NAME}/{executable}")

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
    expected = read_eos_config(EOS_CONFIG)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    # UAT also writes to shared project build and cook intermediates.
    with (OUTPUT.parent / ".package.lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        with tempfile.TemporaryDirectory(prefix=".package-", dir=OUTPUT.parent) as scratch:
            directory = Path(scratch) / "Mac"
            subprocess.run([
                uat, "BuildCookRun", f"-project={ROOT / 'ActDemo/ActDemo.uproject'}",
                "-noP4", "-platform=Mac", "-clientconfig=Development",
                "-build", "-cook", "-stage", "-pak", "-package", "-archive",
                f"-archivedirectory={directory}", "-utf8output", "-unattended", *sys.argv[2:],
            ], check=True)
            app = directory / "ActDemo.app"
            unreal_pak = engine / "Binaries/Mac/UnrealPak"
            check_eos_package(app, unreal_pak, Path(scratch), expected)
            check_island_content(app, unreal_pak, Path(scratch))
            prepare_download(directory)
            aliases = [ROOT / "Builds/Mac", ROOT / "Builds/VoxelPlayground/Mac"]
            pending = [(path, legacy_link_target(path)) for path in aliases]
            if OUTPUT.is_dir():
                extras = {item.name for item in OUTPUT.iterdir()} - {APP_NAME, ZIP_NAME, "ActDemo.app", "ActDemo.zip", GUIDE.name, ".DS_Store"}
                if extras:
                    raise RuntimeError(f"Move unrelated files out of {OUTPUT} before packaging: {sorted(extras)}")
            replace_entry(directory, OUTPUT)
            for index, (alias, target) in enumerate(pending):
                if target is not None:
                    alias.parent.mkdir(parents=True, exist_ok=True)
                    link = Path(scratch) / f"previous-{index}"
                    link.symlink_to(target, target_is_directory=True)
                    replace_entry(link, alias)
    print(OUTPUT / ZIP_NAME)


if __name__ == "__main__":
    main()
