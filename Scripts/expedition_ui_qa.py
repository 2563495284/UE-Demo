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


def encode_replay(directory, role, destination, encoder=None):
    binary, codec_options = encoder or recording_encoder()
    frames = sorted((directory / (role + "_frames")).glob("*.png"))
    if len(frames) < 5:
        raise RuntimeError("Not enough viewport frames for " + role)
    lines = ["ffconcat version 1.0"]
    for first, second in zip(frames, frames[1:]):
        lines.extend([f"file '{first.name}'", f"duration {(int(second.stem) - int(first.stem)) / 1000:.3f}"])
    lines.append(f"file '{frames[-1].name}'")
    manifest = frames[0].parent / "timing.ffconcat"
    manifest.write_text("\n".join(lines) + "\n")
    movie = destination / (role + ".mp4")
    subprocess.run([binary, "-nostdin", "-y", "-hide_banner", "-loglevel", "error", "-f", "concat", "-safe", "1",
                    "-i", str(manifest), "-vf", "fps=24,format=yuv420p", *codec_options,
                    "-movflags", "+faststart", str(movie)], check=True)
    return movie.name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", action="store_true")
    parser.add_argument("--app", type=Path, default=ROOT / "Builds/TreasureIsland/Mac/ActDemo.app")
    parser.add_argument("--scenario", choices=("solo", "pair", "recovery", "walkthrough"), default="pair")
    parser.add_argument("--weak", action="store_true")
    parser.add_argument("--foreground", action="store_true", help="run one interactive Mac game and verify it owns the foreground")
    parser.add_argument("--edges", action="store_true", help="exercise settings, team changes and withdrawn readiness in the pair run")
    parser.add_argument("--shots", action="store_true")
    parser.add_argument("--record", action="store_true", help="record the game viewport and encode a replay using ffmpeg")
    parser.add_argument("--ffmpeg", help="path to a working ffmpeg executable for --record")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/ExpeditionUI")
    parser.add_argument("--port", type=int, default=7817)
    args = parser.parse_args()
    if args.foreground and (sys.platform != "darwin" or args.scenario == "pair"):
        parser.error("--foreground requires a single Mac game window")
    encoder = recording_encoder(args.ffmpeg) if args.record else None
    # One directory per run keeps earlier captures and exposes the exact processes/logs under test.
    directory = args.output.resolve() / (args.scenario + ("-weak-" if args.weak else "-normal-") + time.strftime("%Y%m%d-%H%M%S"))
    directory.mkdir(parents=True)
    engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
    command = ([str(engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"), str(PROJECT), "-game"]
               if args.editor else [str(args.app / "Contents/MacOS/ActDemo")])
    roles = {"solo": ["Solo"], "pair": ["Host", "Guest"], "recovery": ["Recovery"], "walkthrough": ["Walkthrough"]}[args.scenario]
    game_output = directory
    if not args.editor:
        with (args.app / "Contents/Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream)["CFBundleIdentifier"]
        # A sandboxed Mac application exports its own viewport to its container.
        game_output = Path.home() / "Library/Containers" / bundle / "Data/tmp/ExpeditionUI" / directory.name
    shared = ["-windowed", "-ResX=1280", "-ResY=720", "-NoSplash", "-nosound", "-NoTrace",
              "-unattended", "-NoScreenMessages", "-ExecCmds=t.MaxFPS 60", "-AlwaysFocus",
              "-stdout", "-FullStdOutLogOutput", f"-IslandUISeats={len(roles)}", f"-IslandUIOutput={game_output}"]
    if args.foreground:
        shared.remove("-unattended")
    if args.scenario == "walkthrough":
        shared += ["-ExpeditionWalkQA", "-ExpeditionTreasureWalkQA"]
    if args.shots:
        shared.append("-IslandUIShots")
    if args.edges:
        if args.scenario != "pair":
            parser.error("--edges requires a pair of players")
        shared.append("-IslandUIEdges")
    if args.record:
        if args.shots:
            parser.error("Use separate runs for recording and named screenshots.")
        shared.append("-IslandUIRecord")
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
        def start(role):
            offset = roles.index(role)
            log = directory / (role + ".log")
            with log.open("w") as stream:
                process = subprocess.Popen(command + shared + [f"-IslandUIProbe={role}", f"-port={args.port + offset}",
                    f"-WinX={32 + 175 * offset}", f"-WinY={60 + 90 * offset}"],
                    stdout=stream, stderr=subprocess.STDOUT, cwd=ROOT, start_new_session=True)
            processes[role] = process, log
            print(f"{role} pid={process.pid} log={log}", flush=True)

        start(roles[0])
        deadline = time.monotonic() + (1500 if args.scenario == "walkthrough" else 360 if args.editor else 210)
        while time.monotonic() < deadline:
            logs = {role: read_log(log) if log.exists() else "" for role, (_, log) in processes.items()}
            if args.foreground:
                role = roles[0]
                for phase, signal_text in (("home", "Game Engine Initialized"), ("play", "ISLAND_UI_WALK_CONTROL" if role == "Walkthrough" else "ISLAND_UI_CHART")):
                    if phase not in activated and signal_text in logs[role] and activate_game_window(processes[role][0].pid):
                        activated.add(phase)
                        print(f"ISLAND_UI_NATIVE_FOREGROUND role={role} phase={phase} verified=1", flush=True)
            if "Guest" in roles and "Guest" not in processes and "ISLAND_LAN_ARRIVED host=1" in logs["Host"]:
                start("Guest")
                continue
            for role, (process, log) in processes.items():
                failure = re.search(r"(?:ISLAND_UI_FAIL|Fatal error:|Assertion failed:).*", logs[role])
                if failure or (process.poll() is not None and "ISLAND_UI_DONE" not in logs[role]):
                    raise RuntimeError(f"{role} {failure[0] if failure else process.returncode}. See {log}")
            if len(logs) == len(roles) and all("ISLAND_UI_DONE" in log for log in logs.values()):
                if args.foreground and not activated:
                    raise RuntimeError("The operating system did not confirm the test game owned the foreground")
                if args.foreground and roles == ["Solo"] and (
                    "play" not in activated or "ISLAND_UI_PRESS tag=Island.Resume input=pointer" not in logs["Solo"]
                ):
                    raise RuntimeError("The active game window did not resume through the pointer button")
                events = {}
                for role, log in logs.items():
                    if args.weak and ("PktLag set to 40" not in log or "PktLoss set to 2" not in log):
                        raise RuntimeError("Packet simulation was not active on " + role)
                    if "ISLAND_UI_PRESS" not in log or (role not in ("Recovery", "Walkthrough") and "ISLAND_UI_CHART" not in log):
                        raise RuntimeError("Actual UI and chart input evidence missing on " + role)
                    events[role] = re.findall(r"ISLAND_(?:UI_(?:PRESS|CHART|DONE)|LAN_(?:SEARCH|ARRIVED))[^\r\n]*", log)
                if len(roles) == 2 and not re.search(r"ISLAND_LAN_SEARCH results=[1-9]", logs["Guest"]):
                    raise RuntimeError("Guest did not discover a LAN room")
                if args.edges and ("ISLAND_UI_EDGE room_edit=1" not in logs["Host"]
                                   or "ISLAND_UI_EDGE search_cancelled=1" not in logs["Guest"]
                                   or "ready_cancelled=1 team_switch=1 settings_received=1" not in logs["Guest"]):
                    raise RuntimeError("Missing confirmed room edit or guest team/readiness transition")
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
                result = dict(scenario=args.scenario, edges=args.edges, simulated_weak_network=args.weak, roles=roles, events=events,
                              captures=sorted(p.name for p in directory.glob("*.png")))
                if args.foreground:
                    result["foreground_verified"] = sorted(activated)
                if args.record:
                    result["recordings"] = [encode_replay(game_output, role, directory, encoder) for role in roles]
                (directory / "summary.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
                print(directory / "summary.json", flush=True)
                break
            time.sleep(.2)
        else:
            raise TimeoutError(f"UI round trip did not complete. See {directory}")
    finally:
        stop_players()
        copy_screenshots()


if __name__ == "__main__":
    main()
