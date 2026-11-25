#!/usr/bin/env python3
"""Run the voxel acceptance scenario with two independent game processes."""

import argparse
import json
import math
import os
import re
import signal
import subprocess
import time
from contextlib import ExitStack
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "ActDemo" / "ActDemo.uproject"
APP = ROOT / "Builds/TreasureIsland/Mac/远征岛.app"
MAP = "/Game/VoxelPlayground/L_VoxelPlayground"


class EarlyLaunchCrash(RuntimeError):
    """The executable terminated before UE emitted its first startup log."""


def read_log(path):
    return path.read_text(errors="replace")


def wait_for(processes, condition, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        logs = {name: read_log(path) for name, (_, path) in processes.items()}
        for name, log in logs.items():
            failure = re.search(r"VOXEL_PROTOCOL_ERROR.*|VOXEL_QA_CHECK.*passed=0.*|VOXEL_QA_TIMEOUT.*", log)
            if failure:
                raise RuntimeError(f"{name}: {failure.group(0)}")
        if condition(logs):
            return logs
        for name, (process, path) in processes.items():
            if process.poll() is not None:
                if process.returncode == -signal.SIGSEGV and not logs[name].strip():
                    raise EarlyLaunchCrash(f"{name} terminated before the first UE startup message")
                raise RuntimeError(f"{name} exited with {process.returncode}. See {path}")
        time.sleep(.25)
    raise TimeoutError(description)


def p95(samples):
    return sorted(samples)[math.ceil(len(samples) * .95) - 1]


def summarize(logs):
    status = {}
    metrics = {}
    for role, log in logs.items():
        record = re.search(fr"VOXEL_STATUS side={role} seq=(\d+) crc=([0-9a-f]+) chunks=([0-9a-f,]+)", log)
        if record is None or int(record[1]) != 104 or len(record[3].split(",")) != 24:
            raise RuntimeError(f"Invalid final voxel status on {role}")
        status[role] = (record[2], record[3].split(","))
        edits = re.findall(fr"VOXEL_SAMPLE side={role} seq=(\d+) action=(\d+) ms=([.\d]+)", log)
        stress = [(int(seq), int(action), float(ms)) for seq, action, ms in edits if int(seq) > 4]
        if [seq for seq, _, _ in stress] != list(range(5, 105)) or [action for _, action, _ in stress] != [2, 0] * 50:
            raise RuntimeError(f"Missing, misordered or duplicate brush commands on {role}")
        metrics[role + "_edit_to_collision_p95_ms"] = round(p95([ms for _, _, ms in stress]), 3)
    if status["host"] != status["client"]:
        raise RuntimeError("Global or chunk density checksums diverged")
    host_frame = re.search(r"VOXEL_FRAME_P95 ms=([.\d]+) count=(\d+)", logs["host"])
    if host_frame is None:
        raise RuntimeError("Host frame-time telemetry is missing")
    return {"gameplay_edits": 4, "stress_edits": 100, "sequence": 104, "density_crc": status["host"][0],
            "chunk_crcs": status["host"][1], "host_frame_p95_ms": float(host_frame[1]),
            "host_frame_samples": int(host_frame[2]), **metrics}


def run(args, ip, mode, directory):
    directory.mkdir(parents=True, exist_ok=True)
    summary = directory / "summary.json"
    summary.unlink(missing_ok=True)
    net_options = ["-PktLag=40", "-PktLoss=2"] if mode == "emulated" else []
    if args.editor:
        binary = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8")) / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
        base = [str(binary), str(PROJECT), "-game"]
    else:
        base = [str(args.app / "Contents/MacOS/ActDemo")]
    shared = ["-VoxelQA", "-IslandLAN", "-unattended", "-nosound", "-NoSplash", "-NoTrace", "-stdout", "-FullStdOutLogOutput",
              "-ExecCmds=t.MaxFPS 60", "-windowed", "-ResX=960", "-ResY=540"]
    if not args.render:
        shared.append("-nullrhi")
    processes = {}
    with ExitStack() as stack:
        def launch(role, address):
            log = directory / (role + ".log")
            stream = stack.enter_context(log.open("w"))
            process = subprocess.Popen(base + [address, f"-port={args.port}"] + shared + net_options,
                                       stdout=stream, stderr=subprocess.STDOUT, start_new_session=True, cwd=ROOT)
            processes[role] = process, log

        try:
            launch("host", MAP + "?listen?bIsLanMatch")
            wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs["host"], 100, "Host map never initialized")
            launch("client", f"{ip}:{args.port}")
            print(f"{mode}: host and guest started on port {args.port}", flush=True)
            logs = wait_for(processes, lambda logs: all(f"VOXEL_QA_DONE side={side} edits=100" in logs[side]
                                                       for side in ("host", "client")), 185, "Multiplayer probe timed out")
            result = summarize(logs)
            if mode == "emulated" and not all("PktLag set to 40" in log and "PktLoss set to 2" in log for log in logs.values()):
                raise RuntimeError("No runtime indication of the requested packet simulation")
            result.update(render="Metal" if args.render else "NullRHI", mode=mode, independent_processes=True,
                          configured_rtt_ms=80 if mode == "emulated" else 0,
                          outgoing_loss_percent=2 if mode == "emulated" else 0)
            if mode == "normal" and not args.editor:
                launch("third", f"{ip}:{args.port}")
                wait_for({"third": processes["third"]}, lambda current: "voxel room is full or already started" in current["third"],
                         60, "A third connection was not rejected")
                result["third_connection_rejected"] = True
                processes["host"][0].terminate()
                wait_for({"client": processes["client"]}, lambda current: "Host closed the connection" in current["client"],
                         25, "Guest did not observe the host closing the session")
                result["host_exit_ends_session"] = True
            summary.write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, ensure_ascii=False), flush=True)
        finally:
            for process, _ in reversed(list(processes.values())):
                if process.poll() is None:
                    process.terminate()
            for process, _ in processes.values():
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", help="LAN address assigned to this Mac (auto-detect en0 by default)")
    parser.add_argument("--port", type=int, default=7791)
    parser.add_argument("--mode", choices=("normal", "emulated", "both"), default="both")
    parser.add_argument("--editor", action="store_true", help="Use editor game processes during iteration")
    parser.add_argument("--render", action="store_true", help="Run the same scenario with Metal rendering")
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/VoxelQA")
    args = parser.parse_args()
    ip = args.ip or subprocess.check_output(["/usr/sbin/ipconfig", "getifaddr", "en0"], text=True).strip()
    for mode in ("normal", "emulated") if args.mode == "both" else (args.mode,):
        directory = args.output.resolve() / mode
        try:
            run(args, ip, mode, directory)
        except EarlyLaunchCrash as error:
            print(f"{mode}: {error}. Retrying once.", flush=True)
            for role in ("host", "client", "third"):
                log = directory / f"{role}.log"
                if log.exists():
                    log.rename(directory / f"startup-attempt-{role}.log")
            run(args, ip, mode, directory)
        args.port += 1


if __name__ == "__main__":
    main()
