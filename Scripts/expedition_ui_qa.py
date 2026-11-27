#!/usr/bin/env python3
"""Drive the expedition front end in rendered Mac processes and collect viewport evidence."""

import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import signal
import shutil
import subprocess
import sys
import time
import uuid

sys.dont_write_bytecode = True
from voxel_qa import PROJECT, ROOT, read_log


def activate_game_window(pid):
    script = f"""ObjC.import("AppKit");
        var app = $.NSRunningApplication.runningApplicationWithProcessIdentifier({int(pid)});
        if (app) app.activateWithOptions(3);
        var front = false;
        for (var n = 0; n < 12 && !front; ++n) {{
            delay(.05);
            front = $.NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier === {int(pid)};
        }}
        front;"""
    answer = subprocess.run(["/usr/bin/osascript", "-l", "JavaScript", "-e", script],
                            capture_output=True, text=True, timeout=3)
    return answer.returncode == 0 and answer.stdout.strip() == "true"


def recording_encoder(override=None):
    candidates = [override, os.environ.get("FFMPEG_BINARY"), shutil.which("ffmpeg")]
    if sys.platform == "darwin":
        candidates.append("/Applications/Virtual Desktop Streamer.app/Contents/MonoBundle/ffmpeg")
    for binary in dict.fromkeys(path for path in candidates if path):
        if not os.access(binary, os.X_OK):
            continue
        for options in (["-c:v", "libx264", "-preset", "veryfast"],
                        ["-c:v", "h264_videotoolbox", "-allow_sw", "1", "-b:v", "5M"],
                        ["-c:v", "mpeg4", "-q:v", "3"]):
            # Check codec initialization as well as missing dynamic libraries before starting the game.
            check = subprocess.run([str(binary), "-nostdin", "-hide_banner", "-v", "error",
                                    "-f", "lavfi", "-i", "color=size=64x64:rate=1",
                                    "-frames:v", "1", *options, "-f", "null", "-"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if check.returncode == 0:
                return str(binary), options
    raise RuntimeError("No working ffmpeg video encoder. Supply --ffmpeg /path/to/ffmpeg or set FFMPEG_BINARY.")


def encode_replay(directory, role, destination, encoder=None, audio=None, departure=False, source=None):
    binary, codec_options = encoder or recording_encoder()
    folder = role + ("_frames_viewport" if source == "game_viewport" else "_frames")
    frames = sorted((directory / folder).glob("*.png"))
    if len(frames) < 5:
        raise RuntimeError("Not enough viewport frames for " + role)
    lines = ["ffconcat version 1.0"]
    for first, second in zip(frames, frames[1:]):
        lines.append(f"file '{first.name}'")
        if departure:
            lines.append("option framerate 1000")
        lines.append(f"duration {(int(second.stem) - int(first.stem)) / 1000:.3f}")
    lines.append(f"file '{frames[-1].name}'")
    if departure:
        lines.append("option framerate 1000")
    manifest = frames[0].parent / "timing.ffconcat"
    manifest.write_text("\n".join(lines) + "\n")
    movie = destination / (role + ".mp4")
    audio_input = []
    audio_output = []
    if audio is not None and audio.is_file():
        # The mixer starts with the camera change. The first captured frame may arrive later.
        audio_input = ["-ss", f"{int(frames[0].stem) / 1000:.3f}", "-i", str(audio)]
        audio_output = ["-map", "0:v", "-map", "1:a", "-af", "apad", "-c:a", "aac", "-shortest"]
    video = ["-vf", "format=yuv420p", "-fps_mode", "vfr"] if departure else ["-vf", "fps=24,format=yuv420p"]
    subprocess.run([binary, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "concat", "-safe", "0" if departure else "1",
                    "-i", str(manifest), *audio_input, *video, *codec_options, *audio_output,
                    "-movflags", "+faststart", str(movie)], check=True)
    return movie.name


def inspect_departure(directory, role, log):
    from PIL import Image, ImageChops, ImageStat
    count = re.search(r"ISLAND_UI_DEPARTURE_FRAMES saved=(\d+) source=(\w+)", log)
    if not count:
        raise RuntimeError(f"{role} did not report its recording source")
    folder = role + ("_frames_viewport" if count[2] == "game_viewport" else "_frames")
    frames = sorted((directory / folder).glob("*.png"))
    if len(frames) != int(count[1]) or len(frames) < 5:
        raise RuntimeError(f"{role} did not capture its window throughout departure")
    stray = []
    checker = []
    caption_sizes = set()
    detailed = 0
    for frame in frames:
        with Image.open(frame) as image:
            rgb = image.convert("RGB")
        sample = rgb.resize((96, 54))
        detailed += min(ImageStat.Stat(sample).stddev) > 10
        # The default spawn camera briefly showed open blue-gray water with a black right pillar.
        water = ImageStat.Stat(sample.crop((43, 38, 72, 50)))
        r, g, b = water.mean
        black_edge = max(ImageStat.Stat(sample.crop((93, 15, 96, 49))).mean) < 8
        sky = ImageStat.Stat(sample.crop((43, 13, 72, 28))).mean
        if black_edge and max(water.stddev) < 12 and 65 < r < 170 and g > r + 12 and b > g + 5 and min(sky) > r + 30:
            stray.append(frame.name)
        # The untextured terrain covered the center of the dock view in repeating gray squares.
        ground = sample.crop((34, 11, 79, 31))
        colors = list(ground.getdata())
        gray = sum(max(c)-min(c) < 28 for c in colors)
        if gray > .94 * len(colors) and min(ImageStat.Stat(ground).mean) > 120:
            luma = ground.convert("L")
            step = ImageChops.difference(luma.crop((1, 1, 45, 20)), luma.crop((0, 0, 44, 19)))
            if ImageStat.Stat(step).mean[0] > 3.5:
                checker.append(frame.name)
        # Measure actual caption ink only when the whole image outside the center matches the matte.
        if max(ImageStat.Stat(sample.crop((0, 0, 96, 12))).stddev) < 1:
            matte = Image.new("RGB", rgb.size, rgb.getpixel((0, 0)))
            ink = ImageChops.difference(rgb, matte).convert("L").point(lambda value: 255 if value > 25 else 0).getbbox()
            if ink and rgb.width * .2 < ink[0] < ink[2] < rgb.width * .8 and rgb.height * .4 < ink[1] < ink[3] < rgb.height * .6:
                caption_sizes.add((ink[2]-ink[0], ink[3]-ink[1]))
    times = [int(frame.stem) for frame in frames]
    if detailed < 5 or times[-1]-times[0] < 3000:
        raise RuntimeError(f"{role} did not record recognizable game scenes across the countdown")
    return dict(source=count[2], frames=len(times), duration_ms=times[-1]-times[0],
                longest_present_interval_ms=max(b-a for a, b in zip(times, times[1:])),
                default_camera_frames=stray, untextured_dock_frames=checker, caption_ink_sizes_px=sorted(caption_sizes))


def verify_chart_walk(directory, role, log, foreground):
    from math import dist
    from PIL import Image
    sample = re.search(r"ISLAND_UI_CHART_WALK start=([-\d.]+),([-\d.]+) end=([-\d.]+),([-\d.]+) shore=(\d+)", log)
    released = re.search(r"ISLAND_UI_CHART_RELEASE continued_cm=([\d.]+)", log)
    if not sample or not released:
        raise RuntimeError(f"{role} did not finish the held-Tab and released-Tab walk")
    source = "raw" if foreground else "axis"
    camera = {stage: (float(yaw), float(pitch)) for stage, yaw, pitch in re.findall(
        rf"ISLAND_UI_CHART_LOOK stage=(before|held|released) input={source} yaw=([-\d.]+) pitch=([-\d.]+)", log)}
    if set(camera) != {"before", "held", "released"} or any(a * b >= 0 for a, b in zip(camera["held"], camera["released"])):
        raise RuntimeError(f"{role}: opposite mouse movements must turn the camera on both axes with Tab held and released")
    x0, y0, x1, y1, extent = map(float, sample.groups())
    markers = []
    projected = []
    for tag, (x, y) in zip(("Start", "End"), ((x0, y0), (x1, y1))):
        with Image.open(directory / f"{role}_ChartWalk{tag}.png") as frame:
            image = frame.convert("RGB")
        w, h = image.size
        scale = min(w * .76, h * .78) / (2.3 * extent)
        center = (w * .5 + y * scale, h * .5 - x * scale)
        # The local chevron uses Alert's gold ink. Its solid strokes are distinct from
        # translucent map labels, crew dots and the static white docking marks.
        cx, cy = map(round, center)
        pixels = [(px, py) for py in range(max(0, cy-8), min(h, cy+9))
                  for px in range(max(0, cx-8), min(w, cx+9))
                  if all(abs(a-b) <= 3 for a, b in zip(image.getpixel((px, py)), (254, 225, 183)))]
        if len(pixels) < 6:
            raise RuntimeError(f"{role} {tag}: no player chevron rendered near {center}")
        markers.append(tuple(sum(p[axis] for p in pixels) / len(pixels) for axis in range(2)))
        projected.append(center)
    expected = tuple(b-a for a, b in zip(*projected))
    rendered = tuple(b-a for a, b in zip(*markers))
    if dist((x0, y0), (x1, y1)) < 349 or float(released[1]) < 99 or dist(expected, rendered) > 4 or dist(*markers) < 6:
        raise RuntimeError(f"{role}: marker displacement {rendered} differs from world projection {expected}")
    return dict(held_cm=round(dist((x0, y0), (x1, y1)), 1), released_cm=float(released[1]),
                mouse_input=source, camera_yaw_pitch_deg=camera,
                rendered_markers_px=[[round(v, 2) for v in marker] for marker in markers])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", action="store_true")
    parser.add_argument("--app", type=Path, default=ROOT / "Builds/TreasureIsland/Mac/远征岛.app")
    parser.add_argument("--scenario", choices=("solo", "pair", "recovery", "walkthrough", "chart", "settings", "character", "internet"), default="pair")
    parser.add_argument("--role", choices=("host", "guest", "solo", "directory"),
                        help="select a cross-network player, solo host or same-Mac public-directory check")
    parser.add_argument("--room-name", help="unique shared internet test room name (letters, numbers, hyphens or underscores)")
    parser.add_argument("--force-relay", action="store_true", help="require an actual EOS relay connection in a two-device internet test")
    parser.add_argument("--weak", action="store_true")
    parser.add_argument("--foreground", action="store_true", help="run one interactive Mac game and verify it owns the foreground")
    parser.add_argument("--edges", action="store_true", help="exercise settings, team changes and withdrawn readiness in the pair run")
    parser.add_argument("--perspective", action="store_true", help="press V in game, check both camera positions and digging/throwing aim")
    parser.add_argument("--chart-move", action="store_true", help="check mouse look and held W across a Tab press and release, including the rendered player marker")
    parser.add_argument("--shots", action="store_true")
    parser.add_argument("--record", action="store_true", help="record the game viewport and encode a replay using ffmpeg")
    parser.add_argument("--departure", action="store_true",
                        help="record every presented game-window frame from startup through crew control (solo or pair)")
    parser.add_argument("--ffmpeg", help="path to a working ffmpeg executable for --record")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/ExpeditionUI")
    parser.add_argument("--viewport", metavar=("WIDTH", "HEIGHT"), type=int, nargs=2, default=(1280, 720),
                        help="game window dimensions for UI layout checks")
    parser.add_argument("--port", type=int, default=7817)
    parser.add_argument("--seed", type=int, help="Replay a known layout in the chart or full walking scenario")
    args = parser.parse_args()
    if args.departure:
        if (args.scenario not in ("solo", "pair") or args.foreground or args.perspective
                or args.chart_move or args.shots or args.record):
            parser.error("--departure requires a solo or pair run and performs its own recording")
    internet = args.scenario == "internet"
    if internet:
        if not args.role or (args.role in ("host","guest") and not args.room_name):
            parser.error("internet requires --role; host and guest also require the same unique --room-name")
        if args.role in ("solo","directory") and args.force_relay:
            parser.error("relay verification requires two players")
        args.room_name = args.room_name or "IslandQA-" + uuid.uuid4().hex[:12]
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,32}", args.room_name):
            parser.error("--room-name must be 1 to 32 letters, numbers, hyphens or underscores")
    elif args.role or args.room_name or args.force_relay:
        parser.error("--role, --room-name and --force-relay require --scenario internet")
    if args.foreground and (sys.platform != "darwin" or args.scenario == "pair"):
        parser.error("--foreground requires a single Mac game window")
    if args.perspective and args.scenario not in ("solo", "pair"):
        parser.error("--perspective requires a solo or pair run")
    if args.chart_move:
        if args.scenario not in ("solo", "pair"):
            parser.error("--chart-move requires a solo or pair run")
        args.shots = True
    if args.scenario == "chart":
        args.shots = True
    encoder = recording_encoder(args.ffmpeg) if args.record or args.departure else None
    # One directory per run keeps earlier captures and exposes the exact processes/logs under test.
    scenario_label = f"internet-{args.role}" if internet else args.scenario
    directory = args.output.resolve() / (scenario_label + ("-weak-" if args.weak else "-normal-") + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True)
    engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
    command = ([str(engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"), str(PROJECT), "-game", "-NoAssetRegistryCacheWrite"]
               if args.editor else [str(args.app / "Contents/MacOS/ActDemo")])
    if internet:
        roles = {"host": ["InternetHost"], "guest": ["InternetGuest"], "solo": ["Solo"],
                 "directory": ["DirectoryHost", "DirectoryObserver"]}[args.role]
    else:
        roles = {"solo": ["Solo"], "pair": ["Host", "Guest"], "recovery": ["Recovery"],
                 "walkthrough": ["Walkthrough"], "chart": ["Chart"], "settings": ["Settings", "SettingsReload"],
                 "character": ["Character"]}[args.scenario]
    game_output = directory
    if not args.editor:
        with (args.app / "Contents/Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream)["CFBundleIdentifier"]
        # A sandboxed Mac application exports its own viewport to its container.
        game_output = Path.home() / "Library/Containers" / bundle / "Data/tmp/ExpeditionUI" / directory.name
    resolution = [f"-ResX={args.viewport[0]}", f"-ResY={args.viewport[1]}"]
    shared = ["-windowed", *resolution, "-NoSplash", "-nosound", "-NoTrace",
              "-unattended", "-NoScreenMessages", "-ExecCmds=t.MaxFPS 60", "-AlwaysFocus",
              "-stdout", "-FullStdOutLogOutput", f"-IslandUISeats={2 if args.role in ('host', 'guest') else len(roles)}", f"-IslandUIOutput={game_output}"]
    if internet:
        shared += [f"-IslandUIRoom={args.room_name}", "-IslandUIInternet"]
        if args.role == "directory":
            game_output.mkdir(parents=True, exist_ok=True)
        if args.role == "host":
            shared += ["-ExpeditionWalkQA", "-ExpeditionTreasureWalkQA"]
            if args.seed is not None:
                shared.append(f"-ExpeditionSeed={args.seed}")
        if args.force_relay:
            shared.append("-IslandForceRelay")
    else:
        shared.append("-IslandLAN")
    if args.foreground:
        shared.remove("-unattended")
    if args.scenario == "settings":
        # Exercise the same display startup path as Finder in a disposable legacy profile.
        # Both launches must see the same files, without changing the player's preferences.
        for flag in ("-windowed", *resolution, "-nosound"):
            shared.remove(flag)
        profile = game_output / "profile"
        legacy = profile / "Saved" / "Config" / ("MacEditor" if args.editor else "Mac") / "GameUserSettings.ini"
        legacy.parent.mkdir(parents=True, exist_ok=True)
        legacy.write_text("[/Script/Engine.GameUserSettings]\nVersion=5\nFullscreenMode=1\nLastConfirmedFullscreenMode=1\n")
    if args.scenario in ("chart", "walkthrough"):
        shared += ["-ExpeditionWalkQA", "-ExpeditionTreasureWalkQA"]
        if args.seed is not None:
            shared.append(f"-ExpeditionSeed={args.seed}")
    if args.shots:
        shared.append("-IslandUIShots")
    if args.perspective:
        shared.append("-IslandUIPerspective")
    if args.chart_move:
        shared.append("-IslandUIChartMove")
    if args.edges:
        if args.scenario != "pair":
            parser.error("--edges requires a pair of players")
        shared.append("-IslandUIEdges")
    if args.record:
        if args.shots:
            parser.error("Use separate runs for recording and named screenshots.")
        shared.append("-IslandUIRecord")
    if args.departure:
        shared.append("-IslandUIDeparture")
    if args.weak:
        shared += ["-PktLag=40", "-PktLoss=2"]

    processes = {}
    activated = set()

    def stop_players():
        for process, _ in processes.values():
            if process.poll() is None:
                os.killpg(process.pid, signal.SIGTERM)
        for process, _ in processes.values():
            try:
                process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()

    def copy_screenshots():
        if game_output != directory and game_output.is_dir():
            for capture in game_output.glob("*.png"):
                shutil.copy2(capture, directory / capture.name)

    try:
        def start(role, extra=()):
            offset = roles.index(role)
            log = directory / (role + ".log")
            profile = game_output / ("profile" if args.scenario == "settings" else f"{role}_profile")
            if args.edges and role == "Guest":
                # Use a guest with a different language to cover the local UI and LAN message boundary.
                ini = profile / "Saved" / "Config" / ("MacEditor" if args.editor else "Mac") / "GameUserSettings.ini"
                ini.parent.mkdir(parents=True, exist_ok=True)
                ini.write_text("[/Script/ActDemo.IslandClientSettings]\nLanguage=en\n")
            with log.open("w") as stream:
                # Internet probes use separate systems or background windows. Let Slate center
                # each in the desktop's current primary work area, including multi-display setups.
                placement = [] if internet else [f"-WinX={32 + 175 * offset}", f"-WinY={60 + 90 * offset}"]
                process = subprocess.Popen(command + shared + [f"-IslandUIProbe={role}", f"-port={args.port + offset}",
                    *placement, f"-UserDir={profile}", *extra],
                    stdout=stream, stderr=subprocess.STDOUT, cwd=ROOT, start_new_session=True)
            processes[role] = process, log
            print(f"{role} pid={process.pid} log={log}", flush=True)

        start(roles[0])
        deadline = time.monotonic() + (1800 if args.role in ("host","guest") else 1500 if args.scenario == "walkthrough" else 360 if args.editor or args.scenario == "chart" or args.role=="directory" else 210)
        while time.monotonic() < deadline:
            logs = {role: read_log(log) if log.exists() else "" for role, (_, log) in processes.items()}
            if args.foreground:
                role = next(reversed(processes))
                if args.scenario == "settings":
                    phases = [(f"{role}.home", "Game Engine Initialized")]
                    if role == "SettingsReload":
                        phases.append(("SettingsReload.preview", "ISLAND_UI_PRESS tag=Island.Client.Display "))
                else:
                    play_signal = "ISLAND_UI_WALK_CONTROL" if role == "Walkthrough" else "ISLAND_UI_CHART"
                    if args.chart_move:
                        play_signal = "ISLAND_UI_MOUSE_BASELINE_READY"
                    phases = [("home", "Game Engine Initialized"), ("play", play_signal)]
                    if role == "Character":
                        phases.append(("picker", "ISLAND_UI_PRESS tag=Island.Character.1 "))
                        phases.append(("pointer", "ISLAND_UI_INPUT tag=Island.Character.2 reason=outside_hit_grid"))
                for phase, signal_text in phases:
                    if phase not in activated and signal_text in logs[role] and activate_game_window(processes[role][0].pid):
                        activated.add(phase)
                        print(f"ISLAND_UI_NATIVE_FOREGROUND role={role} phase={phase} verified=1", flush=True)
            if "Guest" in roles and "Guest" not in processes and "ISLAND_LAN_ARRIVED host=1" in logs["Host"]:
                start("Guest")
                continue
            if args.role == "directory":
                host = logs["DirectoryHost"]
                if "DirectoryObserver" not in processes and "ISLAND_EOS_ADVERTISED visible=1 occupied=1 seats=8" in host:
                    start("DirectoryObserver")
                    continue
                observer = logs.get("DirectoryObserver", "")
                for signal_file, marker, source in (
                    ("observed-open", "ISLAND_UI_DIRECTORY_OPEN occupied=1 max=8", observer),
                    ("host-hunting", "ISLAND_UI_DIRECTORY_HUNTING", host),
                    ("observed-hidden", "ISLAND_UI_DIRECTORY_HIDDEN", observer),
                    ("host-closed", "ISLAND_EOS_HOST_CLOSED", host),
                ):
                    path = game_output / signal_file
                    if marker in source and not path.exists():
                        path.touch()
            if args.scenario == "settings" and "SettingsReload" not in processes:
                saved = re.search(r"ISLAND_UI_DONE role=Settings confirmed_window=(\d+)x(\d+)", logs["Settings"])
                if saved:
                    stop_players()
                    start("SettingsReload", [f"-IslandExpectedX={saved[1]}", f"-IslandExpectedY={saved[2]}"])
                    continue
            for role, (process, log) in processes.items():
                failure = re.search(r"(?:ISLAND_UI_FAIL|EXPEDITION_WALK_FAIL|Fatal error:|Assertion failed:).*", logs[role])
                if failure or (process.poll() is not None and "ISLAND_UI_DONE" not in logs[role]):
                    raise RuntimeError(f"{role} {failure[0] if failure else process.returncode}. See {log}")
            if len(logs) == len(roles) and all("ISLAND_UI_DONE" in log for log in logs.values()):
                if args.foreground and not activated:
                    raise RuntimeError("The operating system did not confirm the test game owned the foreground")
                if args.foreground and args.scenario == "settings" and not {"Settings.home", "SettingsReload.home", "SettingsReload.preview"} <= activated:
                    raise RuntimeError("Both settings launches and the display-mode preview must own the foreground")
                if args.foreground and not internet and roles == ["Solo"] and (
                    "play" not in activated or "ISLAND_UI_PRESS tag=Island.Resume input=pointer" not in logs["Solo"]
                ):
                    raise RuntimeError("The active game window did not resume through the pointer button")
                events = {}
                for role, log in logs.items():
                    if args.weak and ("PktLag set to 40" not in log or "PktLoss set to 2" not in log):
                        raise RuntimeError("Packet simulation was not active on " + role)
                    needs_chart = not (args.departure or role.startswith(("Internet","Directory")) or role in ("Recovery", "Walkthrough", "Settings", "SettingsReload", "Character"))
                    if "ISLAND_UI_PRESS" not in log or (needs_chart and "ISLAND_UI_CHART" not in log):
                        raise RuntimeError("Actual UI and chart input evidence missing on " + role)
                    if args.perspective:
                        for mode in ("third", "first", "steep", "restored", "chart_blocked"):
                            if not re.search(rf"ISLAND_UI_PERSPECTIVE mode={mode}\b", log):
                                raise RuntimeError(f"Missing {mode} camera check on {role}")
                    events[role] = re.findall(r"ISLAND_(?:UI_(?:PRESS|CHART|PERSPECTIVE|EOS_\w+|DONE)|(?:EOS|LAN)_(?:GUEST_READY|SEARCH|CONNECTION|ARRIVED))[^\r\n]*", log)
                if internet:
                    role = roles[0]
                    log = logs[role]
                    required = ["ISLAND_EOS_GUEST_READY", "ISLAND_EOS_ARRIVED host=" + ("0" if args.role == "guest" else "1")]
                    if args.role in ("host","guest"):
                        required += ["ISLAND_UI_EOS_PARTY", "ISLAND_UI_EOS_EDIT", "ISLAND_UI_EOS_RETURN", "host_closed=1 listing_removed=1"]
                    if args.role == "guest":
                        required += ["ISLAND_UI_PRESS tag=Island.JoinListing", "ISLAND_EOS_SEARCH"]
                    if args.role == "host":
                        required.append("EXPEDITION_TREASURE_WALK_DONE")
                    if args.force_relay:
                        required.append("ISLAND_EOS_CONNECTION relay")
                    for marker in required:
                        if marker not in log:
                            raise RuntimeError(f"{role} lacks {marker}. See {directory}")
                    if args.role == "directory":
                        observer = logs["DirectoryObserver"]
                        if not all(marker in observer for marker in ("ISLAND_EOS_GUEST_READY", "ISLAND_UI_DIRECTORY_OPEN",
                            "ISLAND_UI_DIRECTORY_HIDDEN", "absent_after_close=1", "ISLAND_EOS_SEARCH")):
                            raise RuntimeError("Observer did not confirm occupancy, disappearance and closure")
                        if "ISLAND_EOS_ARRIVED" in observer or "ISLAND_EOS_HOST_CLOSED" not in log:
                            raise RuntimeError("Expected a read-only observer and a confirmed EOS lobby destruction")
                if args.scenario == "pair" and not re.search(r"ISLAND_LAN_SEARCH results=[1-9]", logs["Guest"]):
                    raise RuntimeError("Guest did not discover a LAN room")
                if args.edges and ("ISLAND_UI_EDGE room_edit=1" not in logs["Host"]
                                   or "ISLAND_UI_EDGE search_cancelled=1" not in logs["Guest"]
                                   or "ISLAND_UI_TEXT tag=Island.DecoyCount value=0 matches=1" not in logs["Host"]
                                   or "ready_cancelled=1 team_switch=1 settings_received=1 visible_decoys=0" not in logs["Guest"]):
                    raise RuntimeError("Missing confirmed room edit or guest team/readiness transition")
                if args.edges:
                    host = logs["Host"]
                    cancelled = re.search(r"ISLAND_UI_EDGE countdown_cancelled=1 staged=([1-9]\d*) remaining=0", host)
                    restarted = re.search(r"ISLAND_UI_EDGE restarted=1 routes=(\d+) facilities=(\d+) visible=(\d+)", host)
                    routes = re.findall(r"ROUTE_SCENERY_LAYOUT sites=(\d+)", host)
                    facilities = re.findall(r"NEUTRAL_LAYOUT count=(\d+) routes=", host)
                    if (not cancelled or not restarted or len(routes) < 2 or len(facilities) < 2
                            or restarted[1] != routes[-1] or restarted[2] != facilities[-1]
                            or int(restarted[3]) != int(routes[-1]) + int(facilities[-1])
                            or "ISLAND_UI_EDGE countdown_cancelled=1 staged=0 remaining=0" not in logs["Guest"]):
                        raise RuntimeError("Countdown cancellation must clear the first layout and reveal exactly the new layout")
                if args.scenario == "recovery" and ("join_failed=1 search_focus=1" not in logs["Recovery"]
                                                    or "retry_ok=1" not in logs["Recovery"]):
                    raise RuntimeError("Recovery did not restore search focus and allow a successful retry")
                if args.scenario == "walkthrough" and ("ISLAND_WINNER team=0" not in logs["Walkthrough"]
                                                       or "Island.ReturnRoom" not in logs["Walkthrough"]):
                    raise RuntimeError("The solo walkthrough did not win and return through the real result UI")
                stop_players()
                copy_screenshots()
                if args.shots and not list(directory.glob("*.png")):
                    raise RuntimeError("No rendered screenshots were saved")
                if args.scenario == "chart":
                    for view in ("ChartUnsearched", "ChartOneClue"):
                        if not (directory / f"Chart_{view}.png").is_file():
                            raise RuntimeError(f"Missing {view} chart proof")
                if args.scenario == "character":
                    for index, name in enumerate(("Pig", "Monkey", "Fox", "Calf")):
                        mesh = f"SK_Expedition{name}"
                        if f"ISLAND_UI_CHARACTER mesh={mesh} selected={index}" not in logs["Character"]:
                            raise RuntimeError(f"Character picker failed to show or mark {name}")
                        if args.shots and not (directory / f"Character_{name}.png").is_file():
                            raise RuntimeError(f"Missing {name} character portrait")
                    if "ISLAND_UI_PRESS tag=Island.Character.2 input=pointer" not in logs["Character"]:
                        raise RuntimeError("The fox was not selected through the actual pointer button")
                if args.shots and args.perspective:
                    for role in roles:
                        for view in ("FirstPerson", "ThirdPerson"):
                            if not (directory / f"{role}_{view}.png").is_file():
                                raise RuntimeError(f"Missing {view} screenshot on {role}")
                result = dict(scenario=args.scenario, edges=args.edges, simulated_weak_network=args.weak, roles=roles, events=events,
                              captures=sorted(p.name for p in directory.glob("*.png")))
                if args.edges:
                    result["countdown_retry"] = dict(cancelled_staged=int(cancelled[1]), remaining_after_cancel=0,
                                                      restarted_routes=int(restarted[1]), restarted_facilities=int(restarted[2]))
                if internet:
                    result.update(room=args.room_name, role=args.role, force_relay=args.force_relay)
                    if args.role == "directory":
                        result["checks"] = ["public_1_of_8", "hidden_during_play", "confirmed_destroy", "absent_after_close"]
                if args.chart_move:
                    result["chart_walk"] = {role: verify_chart_walk(directory, role, logs[role], args.foreground) for role in roles}
                if args.foreground:
                    result["foreground_verified"] = sorted(activated)
                if args.departure:
                    result["departure"] = {role: inspect_departure(game_output, role, logs[role]) for role in roles}
                if args.record or args.departure:
                    result["recordings"] = [encode_replay(game_output, role, directory, encoder, departure=args.departure,
                                                         source=result["departure"][role]["source"] if args.departure else None)
                                            for role in roles]
                (directory / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
                print(directory / "summary.json", flush=True)
                if args.departure and any(run["default_camera_frames"] or run["untextured_dock_frames"]
                                          for run in result["departure"].values()):
                    raise RuntimeError("Detected frames with the default ocean camera or untextured dock; see summary.json")
                break
            time.sleep(.2)
        else:
            raise TimeoutError(f"UI round trip did not complete. See {directory}")
    finally:
        stop_players()
        copy_screenshots()


if __name__ == "__main__":
    main()
