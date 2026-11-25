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

APP = ROOT / "Builds/TreasureIsland/Mac/远征岛.app"
MAP = "/Game/TreasureIsland/L_TreasureIsland"
EXPEDITION_MAP = "/Game/TreasureIsland/L_ExpeditionIsland"
GAME_CASES = {
    "games": (3, False), "games-solo": (1, True), "games-crew": (2, True),
    "games-drop": (3, False), "games-eight": (8, False), "games-abort": (2, False), "games-maze": (6, False),
}
MAZE = "PartyMazeChase"
VOXEL_GAMES = {"PartyReefRun", "PartyBeaconClimb", "PartyCrystalHunt", "PartyBridgeDash", "PartyFloodGates", "PartyCrownHold", MAZE}
VEIL_SHOTS = ("PartyPull.png", "TransitionInBlack.png", "TransitionInArrival.png",
              "TransitionOutClosing.png", "TransitionOutBlack.png", "TransitionOutArrival.png")
TREASURE_SHOTS = ("TreasureFragment.png", "TreasureForge.png", "TreasureVault.png")
ROOM_CASES = {**GAME_CASES, "solo": (1, True), "coop": (2, True), "duel": (2, False), "crew-drop": (3, False),
              "mud": (3, False), "field": (3, False), "neutral": (3, False), "crew-eight": (8, True), "eight-teams": (8, False)}
GUESTS = [f"guest{seat}" for seat in range(1, 6)]
ROLES = ["host", *GUESTS]
FAILURE = re.compile(r"(?:VOXEL_PROTOCOL_ERROR|ISLAND_QA_CHECK passed=0|ISLAND_QA_TIMEOUT|Fatal error:).*", re.I)
CHEST_RECEIPT = (r" round=(\d+) team=(\d+) mask=(\d+) points=(\d+) key=(\d+) outcome=(\d+)"
                 r" scouted=(\d+) coordinates=(\d+) fragments=(\d+) crossing=(\d+)")

def verify_interaction(logs, rounds, allow_exit):
    expected = {(int(r[0]), direction) for r in rounds for direction in ("in", "out")}
    pattern = re.compile(r"VEIL_PROOF round=(\d+) direction=(in|out) moved=([\d.]+) early=(\d) black=(\d) unlocked=(\d) drift=([\d.]+) swap=(\d) destination_correction=([\d.]+) black_input=([\d.]+)")
    proof = {}
    for role, log in logs.items():
        if role in allow_exit:
            continue
        views = {}
        for n, direction, travel, early, black, unlocked, drift, swap, destination, black_input in pattern.findall(log):
            key = (int(n), direction)
            if key in views or (early, black, unlocked, swap) != ("0", "1", "0", "1"):
                raise RuntimeError(f"{role} round {n}/{direction}: repeated transition, early lock, unlocked blackout or exposed pawn change")
            if int(n) == 1 and float(travel) < 8.:
                raise RuntimeError(f"{role} round {n}/{direction}: held movement only traveled {travel} cm before blackout")
            if float(drift) > 12.:
                raise RuntimeError(f"{role} round {n}/{direction}: traveled {drift} cm under blackout")
            if float(black_input) > .001:
                raise RuntimeError(f"{role} round {n}/{direction}: blacked-out pawn accepted movement input {black_input}")
            views[key] = dict(round=int(n), direction=direction, before_black_cm=float(travel), black_drift_cm=float(drift),
                              destination_correction_cm=float(destination), black_input=float(black_input))
        if set(views) != expected:
            raise RuntimeError(f"{role} missing transition observations {sorted(expected - set(views))}")
        for n, direction in expected:
            observed = re.findall(rf"PARTY_VEIL stage=(\w+) round={n} direction={direction}\b", log)
            if observed != ["close", "black", "open", "clear"]:
                raise RuntimeError(f"{role} missed a veil phase for round {n}/{direction}: {observed}")
            if direction == "in":
                begin = log.find(f"PARTY_FEATURE_BEGIN round={n} ")
                black = log.find(f"PARTY_VEIL stage=black round={n} direction=in")
                if begin < 0 or begin >= black:
                    raise RuntimeError(f"{role} did not request the feature before round {n} reached full black")
        proof[role] = list(views.values())
    for n, direction in expected:
        host = logs["host"]
        swap = re.search(rf"PARTY_SWAP round={n} direction={direction} players=(\d+)", host)
        ack = list(re.finditer(rf"PARTY_VEIL_ACK round={n} direction={direction} seat=(\d+)",host))
        if not swap or len({m[1] for m in ack if m.start() < swap.start()}) < int(swap[1]):
            raise RuntimeError(f"Authority swapped round {n}/{direction} before all remaining clients were black")
    return proof


def verify_chest_revisit(logs, rounds, allow_exit, exercise_revisit):
    host = logs["host"]
    prepared = re.findall(r"PARTY_PREPARE round=(\d+) chest=(\d+)", host)
    if prepared != [(row[0], row[2]) for row in rounds] or len({site for _, site in prepared}) != len(rounds):
        raise RuntimeError("Each first opening must prepare exactly one minigame per physical chest")
    genuine_mask = sum(1 << int(row[2]) for row in rounds if row[5] == "true")
    previous = {}
    for role, log in logs.items():
        if role in allow_exit:
            continue
        receipts = re.findall("EXPEDITION_GAME_RECEIPT" + CHEST_RECEIPT, log)
        if not receipts or receipts[-1][0] != rounds[-1][0]:
            raise RuntimeError(f"{role} missed the final minigame receipt before the revisit")
        previous[role] = tuple(map(int, receipts[-1][1:]))
    expected = exercise_revisit and any(genuine_mask & ~row[1] for row in previous.values())
    visits = re.findall(r"EXPEDITION_CHEST_REVISIT site=(\d+) team=(\d+) before=(\d+) after=(\d+) party_round=(\d+)", host)
    if len(visits) != int(expected):
        raise RuntimeError(f"Expected {int(expected)} direct pickup, observed {len(visits)}")
    if not visits:
        return None
    site, claimant, before, after, last_round = map(int, visits[0])
    bit = 1 << site
    if not genuine_mask & bit or before & bit or after != before | bit or last_round != len(rounds):
        raise RuntimeError("Direct pickup must add exactly one previously settled site without a new minigame")
    collectors = []
    for role, old in previous.items():
        log = logs[role]
        receipts = re.findall("EXPEDITION_REVISIT_RECEIPT" + CHEST_RECEIPT, log)
        if not receipts:
            raise RuntimeError(f"{role} missed the direct pickup receipt")
        team, mask, _, _, outcome, scouted, _, _, _ = old
        if team == claimant:
            collectors.append(role)
            if mask != before:
                raise RuntimeError(f"{role} had the wrong pre-claim inventory")
            mask = after
            scouted &= ~bit
        points = bin(mask).count("1")
        expected_state = (0, team, mask, points, int(points >= 3), outcome, scouted, mask | scouted, points % 3, int(mask == 15))
        if tuple(map(int, receipts[-1])) != expected_state:
            raise RuntimeError(f"{role} received incorrect progress after the direct pickup")
    if not any("VOXEL_REJECT 本队已取得这个箱子的碎片" in logs[role] for role in collectors):
        raise RuntimeError("The collecting crew did not observe its repeated claim being rejected")
    return dict(site=site, crew=claimant, before=before, after=after, after_round=last_round,
                collectors=collectors, observed_by=list(previous), duplicate_rejected=True)


def verify_private_scouting(logs, rounds, winning_masks, allow_exit, require_blind_rival, revisit):
    pattern = re.compile("EXPEDITION_GAME_RECEIPT" + CHEST_RECEIPT)
    snapshots = {role: {int(row[0]): tuple(map(int,row[1:])) for row in pattern.findall(log)}
                 for role, log in logs.items() if role not in allow_exit}
    previous = {role: (0, 0) for role in snapshots}
    learned, privately_seen, claimed = [], [], []
    for number, _, site, opener, winner, outcome, _ in rounds:
        n, site, opener, winner = map(int, (number, site, opener, winner))
        guest_points, awards = {}, {}
        for role, by_round in snapshots.items():
            if n not in by_round:
                raise RuntimeError(f"{role} has no chart receipt for round {n}")
            team, mask, points, key, _, scouted, coordinates, fragments, crossing = by_round[n]
            old_mask, old_scouted = previous[role]
            new_scout = scouted & ~old_scouted
            crew_won = winning_masks[n] & (1 << team)
            genuine_award = outcome == "true" and (team == opener or crew_won)
            expected_mask = old_mask | (1 << site) if genuine_award else old_mask
            earns_scout = outcome != "true" and crew_won and (old_mask | old_scouted) != 15
            if (mask != expected_mask or scouted & mask or coordinates != (mask | scouted) or coordinates & ~15
                    or points != bin(mask).count("1") or fragments != points % 3 or key != (points >= 3)
                    or crossing != (mask == 15) or (scouted & ~new_scout) != (old_scouted & ~mask)
                    or bin(new_scout).count("1") != int(earns_scout)):
                raise RuntimeError(f"{role} round {n} invalid earned/scouted/private chart state {by_round[n]}")
            if new_scout:
                awards[role] = new_scout
                learned.append(dict(round=n, role=role, team=team, site=new_scout.bit_length()-1))
            if old_scouted & mask:
                claimed.append(dict(round=n, role=role, site=site))
            guest_points[role] = team, coordinates
            previous[role] = mask, scouted
        for winner_role, bit in awards.items():
            for other, (team, coordinates) in guest_points.items():
                if not winning_masks[n] & (1 << team) and not coordinates & bit and guest_points[winner_role][0] != opener:
                    privately_seen.append(dict(round=n, winner=winner_role, blind_rival=other, site=bit.bit_length()-1))
    if revisit:
        for role in revisit["collectors"]:
            if previous[role][1] & (1 << revisit["site"]):
                claimed.append(dict(after_round=revisit["after_round"], role=role, site=revisit["site"], direct=True))
    if require_blind_rival and (not privately_seen or not claimed):
        raise RuntimeError("A rival must win a decoy, see a true site hidden on a losing chart, and collect the scouted site")
    return dict(earned=learned, hidden_from=privately_seen, claimed=claimed)


def verify_veil_pixels(directory):
    from PIL import Image, ImageStat
    evidence = {}
    for name in VEIL_SHOTS:
        with Image.open(directory / name) as image:
            w, h = image.size
            frame = image.convert("L").crop((w//20, h//20, w-w//20, h-h//20))
            hist = frame.histogram()
            dark = sum(hist[:10]) / sum(hist)
            contrast = ImageStat.Stat(frame).stddev[0]
            if "Black" in name:
                if dark < .99:
                    raise RuntimeError(f"{name}: only {dark:.1%} of viewport is fully black")
            elif contrast < 4. or dark > .98:
                raise RuntimeError(f"{name}: scene remains obscured during a partial transition")
            evidence[name] = dict(black_fraction=round(dark,4), luminance_sd=round(contrast,2))
    return evidence


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


def verify_arena_views(log, rounds):
    observations = {}
    for n, mode, gap, body, paws in re.findall(
            r"ARENA_VIEW round=(\d+) mode=(first|third) gap_cm=([\d.]+) body=(\d) paws=(\d+)", log):
        first = mode == "first"
        if ((first and float(gap) > 8) or (not first and float(gap) < 75)
                or (int(body), int(paws)) != ((0, 2) if first else (1, 0))):
            raise RuntimeError(f"Round {n} {mode} camera has gap {gap}, body {body}, first person paws {paws}")
        observations.setdefault(int(n), {})[mode] = float(gap)
    targets = {(int(n), int(first)) for n, first in re.findall(r"ARENA_VIEW_TARGET round=(\d+) first=(\d) matched=1", log)}
    returns = {int(n) for n in re.findall(r"ARENA_VIEW_RETURN round=(\d+) first=\d", log)}
    for n, *rest in rounds:
        n = int(n)
        if (set(observations.get(n, {})) != {"first", "third"} or n not in returns
                or not all((n, value) in targets for value in (0, 1))):
            raise RuntimeError(f"Round {n}: missing first/third view, center aimed block or matching island return")
    return observations


def arena_view_shots(rounds):
    return [f"{game}-R{n}-{mode}.png" for n, game, *_ in rounds for mode in ("First", "Third")]


def terrain_shots(rounds):
    return [f"{game}-R{n}-{stage}.png" for n, game, *_ in rounds for stage in ("Before", "Cut", "Mound")]


def verify_arena_terrain(logs, rounds):
    records = {}
    sightings = {}
    for role, log in logs.items():
        samples = {}
        for n, game, stage, seq, heights, normals, origin in re.findall(
                r"ARENA_TERRAIN_PROFILE round=(\d+) game=(\w+) stage=(\d+) seq=(\d+) height_mm=([-\d,]+) normals=([\d,]+) origin_mm=(-?\d+)", log):
            key = (int(n), game)
            stage = int(stage)
            if stage != int(seq) or stage in samples.setdefault(key, {}):
                raise RuntimeError(f"{role} {key}: missing initial reset or duplicate terrain edit {seq}")
            samples[key][stage] = ([int(v) for v in heights.split(",")], [int(v) for v in normals.split(",")], int(origin))
        if set(samples) != {(int(n), game) for n, game, *_ in rounds}:
            raise RuntimeError(f"{role}: missing collision profile in a visited arena")
        if sightings and samples != sightings:
            raise RuntimeError(f"{role}: replicated collision heights or surface normals disagree")
        sightings = samples
    first_visits = {}
    restored = []
    for (n, game), stages in sightings.items():
        if set(stages) != {0, 1, 2, 3}:
            raise RuntimeError(f"{game} round {n}: missing cut or one of the two fill operations")
        before, cut, refill, mound = (stages[i][0] for i in range(4))
        if max(abs(v) for v in before) > 12 or min(stages[0][1]) < 980:
            raise RuntimeError(f"{game}: the spawn-side control strip is not initially flat")
        if game in first_visits:
            previous, initial = first_visits[game]
            if initial != stages[0]:
                raise RuntimeError(f"{game}: round {n} did not restore the physical surface from round {previous}")
            restored.append({"feature": game, "previous_round": previous, "round": n})
        first_visits[game] = (n, stages[0])
        if any(len(h) != 8 for h in (before, cut, refill, mound)):
            raise RuntimeError(f"{game}: missing a collision ray")
        if not (180 <= before[0]-cut[0] <= 650 and refill[0] > cut[0]+120 and mound[0] > before[0]+120):
            raise RuntimeError(f"{game}: the cuts and fills didn't change the physical floor as expected: {stages}")
        if any(abs(heights[-1] - before[-1]) > 15 for heights in (cut, refill, mound)):
            raise RuntimeError(f"{game}: terrain outside the brush changed")
        if not any(20 < abs(mound[i]-before[i]) < mound[0]-before[0]-20 for i in range(1,7)):
            raise RuntimeError(f"{game}: missing intermediate collision heights on the edge of the mound")
        if not any(60 < normal < 980 for normal in stages[3][1][1:-1]):
            raise RuntimeError(f"{game}: the collision mesh has no sloped skirt")
        records[str(n)] = {"feature": game, "cut_cm": (before[0]-cut[0])/10,
                           "mound_cm": (mound[0]-before[0])/10,
                           "edge_heights_cm": [v/10 for v in mound], "mound_normal_z": [v/1000 for v in stages[3][1]]}
    return {"clients": list(logs), "profiles_by_round": records, "restored_surfaces": restored}


def verify_venue_demolition(logs, rounds, all_buildings):
    """Match each replicated removal, including collision, on three independent clients."""
    count = 10 if all_buildings else 1
    masks = [0] + [(1 << (i + 1)) - 1 for i in range(count)]
    seen = {}
    restored_rounds = []
    for number, feature, *_ in rounds:
        n = int(number)
        parts = 40 if feature in ("PartyBeaconClimb", "PartyBridgeDash", "PartyCrownHold") else 30
        for role, log in logs.items():
            rows = re.findall(rf"EXPEDITION_VENUE_VIEW round={n} gone=(\d+) pieces=(\d+) seat=(\d+)", log)
            expected_seat = 0 if role == "host" else int(role[5:])
            if {tuple(map(int, row)) for row in rows} != {(mask, parts, expected_seat) for mask in masks}:
                raise RuntimeError(f"{role} missed a building, collision or component change in round {n} ({feature})")
        removed = re.findall(rf"EXPEDITION_VENUE_REMOVED round={n} feature={feature} id=(\d+)", logs["host"])
        aim = re.findall(rf"EXPEDITION_VENUE_AIM round={n} id=(\d+) seat=\d+ range_cm=(\d+) feet=", logs["host"])
        if list(map(int, removed)) != list(range(count)) or [int(i) for i, _ in aim] != list(range(count)):
            raise RuntimeError(f"Round {n} did not aim at and remove buildings individually")
        if any(not 435 < int(distance) <= 12500 for _, distance in aim):
            raise RuntimeError(f"Round {n} did not exercise distant buildings from a board stand")
        if feature == "PartyReefRun":
            controls = re.findall(rf"EXPEDITION_VENUE_OCCLUSION round={n} far=(\d)", logs["host"])
            if controls != ["1", "0"]:
                raise RuntimeError("The distant reef wall and close dig-precedence controls did not both run")
        if feature in seen:
            restored_rounds.append({"feature": feature, "previous_round": seen[feature]["round"], "round": n})
        seen[feature] = {"round": n, "buildings": count, "pieces_removed": parts * count // 10}
    if len(rounds) >= 7 and not restored_rounds:
        raise RuntimeError("No previously demolished arena was reopened with its full skyline")
    return {"unique_buildings": sum(row["buildings"] for row in seen.values()),
            "unique_components": sum(row["pieces_removed"] for row in seen.values()),
            "peers": sorted(logs), "masks_observed_each_round": masks, "restored": restored_rounds}


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
    if ("FIELD_QA_VAULT_KEYS three_rejected=1 crews=2" not in logs["host"]
            or "VOXEL_REJECT 钥匙已合成，还需集齐四个真箱点位" not in logs["guest2"]):
        raise RuntimeError("A crew with a key and three sites reached the vault without receiving its fourth-site requirement")
    events = re.findall(r"ISLAND_LOCK (\w+) chest=4 ", logs["host"])
    if events != ["start", "assist", "handoff", "seize", "abandoned", "start", "assist", "abandoned"]:
        raise RuntimeError(f"Unexpected contested vault transitions: {events}")
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
            "abandon_resets_work": True, "three_sites_with_key_rejected": True, "unspent_keys_preserved": True,
            "physical_held_retarget_and_release": True,
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
    ranges = {}
    for row in re.findall(r"NEUTRAL_QA_RANGE ([^\n]+)", host):
        ranges.update({k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", row)})
    checked = (only,) if only else ("tree", "ship", "hopper", "sail", "tide")
    for kind in checked:
        if not any(name.startswith(kind + "_") for name in ranges):
            raise RuntimeError(f"Missing enlarged {kind} reach measurement")
    seed = re.search(r"EXPEDITION_QA_SEED (-?\d+)", host)
    return {"effective_ranges_cm": ranges, "scenario": "neutral", "processes": 3, "teams": 2, "seed": int(seed[1]),
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


def summarize_search_audit(host):
    audit = re.search(r"SEARCH_AREA_AUDIT pass=1 seeds=(\d+) replay=(\d+) exact=(\d+) obstacles=(\d+) "
                      r"center_guess_miss_cm=(\d+)/(\d+)/(\d+) unique_meter_cells=(\d+) "
                      r"prize_quadrants=(\d+)/(\d+)/(\d+)/(\d+)", host)
    if not audit:
        raise RuntimeError("Missing search-area seed replay and intersection measurements")
    traps = re.search(r"SEARCH_TRAP_AUDIT per_seed=(\d+) replayed=(\d+) unchanged_true_layouts=(\d+) "
                      r"surface_excavations=(\d+) regions=(\d+)", host)
    if not traps:
        raise RuntimeError("Missing decoy replay, independent treasure layout and excavation observations")
    count, replay, exact, obstacles, low, median, high, cells, *quadrants = map(int, audit.groups())
    per_seed, replayed_traps, unchanged, excavated, region_mask = map(int, traps.groups())
    if replayed_traps != per_seed * count or unchanged != count or excavated != per_seed:
        raise RuntimeError("Incomplete trap and treasure layout comparisons")
    return {"seeds": count, "replays": replay, "exact_intersections": exact, "obstacles": obstacles,
            "circle_center_error_cm": {"min": low, "median": median, "max": high},
            "unique_meter_cells": cells, "prize_quadrants": quadrants,
            "decoys_per_seed": per_seed, "replayed_decoys": replayed_traps, "unchanged_true_layouts": unchanged,
            "sample_decoy_excavations": excavated, "sample_decoy_region_mask": region_mask}


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


def summarize_game_abort(logs):
    host, guest = logs["host"], logs["guest1"]
    checks = ("PARTY_ABORT round=1 timeout=1", "PARTY_HUNTING round=1 unloaded=1 available=1",
              "ISLAND_QA_CHECK passed=1 failed loading leaves the false chest available",
              "PARTY_START round=2", "PARTY_HUNTING round=2 unloaded=1 available=1")
    if not all(check in host for check in checks) or "PARTY_SWAP round=1 direction=in" in host:
        raise RuntimeError("Missing safe rollback and successful return from the retry")
    for role, log in logs.items():
        stages = re.findall(r"PARTY_VEIL stage=(\w+) round=1 direction=(\w+)", log)
        if stages != [("close","in"), ("black","in"), ("close","out"), ("black","out"), ("open","out"), ("clear","out")]:
            raise RuntimeError(f"{role} observed unexpected blackout stages during the cancelled load: {stages}")
    attempts = re.findall(r"PARTY_PREPARE round=[12] chest=(\d+)", host)
    settled = re.findall(r"PARTY_CHEST_SETTLED round=(\d+) site=(\d+) outcome=(\d+) opener=\d+ winner=\d+ rewarded=(\d+)", host)
    if (len(attempts) != 2 or attempts[0] != attempts[1] or int(attempts[0]) <= 4 or len(settled) != 1
            or settled[0] not in [("2", attempts[0], effect, "0") for effect in ("2","3")]):
        raise RuntimeError("The two attempts did not settle the same false chest exactly once")
    moved = re.search(r"PARTY_ABORT_WALK target=guest cm=([\d.]+) unlocked=1", guest)
    if not moved or float(moved[1]) < 25.:
        raise RuntimeError("Guest movement did not recover after the cancelled load")
    return dict(scenario="games-abort", processes=2, cancelled_before_pawn_swap=True,
                guest_recovered_movement_cm=float(moved[1]), retried_chest=int(attempts[0]),
                cancelled_rewards=0, successful_retry_settlements=1)


def run(args, ip, mode, directory):
    directory.mkdir(parents=True, exist_ok=True)
    summary = directory / "summary.json"
    summary.unlink(missing_ok=True)
    options = ["-PktLag=40", "-PktLoss=2"] if mode == "emulated" else []
    if args.editor:
        engine = Path(os.environ.get("UE_ROOT", "/Users/Shared/Epic Games/UE_5.8"))
        base = [str(engine / "Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"), str(PROJECT), "-game", "-NoAssetRegistryCacheWrite"]
    else:
        base = [str(args.app / "Contents/MacOS/ActDemo")]
    shared = ["-IslandQA", "-IslandLAN", "-unattended", "-nosound", "-NoSplash", "-NoTrace", "-stdout", "-FullStdOutLogOutput",
              "-ExecCmds=t.MaxFPS 60", "-windowed", "-ResX=1280", "-ResY=720"]
    if args.profile:
        shared.append("-VoxelProfile")
    room = ROOM_CASES.get(args.scenario)
    guests = GUESTS
    if room:
        count, together = room
        guests = [f"guest{seat}" for seat in range(1, count)]
        shared.append(f"-ExpeditionPlayers={count}")
        if args.identity_shots:
            shared.append("-ExpeditionIdentityShots")
        if args.decoys is not None:
            shared.append(f"-ExpeditionDecoys={args.decoys}")
        elif args.scenario in GAME_CASES:
            shared.append("-ExpeditionDecoys=2")
        if together:
            shared.append("-ExpeditionTogether")
        if args.stress:
            shared.append("-ExpeditionStress")
        if args.scenario == "solo" and not args.repro_dig:
            shared.append("-ExpeditionEarlyPrize")
        if args.repro_dig:
            shared.extend(["-ExpeditionRepro", "-ExpeditionSeed=7319"])
        if args.scenario == "crew-drop":
            shared.append("-ExpeditionDrop")
        if args.scenario in GAME_CASES:
            shared.append("-ExpeditionGamesQA")
            if args.scenario == "games-maze":
                shared.append("-ExpeditionMazeQA")
            if args.interaction or args.venue_demolition or args.venue_smoke or args.arena_terrain or args.arena_views:
                shared.append("-ExpeditionScoreGamesQA")
            if args.interaction:
                shared.append("-ExpeditionInteractionQA")
            if args.venue_demolition:
                shared.append("-ExpeditionVenueDemolitionQA")
            elif args.venue_smoke:
                shared.append("-ExpeditionVenueSmokeQA")
            if args.arena_terrain:
                shared.append("-ExpeditionArenaTerrainQA")
            if args.arena_views:
                shared.append("-ExpeditionArenaViewsQA")
            if args.game_only:
                shared.append("-ExpeditionGameOnly=" + args.game_only)
            if args.scenario == "games-drop":
                shared.append("-ExpeditionGamesDropQA")
            if args.scenario == "games-abort":
                shared.append("-ExpeditionAbortQA")
        if args.scenario == "mud":
            shared.append("-ExpeditionMudQA")
            if args.mud_exit:
                shared.append(f"-MudExit={args.mud_exit}")
        if args.scenario == "neutral":
            shared.append("-ExpeditionNeutralQA")
            if args.structures:
                shared.append("-NeutralDemolitionQA")
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
    if (args.scenario in ("field", "neutral", "mud") or args.scenario in GAME_CASES) and args.screenshot and not args.editor:
        with (args.app / "Contents/Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream)["CFBundleIdentifier"]
        shot_output = Path.home() / "Library/Containers" / bundle / "Data/tmp/ExpeditionField" / str(time.time_ns())
    if args.interaction and args.screenshot and args.editor:
        for name in (*VEIL_SHOTS, *TREASURE_SHOTS, "TreasureIntel.png", "TreasureFalseChart.png", "TreasureVaultOpened.png"):
            (shot_output / name).unlink(missing_ok=True)
    if args.render_guests:
        for guest in guests:
            (shot_output / guest).mkdir(parents=True,exist_ok=True)
            for name in (*VEIL_SHOTS, "TreasureIntel.png"):
                (shot_output / guest / name).unlink(missing_ok=True)
    processes = {}
    with ExitStack() as stack:
        def launch(role, url):
            path = directory / (role + ".log")
            stream = stack.enter_context(path.open("w"))
            render = [] if (role == "host" and not args.headless_host) or (role != "host" and args.render_guests) else ["-nullrhi"]
            if (role == "host" or args.render_guests) and args.screenshot:
                render.append("-IslandScreenshot")
                if (args.scenario in ("field", "neutral", "mud") or args.scenario in GAME_CASES):
                    render.append(f"-FieldShotsDir={shot_output if role == 'host' else shot_output / role}")
            scenario_flags = ["-NeutralLayoutAudit"] if role == "host" and args.neutral_audit else []
            if role == "host" and args.seed is not None:
                scenario_flags.append(f"-ExpeditionSeed={args.seed}")
            if role == "host" and args.scenario == "hunt":
                scenario_flags.append("-ini:Game:[/Script/ActDemo.IslandGameMode]:DecoyChestCount=0")
            if role == "guest5" and args.scenario == "party-timeout":
                scenario_flags.append("-IslandPartySkipReady")
            if role == "guest1" and args.scenario == "games-abort":
                scenario_flags.append("-IslandPartySkipReady")
            process = subprocess.Popen(base + [url, f"-port={args.port}"] + shared + render + options + scenario_flags,
                                       stdout=stream, stderr=subprocess.STDOUT, start_new_session=True, cwd=ROOT)
            processes[role] = process, path

        try:
            launch("host", args.map + "?listen?bIsLanMatch")
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
            if args.stall_before_black:
                initial = wait_for(processes, lambda logs: all("PARTY_FEATURE_ACTIVE round=1 success=1" in log for log in logs.values()),
                                   120, "Features were not ready for the host stall")
                if "PARTY_VEIL stage=black round=1 direction=in" in initial["host"]:
                    raise RuntimeError("Host already submitted blackout before the requested stall")
                host = processes["host"][0]
                os.kill(host.pid, signal.SIGSTOP)
                try:
                    time.sleep(17.)
                finally:
                    os.kill(host.pid, signal.SIGCONT)
            allow_exit = ()
            if args.scenario == "party-drop":
                wait_for(processes, lambda logs: "PARTY_START round=2" in logs["host"] and "PARTY_PAWN_READY round=2" in logs["guest5"],
                         180, "The second party did not start")
                os.killpg(processes["guest5"][0].pid, signal.SIGTERM)
                allow_exit = ("guest5",)
            done_marker = "EXPEDITION_QA_DONE" if room else ("ISLAND_QA_DONE peers=6 reopened=1" if args.scenario == "hunt" else "PARTY_QA_DONE")
            if args.scenario in GAME_CASES:
                if args.scenario == "games-drop":
                    wait_for(processes, lambda logs: "EXPEDITION_GAMES_DROP_READY" in logs["host"], 150,
                             "The first arena did not open for the departing guest")
                    os.killpg(processes[guests[-1]][0].pid, signal.SIGTERM)
                    allow_exit = (guests[-1],)
                done_marker = "EXPEDITION_GAMES_DONE rounds="
            if args.repro_dig:
                done_marker = "EXPEDITION_QA_REPRO_DONE"
            if args.scenario == "mud":
                done_marker = "MUD_QA_DONE"
            if args.scenario == "field":
                done_marker = "FIELD_QA_DONE players=3"
            if args.scenario == "neutral":
                done_marker = f"NEUTRAL_QA_DONE players=3 teams=2 kinds={1 if args.neutral_kind else 5}"
            if args.structures:
                done_marker = "SCENERY_QA_DONE"
            if args.scenario == "crew-drop":
                wait_for(processes, lambda logs: "EXPEDITION_QA_DROP_READY" in logs["host"], 45, "Three crews did not launch")
                os.killpg(processes[guests[-1]][0].pid, signal.SIGTERM)
                allow_exit = (guests[-1],)
                done_marker = "EXPEDITION_QA_DROP_DONE launched=3 remaining=2"
            timeout = 480 if args.scenario == "games-maze" and args.screenshot else 300
            if args.scenario in GAME_CASES and not args.game_only:
                decoys = args.decoys if args.decoys is not None else 2
                timeout = 95 * (5 + decoys)
            logs = wait_for(processes, lambda logs: done_marker in logs["host"], timeout,
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
            elif args.structures:
                done = re.search(r"SCENERY_QA_DONE (static=\d+ landmark_parts=\d+ docks=8 neutral=\d+ kinds=31 remaining=0) hits=(\d+) peers=3 recovery=1", logs["host"])
                if not done or any(f"NEUTRAL_QA_VIEW stage=96 digest={done[1]}" not in log for log in logs.values()):
                    raise RuntimeError("All three peers must observe the removed meshes and disabled collision")
                attachment_hits = re.findall(r"SCENERY_QA_ATTACHMENT_STRIKE kind=(\d+) part=(\w+)", logs["host"])
                attachment_faces = re.findall(r"SCENERY_QA_ATTACHMENT_FACE kind=(\d+) part=(\w+)", logs["host"])
                if {kind for kind, _ in attachment_hits} != {"0", "1", "2", "3"}:
                    raise RuntimeError("Missing authoritative shovel hits on the tree, ship, hopper, or sail attachments")
                names = ("tree", "ship", "hopper", "sail")
                result = dict(scenario="structures", processes=3, geometry=done[1], strikes=int(done[2]), safe_recovery=True,
                              attachment_hits={name: [part for kind, part in attachment_hits if kind == str(index)] for index, name in enumerate(names)},
                              exposed_attachments={name: sorted({part for kind, part in attachment_faces if kind == str(index)}) for index, name in enumerate(names)})
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
            elif args.scenario == "games-abort":
                result = summarize_game_abort(logs)
            elif args.scenario in GAME_CASES:
                rounds = re.findall(r"EXPEDITION_GAME_RETURN round=(\d+) feature=(\w+) site=(\d+) opening=(\d+) winner=(-?\d+) result=(\w+) credited_mask=(\d+)", logs["host"])
                winning_masks = {int(n): int(mask) for n, mask in re.findall(r"EXPEDITION_GAME_RETURN round=(\d+) [^\n]*? winners=(\d+)", logs["host"])}
                games = {row[1] for row in rounds}
                if not rounds or any(game not in VOXEL_GAMES for game in games):
                    raise RuntimeError("A completed arena and its reveal are missing")
                if args.game_only:
                    if len(rounds) != (2 if args.scenario == "games-maze" else 1) or games != {args.game_only}:
                        raise RuntimeError("The requested arena did not complete its expected rounds")
                else:
                    layout = re.search(r"EXPEDITION_CHEST_LAYOUT true=4 false=(\d+) vault=1", logs["host"])
                    decoys = args.decoys if args.decoys is not None else 2
                    if not layout or int(layout[1]) != decoys:
                        raise RuntimeError("Additional decoy count differs from the room host's selection")
                    if {int(r[2]) for r in rounds if r[5] == "true"} != set(range(4)):
                        raise RuntimeError("All four genuine sites must survive the host's decoy setting")
                    if sorted(int(r[2]) for r in rounds if r[5] != "true") != list(range(5, 5 + decoys)):
                        raise RuntimeError("Every configured decoy must resolve exactly once")
                    deck = 6 if together or "-ExpeditionScoreGamesQA" in shared else 7
                    if len(games) != min(len(rounds), deck) or len({row[1] for row in rounds[:deck]}) != min(len(rounds), deck):
                        raise RuntimeError("An arena repeated before the eligible deck was exhausted")
                owned_sites = {}
                for n, game, site, opening, winner, outcome, awarded in rounds:
                    credited = 0
                    if outcome == "true":
                        eligible = winning_masks[int(n)] | (1 << int(opening))
                        for team in (t for t in range(8) if eligible & (1 << t)):
                            sites = owned_sites.setdefault(team, set())
                            if site not in sites:
                                credited |= 1 << team
                                sites.add(site)
                    if outcome not in ("true", "scatter", "disguise") or int(awarded) != credited:
                        raise RuntimeError(f"Unexpected credit mask at round {n}: got {awarded}, expected {credited}")
                for role, log in logs.items():
                    if role not in allow_exit and len(re.findall(r"PARTY_VENUE_VISIBLE round=\d+", log)) != len(rounds):
                        raise RuntimeError(f"{role} did not stream every selected map")
                if set(re.findall(r"EXPEDITION_STONE_DUG feature=(\w+)", logs["host"])) != games - {MAZE}:
                    raise RuntimeError("A played arena did not allow excavation of its stone foundation")
                result = {"scenario": args.scenario, "processes": count, "departed": len(allow_exit),
                          "games": [dict(round=int(n), feature=g, site=int(site), opener=int(o), winner=int(w),
                                         result=kind, winning_team_mask=winning_masks[int(n)], credited_team_mask=int(teams))
                                    for n,g,site,o,w,kind,teams in rounds]}
                snapshots = {}
                snapshots_by_round = []
                for role, log in logs.items():
                    if role in allow_exit:
                        continue
                    per_game = {}
                    by_seed = {}
                    for match in re.finditer(r"PARTY_GRID_VIEW game=(\w+)Session seed=(-?\d+) initial=(\w+) grid=(\w+) changed=(\d+) winner=(-?\d+) scores=([^\s]+)", log):
                        name, seed, initial, final, changed, winner, scores = match.groups()
                        per_game["Party" + name] = (seed, initial, final, int(changed), int(winner), scores)
                        by_seed[seed] = ("Party" + name, initial, final, int(changed), int(winner), scores)
                    settled = list(by_seed.items())
                    if [board[0] for _, board in settled] != [row[1] for row in rounds]:
                        raise RuntimeError(f"{role} missed a settled game board, including repeat arena visits")
                    if snapshots_by_round and settled != snapshots_by_round:
                        raise RuntimeError(f"{role} disagrees on a completed round's seed, terrain, winner or scores")
                    snapshots_by_round = settled
                    if set(per_game) != games or any(row[3] == 0 or row[4] < 0 for name, row in per_game.items() if name != MAZE):
                        raise RuntimeError(f"{role} did not observe terrain edits and a winner in every arena")
                    if snapshots and snapshots != per_game:
                        raise RuntimeError(f"{role} and host disagree on the settled terrain, score or winner")
                    snapshots = per_game
                result["replicated_boards"] = snapshots
                revisit = verify_chest_revisit(logs, rounds, allow_exit, args.scenario != "games-maze")
                if revisit:
                    result["direct_chest_pickup"] = revisit
                if args.scenario == "games-maze":
                    if "MAZE_QA_DEFAULT limit=180" not in logs["host"]:
                        raise RuntimeError("The unmodified chase duration was not verified")
                    if args.screenshot and not re.search(r"MAZE_CHASE_START [^\n]*seconds=180\b", logs["host"]):
                        raise RuntimeError("The photographed round did not run the full three-minute pursuit clock")
                    if winning_masks[1].bit_count() != 1 or winning_masks[2].bit_count() != 2:
                        raise RuntimeError("Expected one winning evader crew, then two winning hunter crews")
                    layouts = {}
                    for role, log in logs.items():
                        views = {(int(n), int(stage)) for n, stage in re.findall(r"MAZE_QA_VIEW round=(\d+) stage=(\d+)", log)}
                        if views != {(n,s) for n in (1,2) for s in (1,3,4,6)}:
                            raise RuntimeError(f"{role} missed a wall collision update or capture state")
                        peer_layouts = {n: (seed, digest) for n, seed, digest in re.findall(
                            r"MAZE_QA_LAYOUT round=(\d+) seed=(-?\d+) hash=(\w+) reachable=81", log)}
                        if (set(peer_layouts) != {"1", "2"} or peer_layouts["1"][1] == peer_layouts["2"][1]
                                or layouts and peer_layouts != layouts):
                            raise RuntimeError(f"{role} missed a connected layout or disagrees with another peer's generated walls")
                        layouts = peer_layouts
                    result["maze_layouts"] = layouts
                    all_views = "\n".join(logs.values())
                    for seed, _ in layouts.values():
                        swings = set(re.findall(rf"MAZE_QA_TOOL seed={seed} place=([01]) target=(\w+)", all_views))
                        if not {("1", "wall"), ("0", "wall"), ("1", "gap"), ("1", "runner")} <= swings:
                            raise RuntimeError(f"Expected real cursor inputs for the occluded runner, wall, opening and tag in seed {seed}")
                    spectated = {int(n) for log in logs.values() for n in re.findall(r"MAZE_QA_SPECTATE round=(\d+)", log)}
                    if spectated != {1,2}:
                        raise RuntimeError("A frozen evader did not watch the surviving teammate")
                    result["maze_checks"] = ["blocked capture", "cut and restored wall on every peer",
                                             "caught runner frozen and watching teammate", "survival and full capture"]
                result["replicated_boards_by_round"] = [
                    dict(round=int(r[0]), seed=int(seed), feature=name, grid=grid, changed=changed, winner=winner, scores=scores)
                    for r, (seed, (name, _, grid, changed, winner, scores)) in zip(rounds, snapshots_by_round)]
                if args.interaction:
                    result["transition_views"] = verify_interaction(logs, rounds, allow_exit)
                    result["private_scouting"] = verify_private_scouting(logs, rounds, winning_masks, allow_exit,
                        args.scenario == "games" and not args.game_only and any(row[5] != "true" for row in rounds), revisit)
                    if not args.game_only:
                        for role, log in logs.items():
                            if role not in allow_exit and "TREASURE_VAULT_REVEALED winner=" not in log:
                                raise RuntimeError(f"{role} never saw the open final vault")
                        if args.seed == 3 and (args.decoys is None or args.decoys >= 2):
                            if not {"scatter", "disguise"} <= {row[5] for row in rounds}:
                                raise RuntimeError("Seed 3 failed to exercise both previously reproducible decoy events")
                        result["vault_revealed_on_remaining_clients"] = True
                if args.arena_terrain:
                    result["continuous_terrain"] = verify_arena_terrain(logs, rounds)
                if args.arena_views:
                    result["arena_camera_distances_cm"] = {role: verify_arena_views(log, rounds) for role, log in logs.items()}
                if args.venue_demolition or args.venue_smoke:
                    result["venue_demolition"] = verify_venue_demolition(logs, rounds, args.venue_demolition)
                for role, log in logs.items():
                    if role in allow_exit:
                        continue
                    receipts = {int(n) for n in re.findall(r"EXPEDITION_GAME_RECEIPT round=(\d+)", log)}
                    if receipts != set(range(1, len(rounds) + 1)):
                        raise RuntimeError(f"{role} missed a private progress snapshot")
                if args.screenshot:
                    shots = (["MazeChasePlay.png", "PartyMazeChaseOverview.png"] if args.scenario == "games-maze" else
                             [game + suffix for game in sorted(games) for suffix in (".png", "Overview.png")])
                    shots.append("PartyPull.png")
                    if args.arena_terrain:
                        shots.extend(terrain_shots(rounds))
                    if args.arena_views:
                        shots.extend(arena_view_shots(rounds))
                    if args.venue_demolition and "PartyBridgeDash" in games:
                        shots.extend(["VenueBridgeBefore.png", "VenueBridgeAfter.png"])
                    if args.scenario != "games-maze" and not ((args.identity_shots or args.interaction) and args.game_only):
                        shots.append("PartyTreasureHint.png")
                    if args.interaction:
                        shots.extend(VEIL_SHOTS[1:])
                        if any(view["role"] == "host" for view in result["private_scouting"]["earned"]):
                            shots.append("TreasureIntel.png")
                        if any(row[5] in ("scatter", "disguise") for row in rounds):
                            shots.append("TreasureFalseChart.png")
                        if not args.game_only:
                            shots.extend(TREASURE_SHOTS)
                            shots.append("TreasureVaultOpened.png")
                    if not args.game_only:
                        shots.extend(["TreasureChart3.png", "TreasureChart4.png"])
                    if args.identity_shots:
                        if "EXPEDITION_IDENTITY_DONE allies=1 rivals=1 fog_names=0 occlusion=1" not in logs["host"]:
                            raise RuntimeError("The rendered host did not verify names, fog recovery and nameplate occlusion")
                        shots.extend(["IdentityFog.png", "IdentityFogChart.png", "IdentityRevealed.png", "IdentityOccluded.png"])
                    for name in shots:
                        if not (shot_output / name).is_file():
                            raise RuntimeError(f"Missing arena viewport capture {name}")
                        if shot_output != directory:
                            shutil.copy2(shot_output / name, directory / name)
                    result["host_screenshots"] = shots
                    if (args.arena_views or args.arena_terrain) and args.render_guests:
                        for guest in guests:
                            target = directory / guest
                            target.mkdir(exist_ok=True)
                            guest_shots = arena_view_shots(rounds) if args.arena_views else []
                            if args.arena_terrain:
                                guest_shots.extend(terrain_shots(rounds))
                            for name in guest_shots:
                                source = shot_output / guest / name
                                if not source.is_file():
                                    raise RuntimeError(f"{guest} missed arena view {name}")
                                if source != target / name:
                                    shutil.copy2(source, target / name)
                    if args.interaction:
                        result["transition_pixels"] = verify_veil_pixels(directory)
                        if args.render_guests:
                            result["guest_transition_pixels"] = {}
                            for guest in guests:
                                if guest in allow_exit:
                                    continue
                                target = directory / guest
                                target.mkdir(exist_ok=True)
                                guest_shots = list(VEIL_SHOTS)
                                if any(view["role"] == guest for view in result["private_scouting"]["earned"]):
                                    guest_shots.append("TreasureIntel.png")
                                for name in guest_shots:
                                    source = shot_output / guest / name
                                    if not source.is_file():
                                        raise RuntimeError(f"{guest} missed viewport screenshot {name}")
                                    if source != target / name:
                                        shutil.copy2(source,target / name)
                                result["guest_transition_pixels"][guest] = verify_veil_pixels(target)
            elif room:
                teams = 1 if together else count
                expected = f"EXPEDITION_QA_DONE players={count} teams={teams} returned=1"
                reports = re.findall(r"EXPEDITION_QA_PRIVATE box=(\d+) peers=(\d+)", logs["host"])
                winners = re.findall(r"ISLAND_WINNER team=(\d+)", logs["host"])
                if expected not in logs["host"] or reports != [(str(box), str(count)) for box in range(5)] or winners != [str(teams - 1)]:
                    raise RuntimeError("Incomplete party membership, privacy or winner verification")
                result = {"scenario": args.scenario, "processes": count, "teams": teams, "winner": int(winners[0]),
                          "peer_inspections": 5 * count, "returned_to_same_room": True}
                if args.scenario == "solo":
                    if not all(marker in logs["host"] for marker in ("points=3 key=1 vault_hidden=1", "points=4 key=1 vault_exposed=1")):
                        raise RuntimeError("Missing three-point denial or four-point vault access")
                    result["pre_excavated_vault_hidden_with_three_points"] = True
                    result["revealed_with_four_points_and_key_spent"] = True
                if args.stress:
                    result.update(expedition_performance(logs))
                    if not args.headless_host:
                        result["performance_targets_met"] = result["host_frame_p95_ms"] <= 33.3 and max(result["edit_to_collision_p95_ms"].values()) <= 150
            else:
                result = summarize(logs) if args.scenario == "hunt" else summarize_party(logs, args.scenario)
            if args.stall_before_black:
                if "PARTY_ABORT" in logs["host"] or "PARTY_START round=1" not in logs["host"]:
                    raise RuntimeError("A ready minigame was cancelled during the host's display stall")
                result["host_stall_after_assets_seconds"] = 17
            if args.neutral_audit:
                result["neutral_layout_audit"] = summarize_neutral_audit(logs["host"])
                result["search_area_audit"] = summarize_search_audit(logs["host"])
            if args.scenario == "solo" and not args.repro_dig:
                checks = ("an early blind dig clears the prize site without revealing its actor",
                          "four points publish the exact crossing and expose the excavated vault")
                if any("ISLAND_QA_CHECK passed=1 " + check not in logs["host"] for check in checks):
                    raise RuntimeError("Missing concealed prize and key reveal observations")
                result["early_prize_excavation"] = True
            if options and not all("PktLag set to 40" in log and "PktLoss set to 2" in log for log in logs.values()):
                raise RuntimeError("Missing runtime packet simulation settings")
            result.update(mode=mode, ip=ip, host_render="NullRHI" if args.headless_host else "Metal", guest_render="Metal" if args.render_guests else "NullRHI",
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
    parser.add_argument("--decoys", type=int, choices=range(25), metavar="0..24", help="Apply the host room setting for extra false chests")
    parser.add_argument("--identity-shots", action="store_true", help="Capture two crewmates and one rival before, during and after disguise")
    parser.add_argument("--game-only", choices=sorted(VOXEL_GAMES), help="Use a single trap game during expedition games QA")
    parser.add_argument("--arena-terrain", action="store_true", help="Dig and fill off-grid with the normal mouse controls, compare collision profiles on each peer")
    parser.add_argument("--arena-views", action="store_true", help="Toggle V in each arena and verify local camera, visible body, center aim and island return")
    venue = parser.add_mutually_exclusive_group()
    venue.add_argument("--venue-demolition", action="store_true", help="Dig every building on each minigame map and compare geometry and collision after every strike on all three processes")
    venue.add_argument("--venue-smoke", action="store_true", help="Confirm one distant building in each arena can be dug and disappears for all three players")
    parser.add_argument("--mud-exit", choices=("under", "roof", "rescue"), help="Check a specific alternate escape from a sealed shell")
    parser.add_argument("--mode", choices=("normal", "emulated", "both"), default="both")
    parser.add_argument("--editor", action="store_true", help="Use editor game processes during iteration")
    parser.add_argument("--headless-host", action="store_true", help="Disable Metal for iteration only")
    parser.add_argument("--render-guests", action="store_true", help="Capture and verify both sides of the portal from each guest's Metal viewport")
    parser.add_argument("--stress", action="store_true", help="Exercise 300 concurrent authoritative edits in an eight-player expedition")
    parser.add_argument("--structures", action="store_true", help="Demolish all expedition structures with the digging tool and compare peer geometry")
    parser.add_argument("--neutral-kind", choices=("tree","ship","hopper","sail","tide"), help="Run one neutral interaction for diagnosis")
    parser.add_argument("--seed", type=int, help="Replay the private expedition round seed")
    parser.add_argument("--neutral-audit", action="store_true", help="Check neutral placement and routes on 64 actual-map seeds during setup")
    parser.add_argument("--profile", action="store_true", help="Add terrain edit preparation, geometry and collision profiling to all processes")
    parser.add_argument("--repro-dig", action="store_true", help="Replay the recorded D excavation and trace actual cross-sections and occluders")
    parser.add_argument("--screenshot", action="store_true", help="Capture the completed chart or the party prompt on the host")
    parser.add_argument("--interaction", action="store_true", help="Exercise movement through both portal directions and capture private treasure feedback")
    parser.add_argument("--stall-before-black", action="store_true", help="Suspend the ready host for 17 seconds before the first painted blackout")
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--map", help="Level package to host, defaults to the scenario's corresponding island")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/IslandQA")
    args = parser.parse_args()
    if args.scenario == "games-maze":
        if args.game_only not in (None, MAZE) or args.decoys == 0:
            parser.error("games-maze plays two maze rounds using one genuine chest and one decoy")
        args.game_only = MAZE
    if args.game_only == MAZE and (ROOM_CASES.get(args.scenario, (0, False))[1] or args.arena_views or
                                   args.venue_demolition or args.venue_smoke or args.arena_terrain or args.interaction):
        parser.error("Maze chase needs at least two crews and its own wall and spectator checks (--scenario games-maze)")
    if args.stall_before_black and (args.scenario not in GAME_CASES or args.scenario == "games-abort"):
        parser.error("--stall-before-black exercises a successfully loaded minigame")
    if args.scenario == "games-abort":
        if args.decoys == 0 or args.interaction:
            parser.error("games-abort exercises one recoverable false chest and must be run independently of --interaction")
        args.game_only = args.game_only or "PartyReefRun"
    if (args.arena_views or args.arena_terrain) and args.scenario not in ("games", "games-crew", "games-solo"):
        parser.error("Arena view and terrain checks require a games, games-crew or games-solo scenario")
    if args.render_guests and not ((args.interaction or args.arena_views or args.arena_terrain) and args.screenshot and not args.headless_host):
        parser.error("--render-guests requires --interaction, --arena-views or --arena-terrain with --screenshot and a rendered host")
    if args.interaction and (args.scenario not in GAME_CASES or (args.screenshot and args.headless_host)
                             or (args.game_only and args.decoys == 0)):
        parser.error("--interaction needs a games scenario, a rendered host for screenshots and a decoy in single-game mode")
    if args.identity_shots and (args.scenario != "games" or args.headless_host or not args.screenshot):
        parser.error("--identity-shots requires --scenario games --screenshot with a rendered host")
    if args.game_only and args.scenario not in GAME_CASES:
        parser.error("--game-only requires a games scenario")
    if (args.venue_demolition or args.venue_smoke) and args.scenario != "games":
        parser.error("--venue-demolition and --venue-smoke run the three-player games scenario")
    if args.neutral_audit and args.scenario not in ROOM_CASES:
        parser.error("--neutral-audit requires an expedition room scenario")
    if args.structures and args.scenario != "neutral":
        parser.error("--structures uses --scenario neutral")
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
