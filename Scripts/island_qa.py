#!/usr/bin/env python3
"""Exercise the archived six-player LAN prototype in independent UE processes."""

import argparse
import json
import os
import re
import signal
import subprocess
import time
from contextlib import ExitStack
from pathlib import Path

from voxel_qa import EarlyLaunchCrash, PROJECT, ROOT, p95, read_log

APP = ROOT / "Builds/TreasureIsland/Mac/ActDemo.app"
MAP = "/Game/TreasureIsland/L_TreasureIsland"
GUESTS = [f"guest{seat}" for seat in range(1, 6)]
ROLES = ["host", *GUESTS]
FAILURE = re.compile(r"(?:VOXEL_PROTOCOL_ERROR|ISLAND_QA_CHECK passed=0|ISLAND_QA_TIMEOUT|Fatal error:).*", re.I)


def wait_for(processes, condition, timeout, description, allow_exit=()):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        logs = {name: read_log(path) for name, (_, path) in processes.items()}
        for name, log in logs.items():
            if failure := FAILURE.search(log):
                raise RuntimeError(f"{name}: {failure[0]}")
        if condition(logs):
            return logs
        for name, (process, path) in processes.items():
            if name in allow_exit or process.poll() is None:
                continue
            if process.returncode == -signal.SIGSEGV and not logs[name].strip():
                raise EarlyLaunchCrash(f"{name} terminated before the first UE startup message")
            raise RuntimeError(f"{name} exited with {process.returncode}. See {path}")
        time.sleep(.25)
    raise TimeoutError(description)


def summarize(logs):
    host = logs["host"]
    stress = re.search(r"ISLAND_QA_STRESS first=(\d+) last=(\d+) p95=([.\d]+) frames=(\d+)", host)
    checks = re.findall(r"ISLAND_QA_ISOLATION epoch=(\d+) peers=6 checksum=(.+)", host)
    winners = re.findall(r"ISLAND_WINNER team=(\d+)", host)
    duplicates = set(re.findall(r"ISLAND_QA_DUPLICATE team=(\d+) chest=(\d+)", host))
    if not stress or len(checks) < 15 or len(winners) != 1 or len(duplicates) != 12:
        raise RuntimeError("Missing edit telemetry, all-crew inspections or single-winner evidence")
    first, last = int(stress[1]), int(stress[2])
    if last - first + 1 < 300:
        raise RuntimeError("Less than 300 accepted terrain edits")
    ranges = range(first, last + 1)
    per_process = {}
    timings = []
    for role in ROLES:
        samples = re.findall(r"VOXEL_SAMPLE side=\w+ seq=(\d+) action=(\d+) ms=([.\d]+)", logs[role])
        samples = [(int(seq), int(action), float(ms)) for seq, action, ms in samples if first <= int(seq) <= last]
        if [seq for seq, _, _ in samples] != list(ranges) or {action for _, action, _ in samples} != {0, 2}:
            raise RuntimeError(f"{role}: missing, duplicate or unordered edit sample")
        latency = [ms for _, _, ms in samples]
        per_process[role] = round(p95(latency), 3)
        if role != "host":
            timings.extend(latency)
    density = re.search(r"ISLAND_QA_DENSITY seq=(\d+) crc=([0-9a-f]+) chunks=(\d+)", host)
    if not density or int(density[1]) != last or int(density[3]) != 576:
        raise RuntimeError("Missing final global/chunk density evidence")
    return {
        "processes": len(ROLES), "stress_edits": len(ranges), "last_sequence": last,
        "density_crc": density[2], "chunk_count": int(density[3]),
        "six_peer_state_checks": len(checks),
        "rejected_duplicate_crew_chest_claims": len(duplicates),
        "winner": ["Red", "Blue", "Yellow"][int(winners[0])],
        "host_frame_p95_ms": float(stress[3]), "host_frame_samples": int(stress[4]),
        "edit_to_collision_p95_ms": per_process,
        "guests_combined_edit_to_collision_p95_ms": round(p95(timings), 3),
        "host_reopened_all_six_pristine": True,
    }


def summarize_party(logs, scenario):
    host = logs["host"]
    timed_out = scenario == "party-timeout"
    dropped = scenario == "party-drop"
    expected_rounds = 1 if timed_out else 2
    active_players = 5 if dropped else 6
    results = re.findall(r"PARTY_BUTTON_RESULT confirmed=(\d+) players=(\d+)", host)
    expected_results = [] if timed_out else [("6", "6"), ("0", str(active_players))]
    if results != expected_results or "PARTY_QA_ISLAND_JUMP" not in host:
        raise RuntimeError(f"Unexpected button results {results} or jump binding was not restored")
    if timed_out and "PARTY_ABORT round=1 timeout=1" not in host:
        raise RuntimeError("Missing timeout recovery evidence")
    if dropped and "ISLAND_GUEST_LEFT players=5 active=1" not in host:
        raise RuntimeError("Missing disconnect recovery evidence")
    if len(re.findall(r"PARTY_PREPARE round=", host)) != expected_rounds:
        raise RuntimeError("Concurrent opens triggered an extra party or no party")
    for role, log in logs.items():
        role_dropped = dropped and role == "guest5"
        role_skipped = timed_out and role == "guest5"
        rounds_with_pawn = 0 if timed_out else expected_rounds
        completed_rounds = expected_rounds - role_dropped
        if len(re.findall(r"PARTY_FEATURE_INSTALLED round=\d+ success=1", log)) != completed_rounds:
            raise RuntimeError(f"{role}: incomplete feature unload")
        loaded_rounds = expected_rounds - role_skipped
        if len(re.findall(r"PARTY_FEATURE_BEGIN round=\d+ installed=1", log)) != expected_rounds:
            raise RuntimeError(f"{role}: feature already mounted before a round")
        if len(re.findall(r"PARTY_FEATURE_ACTIVE round=\d+ success=1", log)) != loaded_rounds:
            raise RuntimeError(f"{role}: feature did not activate")
        ready_count = len(re.findall(r"PARTY_PAWN_READY round=", log))
        input_count = len(re.findall(r"PARTY_BUTTON_INPUT attached=1", log))
        removed_count = len(re.findall(r"PARTY_BUTTON_INPUT attached=0", log))
        minimum_removed = rounds_with_pawn - role_dropped
        if ready_count != rounds_with_pawn or input_count != rounds_with_pawn or not minimum_removed <= removed_count <= rounds_with_pawn:
            raise RuntimeError(f"{role}: duplicate or missing party pawn/input lifecycle ({ready_count}, {input_count}, {removed_count})")
    return {
        "processes": len(logs), "scenario": scenario, "attempts": expected_rounds,
        "party_results": [{"confirmed": int(count), "players": int(players)} for count, players in results],
        "restored_island_jump": True, "restored_pawns_terrain_and_private_clues": True,
        "unloaded_on_all_remaining_processes": True,
    }


def run(args, ip, mode, directory):
    directory.mkdir(parents=True, exist_ok=True)
    summary = directory / "summary.json"
    summary.unlink(missing_ok=True)
    options = ["-PktLag=40", "-PktLoss=2"] if mode == "emulated" else []
    if args.editor:
        engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
        base = [str(engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"), str(PROJECT), "-game"]
    else:
        base = [str(args.app / "Contents/MacOS/ActDemo")]
    shared = ["-IslandQA", "-unattended", "-nosound", "-NoSplash", "-NoTrace", "-stdout", "-FullStdOutLogOutput",
              "-ExecCmds=t.MaxFPS 60", "-windowed", "-ResX=1280", "-ResY=720"]
    if args.scenario != "hunt":
        shared.append("-IslandPartyQA")
    if args.scenario == "party-timeout":
        shared.append("-IslandPartyTimeout")
    address = f"{ip}:{args.port}"
    processes = {}
    with ExitStack() as stack:
        def launch(role, url):
            path = directory / (role + ".log")
            stream = stack.enter_context(path.open("w"))
            render = [] if role == "host" and not args.headless_host else ["-nullrhi"]
            if role == "host" and args.screenshot:
                render.append("-IslandScreenshot")
            scenario_flags = []
            if role == "host" and args.scenario == "hunt":
                scenario_flags.append("-ini:Game:[/Script/ActDemo.IslandGameMode]:DecoyChestCount=0")
            if role == "guest5" and args.scenario == "party-timeout":
                scenario_flags.append("-IslandPartySkipReady")
            process = subprocess.Popen(base + [url, f"-port={args.port}"] + shared + render + options + scenario_flags,
                                       stdout=stream, stderr=subprocess.STDOUT, start_new_session=True, cwd=ROOT)
            processes[role] = process, path

        try:
            launch("host", args.map + "?listen")
            wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs["host"], 120, "Host map did not initialize")
            for guest in GUESTS:
                launch(guest, address)
                wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs[guest], 100, f"{guest} did not join and load terrain")
            print(f"{mode}: six independent processes joined {address}", flush=True)
            allow_exit = ()
            if args.scenario == "party-drop":
                wait_for(processes, lambda logs: "PARTY_START round=2" in logs["host"] and "PARTY_PAWN_READY round=2" in logs["guest5"],
                         180, "The second party did not start")
                os.killpg(processes["guest5"][0].pid, signal.SIGTERM)
                allow_exit = ("guest5",)
            done_marker = "ISLAND_QA_DONE peers=6 reopened=1" if args.scenario == "hunt" else "PARTY_QA_DONE"
            logs = wait_for(processes, lambda logs: done_marker in logs["host"], 300,
                            "Six-player scenario did not complete", allow_exit)
            result = summarize(logs) if args.scenario == "hunt" else summarize_party(logs, args.scenario)
            if options and not all("PktLag set to 40" in log and "PktLoss set to 2" in log for log in logs.values()):
                raise RuntimeError("Missing runtime packet simulation settings")
            result.update(mode=mode, ip=ip, host_render="NullRHI" if args.headless_host else "Metal", guest_render="NullRHI",
                          configured_rtt_ms=80 if options else 0, configured_loss_percent=2 if options else 0)
            if args.scenario == "hunt" and not args.editor and mode == "normal":
                launch("seventh", address)
                wait_for({"seventh": processes["seventh"]}, lambda logs: "voxel room is full or already started" in logs["seventh"],
                         60, "A seventh connection was not rejected")
                result["seventh_connection_rejected"] = True
                os.killpg(processes["guest5"][0].pid, signal.SIGTERM)
                wait_for({"host": processes["host"]}, lambda logs: "ISLAND_GUEST_LEFT players=5 active=1" in logs["host"],
                         30, "The match did not stay active when a guest left")
                result["guest_exit_keeps_match_active"] = True
                os.killpg(processes["host"][0].pid, signal.SIGTERM)
                wait_for({guest: processes[guest] for guest in GUESTS[:-1]},
                         lambda logs: all("Host closed the connection" in log for log in logs.values()), 30,
                         "Guests did not observe the host closing the session")
                result["host_exit_ends_session"] = True
            if args.scenario == "hunt":
                result["performance_targets_met"] = result["host_frame_p95_ms"] <= 33.3 and max(result["edit_to_collision_p95_ms"].values()) <= 150
            summary.write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, ensure_ascii=False), flush=True)
            return result.get("performance_targets_met", True)
        finally:
            for process, _ in reversed(list(processes.values())):
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGTERM)
            for process, _ in processes.values():
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", help="LAN IP assigned to this Mac, default is en0")
    parser.add_argument("--port", type=int, default=7792)
    parser.add_argument("--scenario", choices=("hunt", "party", "party-drop", "party-timeout"), default="hunt")
    parser.add_argument("--mode", choices=("normal", "emulated", "both"), default="both")
    parser.add_argument("--editor", action="store_true", help="Use editor game processes during iteration")
    parser.add_argument("--headless-host", action="store_true", help="Disable Metal for iteration only")
    parser.add_argument("--screenshot", action="store_true", help="Capture the completed chart or the party prompt on the host")
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--map", default=MAP, help="Level package to host, including authored test levels")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/IslandQA")
    args = parser.parse_args()
    ip = args.ip or subprocess.check_output(["/usr/sbin/ipconfig", "getifaddr", "en0"], text=True).strip()
    modes = ("normal", "emulated") if args.mode == "both" else (args.mode,)
    missed_targets = []
    for mode in modes:
        directory = args.output.resolve() / mode if args.scenario == "hunt" else args.output.resolve() / args.scenario / mode
        try:
            passed = run(args, ip, mode, directory)
        except EarlyLaunchCrash as error:
            print(f"{mode}: {error}. Retrying once.", flush=True)
            if args.editor:
                raise
            subprocess.run(["/usr/bin/codesign", "--verify", "--deep", str(args.app)], check=True)
            passed = run(args, ip, mode, directory)
        if not passed:
            missed_targets.append(mode)
    if missed_targets:
        raise SystemExit("Performance targets missed in " + ", ".join(missed_targets))


if __name__ == "__main__":
    main()
