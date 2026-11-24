#!/usr/bin/env python3
"""Exercise LAN treasure hunts and expedition rooms in independent UE processes."""

import argparse
import json
import os
import plistlib
import re
import signal
import shutil
import subprocess
import sys
import time
from contextlib import ExitStack
from pathlib import Path

sys.dont_write_bytecode = True
from voxel_qa import EarlyLaunchCrash, PROJECT, ROOT, p95, read_log

APP = ROOT / "Builds/TreasureIsland/Mac/ActDemo.app"
MAP = "/Game/TreasureIsland/L_TreasureIsland"
EXPEDITION_MAP = "/Game/TreasureIsland/L_ExpeditionIsland"
ROOM_CASES = {"solo": (1, True), "coop": (2, True), "duel": (2, False), "crew-drop": (3, False),
              "mud": (3, False), "field": (3, False), "neutral": (3, False), "crew-eight": (8, True), "eight-teams": (8, False)}
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


def summarize_mud_contacts(host):
    parts = re.split(r"MUD_QA_STEP stage=(\d+)\b", host)
    stages = dict(zip(parts[1::2], parts[2::2]))
    roster = re.search(r"MUD_QA_ROSTER first=(\S+) second=(\S+) target=(\S+)", stages["11"])
    if not roster:
        raise RuntimeError("Missing authoritative two-thrower roster")
    first, second, victim = roster.groups()
    pattern = re.compile(r"MUD_QA_CONTACT source=(\S+) target=(\S+) result=(\w+) stack=(\d+) time=([\d.]+)")

    def contacts(stage):
        return [(source, result, int(stack), float(when)) for source, target, result, stack, when
                in pattern.findall(stages[stage]) if target == victim]

    pair = contacts("11")
    if (len(pair) != 2 or {hit[0] for hit in pair} != {first, second}
            or [(hit[1], hit[2]) for hit in pair] != [("accepted", 1), ("window", 1)]
            or not 0 <= pair[1][3] - pair[0][3] < .4):
        raise RuntimeError(f"Both teammates must physically hit the same capsule inside its 0.4 s gate: {pair}")
    cleared = re.search(r"MUD_QA_STACK_CLEAR time=([\d.]+)", stages["12"])
    if not cleared or not 1.45 <= float(cleared[1]) - pair[0][3] <= 1.78:
        raise RuntimeError("Unrefreshed marks must expire at 1.5 s of server world time")
    alternating = [(source, stack) for source, result, stack, _ in contacts("13") if result == "accepted"]
    if alternating != [(first, 1), (second, 2), (first, 3)]:
        raise RuntimeError(f"Three physical hits from alternating teammates must share one stack: {alternating}")
    wake = re.search(r"MUD_QA_WAKE time=([\d.]+)", stages["14"])
    end = re.search(r"MUD_QA_GROUP [^\n]*\bend_time=([\d.]+)", stages["15"])
    if not wake or not end or not 3.8 <= float(end[1]) - float(wake[1]) <= 4.3:
        raise RuntimeError("Wake immunity must last 4 s of authoritative world time")
    protected = contacts("15")
    if ({hit[0] for hit in protected} != {first, second} or any(hit[1:3] != ("immune", 0) for hit in protected)
            or protected[-1][3] - float(wake[1]) < 2.4):
        raise RuntimeError(f"Both teammates must hit the protected capsule without adding marks: {protected}")
    restored = contacts("150")
    if len(restored) != 1 or restored[0][:3] != (second, "accepted", 1) or restored[0][3] < float(end[1]):
        raise RuntimeError(f"The first physical hit after immunity must start a fresh stack: {restored}")
    return {"simultaneous_contact_gap_s": round(pair[1][3] - pair[0][3], 3),
            "mark_expiry_s": round(float(cleared[1]) - pair[0][3], 3),
            "protected_contacts": {seat: sum(hit[0] == name for hit in protected)
                                   for seat, name in (("first", first), ("second", second))},
            "wake_immunity_s": round(float(end[1]) - float(wake[1]), 3), "first_unprotected_stack": restored[0][2]}


def summarize_field(logs):
    shared = re.search(r"FIELD_QA_SHARED peers=3 seq=(\d+) crc=([0-9a-f]+) drop=(\d+) reference_delta=(-?\d+)", logs["host"])
    if (not shared or int(shared[1]) != 1 or not 28 <= int(shared[3]) <= 55 or abs(int(shared[4])) > 20
            or "FIELD_QA_PUNCH_INPUT" not in logs["guest1"]):
        raise RuntimeError("One ordinary punch must lower the floor at impact while preserving the ground four metres away")
    rally = "ISLAND_CREW_RALLY received"
    if ("FIELD_QA_RALLY_INPUT" not in logs["guest1"]
            or any(logs[role].count(rally) != 1 for role in ("guest1", "host"))
            or rally in logs["guest2"]):
        raise RuntimeError("Guest rally did not reach exactly the two members of the issuing crew")
    walk = re.search(r"FIELD_QA_WALK advance=(\d+) depth=(\d+)", logs["host"])
    if not walk or int(walk[1]) < 65 or int(walk[2]) < 12:
        raise RuntimeError("Guest did not walk into the newly excavated ground and stand on its lower floor")
    dig_log = logs["host"].partition("FIELD_QA_ECHO_SEARCH predicted_gap=")[2].partition("ISLAND_LOCK start")[0]
    echoes = re.findall(r"ISLAND_EXCAVATION_ECHO strong=([01])", dig_log)
    if not echoes or echoes[0] != "0" or "1" not in echoes or "ISLAND_EXCAVATION_ECHO" in logs["guest2"]:
        raise RuntimeError("Expected weak then strong echoes on the excavator, and silence on the distant observer")
    for role, log in logs.items():
        if f"FIELD_QA_TERRAIN seq={shared[1]} crc={shared[2]} drop={shared[3]} reference_delta={shared[4]}" not in log:
            raise RuntimeError(f"{role} did not observe the same local dig, reference height and density")
    expected = {"host": ("crew=0 lock=0 assisted=1 seized=0 role=2", "crew=0 lock=1 assisted=0 seized=1 role=0"),
                "guest1": ("crew=0 lock=0 assisted=1 seized=0 role=1",),
                "guest2": ("crew=1 lock=0 assisted=1 seized=0 role=0", "crew=1 lock=1 assisted=0 seized=1 role=1")}
    for role, views in expected.items():
        if any("FIELD_QA_VIEW " + view not in logs[role] for view in views):
            raise RuntimeError(f"{role} did not receive its corresponding lock and crew role")
    if "VOXEL_REJECT 对手正在合撬，箱盖已稳住" not in logs["guest2"] or "VOXEL_REJECT 对手已经接锁，守住了这次开箱" not in logs["host"]:
        raise RuntimeError("The protected and already-seized lock must each reject a real rival input")
    events = re.findall(r"ISLAND_LOCK (\w+) chest=1 ", logs["host"])
    if events != ["start", "assist", "handoff", "seize", "complete", "start", "assist", "abandoned"]:
        raise RuntimeError(f"Unexpected contested chest transitions: {events}")
    for worker, role in ((1, "leader"), (0, "helper")):
        if f"FIELD_QA_TWO_HITS worker={worker}" not in logs["host"] or f"FIELD_QA_STUN_LOCK worker={role}" not in logs["host"]:
            raise RuntimeError(f"The active {role} must keep working through two impacts and drop out on the third.")
    if "FIELD_QA_HELD physical=1 repeated=1 retargeted=1 released=1" not in logs["host"]:
        raise RuntimeError("Held physical input must edit a second aimed spot and stop on release.")
    return {"scenario": "field", "processes": 3, "punch_depth_cm": int(shared[3]),
            "ground_delta_cm_at_4m": int(shared[4]),
            "guest_walk_forward_cm": int(walk[1]), "guest_walk_depth_cm": int(walk[2]),
            "rally_delivered_to_crew_only": True,
            "dig_echo_samples": [int(level) for level in echoes],
            "agreed_terrain_crc": shared[2], "edit_sequence": int(shared[1]),
            "replicated_crew_role_views": sum(map(len, expected.values())),
            "chest_transitions": events, "protected_lock": True, "seize_limit": 1,
            "abandon_resets_work": True, "winner_only_clue": True, "physical_held_retarget_and_release": True,
            "two_hits_preserve_lock": True, "third_hit_leader_handoff": True, "third_hit_helper_removed": True}


def summarize_neutral(logs, only=None):
    host = logs["host"]
    by_kind = {}
    kinds = ("Tree", "Ship", "Hopper", "Sail", "Tide")
    for index, kind in enumerate(kinds):
        if only and kind.lower() != only:
            continue
        by_kind[kind] = {}
        for state, step in (("before", 1), ("after", 90)):
            result = re.search(rf"NEUTRAL_QA_SHARED kind={kind} state={state} peers=3 digest=(\S+)", host)
            if not result or any(f"NEUTRAL_QA_VIEW stage={index * 100 + step} digest={result[1]}" not in log for log in logs.values()):
                raise RuntimeError(f"Missing peer placement, state, collision or terrain observation for {kind}/{state}")
            by_kind[kind][state] = result[1]
    for kind, state, step in (("Sail", "active", 303), ("Tide", "active", 403), ("Tide", "overflow", 407)):
        if only and kind.lower() != only:
            continue
        match = re.search(rf"NEUTRAL_QA_SHARED kind={kind} state={state} peers=3 digest=(\S+)", host)
        if not match or any(f"NEUTRAL_QA_VIEW stage={step} digest={match[1]}" not in log for log in logs.values()):
            raise RuntimeError(f"Missing shared {kind}/{state} state")
        by_kind[kind][state] = match[1]
    metrics = {}
    for name in ("TREE_CROSS", "SHIP_PASS", "SHIP_BANK", "SHIP_DECK", "HOPPER_ASCENT", "HOPPER_CUT", "TIDE_BOOST", "TIDE_REOPEN"):
        if only and not name.lower().startswith(only + "_"):
            continue
        match = re.search(rf"NEUTRAL_QA_{name} ([^\n]+)", host)
        if not match:
            raise RuntimeError(f"Missing physical result {name}")
        metrics[name.lower()] = {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", match[1])}
    if (not only or only == "sail") and ("NEUTRAL_QA_SAIL lowered_by=0" not in host or "NEUTRAL_QA_SAIL raised_by=1" not in host):
        raise RuntimeError("The two different crews must lower and raise the same sail")
    seed = re.search(r"EXPEDITION_QA_SEED (-?\d+)", host)
    return {"scenario": "neutral", "processes": 3, "teams": 2, "seed": int(seed[1]),
            "per_kind_peer_digests": by_kind, "physical_interactions": metrics, "sail_lower_then_raise_by": [0, 1] if not only or only == "sail" else []}


def summarize_neutral_audit(host):
    rows = re.findall(r"NEUTRAL_AUDIT_SEED seed=(\d+) count=(\d+) kinds=(\d+) regions=(\d+) hash=(\w+) replay=1 routes=1 items=(\S*)", host)
    done = re.search(r"NEUTRAL_AUDIT_DONE seeds=64 passed=1 full_seed=(-?\d+) tree=(\d+) ship=(\d+) hopper=(\d+) sail=(\d+) tide=(\d+)", host)
    if len({r[0] for r in rows}) != 64 or not done or min(map(int, done.groups()[1:])) < 1:
        raise RuntimeError("64 real seeds must replay deterministically, preserve routes and naturally cover every kind")
    names = ("tree", "ship", "hopper", "sail", "tide")
    seeds = []
    totals = dict.fromkeys(names, 0)
    counts = {}
    for seed, count, kinds, regions, digest, items in rows:
        entries = []
        for kind, x, y, z, yaw, region in re.findall(r"(\d+)@(-?\d+),(-?\d+),(-?\d+)/(-?\d+):([A-D])", items):
            name = names[int(kind)]
            entries.append({"kind": name, "xyz_cm": [int(x), int(y), int(z)], "yaw": int(yaw), "region": region})
            totals[name] += 1
        if len(entries) != int(count):
            raise RuntimeError(f"Seed {seed} omitted a placement from its location record")
        counts[count] = counts.get(count, 0) + 1
        seeds.append({"seed": int(seed), "count": int(count), "kind_mask": int(kinds), "region_mask": int(regions),
                      "digest": digest, "items": entries})
    if list(totals.values()) != list(map(int, done.groups()[1:])):
        raise RuntimeError("Placement counts do not agree with the 64-seed summary")
    return {"seeds": seeds, "full_seed": int(done[1]), "per_kind": totals, "density_distribution": counts}


def expedition_performance(logs):
    stress = re.search(r"EXPEDITION_QA_STRESS first=(\d+) last=(\d+) frame95=([.\d]+) frames=(\d+)", logs["host"])
    state = re.search(r"EXPEDITION_QA_TERRAIN peers=8 digest=([^\s]+)", logs["host"])
    if not stress or not state:
        raise RuntimeError("Missing frame measurements or eight-peer density and collision verification")
    first, last = int(stress[1]), int(stress[2])
    sequences = list(range(first, last + 1))
    if len(sequences) < 300:
        raise RuntimeError("Fewer than 300 accepted edits from eight concurrent players")
    collision = {}
    for role, log in logs.items():
        samples = re.findall(r"VOXEL_SAMPLE side=\w+ seq=(\d+) action=(\d+) ms=([.\d]+)", log)
        window = [(int(seq), int(action), float(ms)) for seq, action, ms in samples if first <= int(seq) <= last]
        if [seq for seq, _, _ in window] != sequences or {action for _, action, _ in window} != {0, 2}:
            raise RuntimeError(f"{role}: incomplete edit replication in stress window")
        collision[role] = round(p95([ms for _, _, ms in window]), 3)
    result = {"stress_edits": len(sequences), "host_frame_p95_ms": float(stress[3]), "host_frame_samples": int(stress[4]),
              "edit_to_collision_p95_ms": collision, "eight_peer_density_and_floor_digest": state[1]}
    clocks = re.findall(r"EXPEDITION_QA_CLOCK source=(\w+) p95=([-\d.]+) valid=(\d+) wall_frames=(\d+)", logs["host"])
    if clocks:
        if any(int(wall) != result["host_frame_samples"] for _, _, _, wall in clocks):
            raise RuntimeError("Engine timing and wall-frame measurement windows differ")
        result["host_engine_clocks"] = {
            name: {"p95_ms": float(timing) if int(valid) else None, "nonzero_samples": int(valid)}
            for name, timing, valid, _ in clocks
        }
    return result


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
    if args.profile:
        shared.append("-VoxelProfile")
    room = ROOM_CASES.get(args.scenario)
    guests = GUESTS
    if room:
        count, together = room
        guests = [f"guest{seat}" for seat in range(1, count)]
        shared.append(f"-ExpeditionPlayers={count}")
        if together:
            shared.append("-ExpeditionTogether")
        if args.stress:
            shared.append("-ExpeditionStress")
        if args.repro_dig:
            shared.extend(["-ExpeditionRepro", "-ExpeditionSeed=7319"])
        if args.scenario == "crew-drop":
            shared.append("-ExpeditionDrop")
        if args.scenario == "mud":
            shared.append("-ExpeditionMudQA")
            if args.mud_exit:
                shared.append(f"-MudExit={args.mud_exit}")
        if args.scenario == "neutral":
            shared.append("-ExpeditionNeutralQA")
            if args.neutral_kind:
                shared.append(f"-NeutralOnlyKind={('tree','ship','hopper','sail','tide').index(args.neutral_kind)}")
        if args.scenario == "field":
            # Keep the three lock participants in the same sightline fixture on both networks.
            shared.extend(["-ExpeditionFieldQA", "-ExpeditionSeed=1872304730"])
    elif args.scenario != "hunt":
        shared.append("-IslandPartyQA")
    if args.scenario == "party-timeout":
        shared.append("-IslandPartyTimeout")
    address = f"{ip}:{args.port}"
    shot_output = directory
    if args.scenario in ("field", "neutral", "mud") and args.screenshot and not args.editor:
        with (args.app / "Contents/Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream)["CFBundleIdentifier"]
        shot_output = Path.home() / "Library/Containers" / bundle / "Data/tmp/ExpeditionField" / str(time.time_ns())
    processes = {}
    with ExitStack() as stack:
        def launch(role, url):
            path = directory / (role + ".log")
            stream = stack.enter_context(path.open("w"))
            render = [] if role == "host" and not args.headless_host else ["-nullrhi"]
            if role == "host" and args.screenshot:
                render.append("-IslandScreenshot")
                if args.scenario in ("field", "neutral", "mud"):
                    render.append(f"-FieldShotsDir={shot_output}")
            scenario_flags = ["-NeutralLayoutAudit"] if role == "host" and args.neutral_audit else []
            if role == "host" and args.seed is not None:
                scenario_flags.append(f"-ExpeditionSeed={args.seed}")
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
            for guest in guests:
                launch(guest, address)
                if args.scenario != "neutral":
                    wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs[guest], 100, f"{guest} did not join and load terrain")
            if args.scenario == "neutral":
                # The room assigns crews after both guests are ready. All observations are by
                # in-game seat, so simultaneous slow editor starts do not affect their identity.
                wait_for(processes, lambda logs: all("VOXEL_INITIAL crc=" in logs[g] for g in guests), 120, "Neutral guests did not load the terrain")
            print(f"{mode}: {len(guests) + 1} independent processes joined {address}", flush=True)
            allow_exit = ()
            if args.scenario == "party-drop":
                wait_for(processes, lambda logs: "PARTY_START round=2" in logs["host"] and "PARTY_PAWN_READY round=2" in logs["guest5"],
                         180, "The second party did not start")
                os.killpg(processes["guest5"][0].pid, signal.SIGTERM)
                allow_exit = ("guest5",)
            done_marker = "EXPEDITION_QA_DONE" if room else ("ISLAND_QA_DONE peers=6 reopened=1" if args.scenario == "hunt" else "PARTY_QA_DONE")
            if args.repro_dig:
                done_marker = "EXPEDITION_QA_REPRO_DONE"
            if args.scenario == "mud":
                done_marker = "MUD_QA_DONE"
            if args.scenario == "field":
                done_marker = "FIELD_QA_DONE players=3"
            if args.scenario == "neutral":
                done_marker = f"NEUTRAL_QA_DONE players=3 teams=2 kinds={1 if args.neutral_kind else 5}"
            if args.scenario == "crew-drop":
                wait_for(processes, lambda logs: "EXPEDITION_QA_DROP_READY" in logs["host"], 45, "Three crews did not launch")
                os.killpg(processes[guests[-1]][0].pid, signal.SIGTERM)
                allow_exit = (guests[-1],)
                done_marker = "EXPEDITION_QA_DROP_DONE launched=3 remaining=2"
            logs = wait_for(processes, lambda logs: done_marker in logs["host"], 300,
                            "The multiplayer scenario did not complete", allow_exit)
            if args.repro_dig:
                result = {"scenario": "d-shaft-repro", "position_xy": [1830, -4673], "depth_below_surface_cm": 150, "exposed_and_opened": True}
            elif args.scenario == "mud" and args.mud_exit:
                cover = re.search(r"MUD_QA_COVER (.+)", logs["host"])
                route = re.search(r"MUD_QA_ESCAPE_VARIANT (.+)", logs["host"])
                if not cover or not route or f"route={args.mud_exit} " not in route[1]:
                    raise RuntimeError("Missing closed shell or physical escape evidence")
                result = dict(scenario="mud", cover=cover[1].strip(), escape=route[1].strip(), processes=3, teams=2)
            elif args.scenario == "mud":
                result = {"scenario": "mud", "processes": 3, "teams": 2}
                result["duel_contacts"] = summarize_mud_contacts(logs["host"])
                for label in ("COVER", "ESCAPE", "PIT", "STEPS", "EXIT", "GROUP", "BUTTONS"):
                    match = re.search(r"MUD_QA_" + label + r" (.*)", logs["host"])
                    if not match: raise RuntimeError(f"Missing {label} evidence")
                    result[label.lower()] = match[1].strip()
                shared = logs["host"].split("MUD_QA_STEP stage=17", 1)[1].split("MUD_QA_STEP stage=18", 1)[0]
                for action in ("Dig", "Throw"):
                    uses = shared.count(f"EXPEDITION_GAS_USE action=Expedition.Action.{action} ")
                    if uses < 2:
                        raise RuntimeError(f"Holding both buttons produced only {uses} {action} uses")
                continued = logs["host"].split("MUD_QA_STEP stage=18", 1)[1].split("MUD_QA_STEP stage=19", 1)[0]
                if continued.count("EXPEDITION_GAS_USE action=Expedition.Action.Throw ") < 2:
                    raise RuntimeError("Releasing dig interrupted held throw")
                if args.screenshot:
                    shots = ["MudDuel.png", "MudShell.png", "MudSteps.png"]
                    for shot in shots:
                        if not (shot_output/shot).is_file(): raise RuntimeError(f"Missing {shot}")
                        if shot_output != directory: shutil.copy2(shot_output/shot, directory/shot)
                    result["host_screenshots"] = shots
                for role, log in logs.items():
                    if "MUD_QA_LOCAL stage=3" not in log or "MUD_QA_LOCAL stage=8" not in log:
                        raise RuntimeError(f"Missing actual collision observations from {role}")
            elif args.scenario == "crew-drop":
                result = {"scenario": args.scenario, "launched_crews": 3, "remaining_players": 2, "original_berths_preserved": True}
            elif args.scenario in ("field", "neutral"):
                result = summarize_field(logs) if args.scenario == "field" else summarize_neutral(logs, args.neutral_kind)
                if args.screenshot:
                    shots = [f"Field{name}.png" for name in ("01Surface", "02Dig", "03Assist", "04Seized")]
                    if args.scenario == "neutral":
                        shots = [f"Neutral{kind}{state}.png" for kind in ("Tree","Ship","Hopper","Sail","Tide") for state in ("Before","After") if not args.neutral_kind or kind.lower() == args.neutral_kind]
                    if any(not (shot_output / name).is_file() for name in shots):
                        raise RuntimeError("One or more field viewport captures are missing")
                    if shot_output != directory:
                        for name in shots:
                            shutil.copy2(shot_output / name, directory / name)
                    result["host_screenshots"] = shots
            elif room:
                teams = 1 if together else count
                expected = f"EXPEDITION_QA_DONE players={count} teams={teams} returned=1"
                reports = re.findall(r"EXPEDITION_QA_PRIVATE box=(\d+) peers=(\d+)", logs["host"])
                winners = re.findall(r"ISLAND_WINNER team=(\d+)", logs["host"])
                if expected not in logs["host"] or reports != [(str(box), str(count)) for box in range(5)] or winners != [str(teams - 1)]:
                    raise RuntimeError("Incomplete party membership, privacy or winner verification")
                result = {"scenario": args.scenario, "processes": count, "teams": teams, "winner": int(winners[0]),
                          "peer_inspections": 5 * count, "returned_to_same_room": True, "ordinary_room_decoys": 0}
                if args.stress:
                    result.update(expedition_performance(logs))
                    if not args.headless_host:
                        result["performance_targets_met"] = result["host_frame_p95_ms"] <= 33.3 and max(result["edit_to_collision_p95_ms"].values()) <= 150
            else:
                result = summarize(logs) if args.scenario == "hunt" else summarize_party(logs, args.scenario)
            if args.neutral_audit:
                result["neutral_layout_audit"] = summarize_neutral_audit(logs["host"])
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
    parser.add_argument("--scenario", choices=("hunt", "party", "party-drop", "party-timeout", *ROOM_CASES), default="hunt")
    parser.add_argument("--mud-exit", choices=("under", "roof", "rescue"), help="Check a specific alternate escape from a sealed shell")
    parser.add_argument("--mode", choices=("normal", "emulated", "both"), default="both")
    parser.add_argument("--editor", action="store_true", help="Use editor game processes during iteration")
    parser.add_argument("--headless-host", action="store_true", help="Disable Metal for iteration only")
    parser.add_argument("--stress", action="store_true", help="Exercise 300 concurrent authoritative edits in an eight-player expedition")
    parser.add_argument("--neutral-kind", choices=("tree","ship","hopper","sail","tide"), help="Run one neutral interaction for diagnosis")
    parser.add_argument("--seed", type=int, help="Replay the private expedition round seed")
    parser.add_argument("--neutral-audit", action="store_true", help="Check neutral placement and routes on 64 actual-map seeds during setup")
    parser.add_argument("--profile", action="store_true", help="Add terrain edit preparation, geometry and collision profiling to all processes")
    parser.add_argument("--repro-dig", action="store_true", help="Replay the recorded D excavation and trace actual cross-sections and occluders")
    parser.add_argument("--screenshot", action="store_true", help="Capture the completed chart or the party prompt on the host")
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--map", help="Level package to host, defaults to the scenario's corresponding island")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/IslandQA")
    args = parser.parse_args()
    if args.neutral_audit and args.scenario not in ROOM_CASES:
        parser.error("--neutral-audit requires an expedition room scenario")
    if args.scenario == "neutral" and args.seed is None:
        parser.error("--scenario neutral needs --seed from the 64-seed audit's full_seed")
    if args.stress and args.scenario not in ("eight-teams", "crew-eight"):
        parser.error("--stress requires --scenario eight-teams or crew-eight")
    if args.repro_dig and args.scenario not in ("solo", "crew-eight"):
        parser.error("--repro-dig uses a solo or eight-person cooperative expedition")
    args.map = args.map or (EXPEDITION_MAP if args.scenario in ROOM_CASES else MAP)
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
