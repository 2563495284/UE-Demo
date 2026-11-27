#!/usr/bin/env python3
"""Exercise LAN treasure hunts and expedition rooms in independent UE processes."""

import argparse
import hashlib
import json
import os
import plistlib
import re
import signal
import shutil
import struct
import subprocess
import sys
import time
from contextlib import ExitStack
from datetime import datetime
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
VEIL_SHOTS = ("TransitionOutClosing.png", "TransitionOutBlack.png", "TransitionOutArrival.png")
TREASURE_SHOTS = ("TreasureFragment.png", "TreasureForge.png", "TreasureVault.png")
CHARACTER_CASES = {"character-pair": (2, False), "character-solo": (1, True)}
ROOM_CASES = {**GAME_CASES, **CHARACTER_CASES, "solo": (1, True), "coop": (2, True), "duel": (2, False), "crew-drop": (3, False),
              "mud": (3, False), "field": (3, False), "neutral": (3, False), "crew-eight": (8, True), "eight-teams": (8, False)}
GUESTS = [f"guest{seat}" for seat in range(1, 6)]
ROLES = ["host", *GUESTS]
FAILURE = re.compile(r"(?:VOXEL_PROTOCOL_ERROR|ISLAND_QA_CHECK passed=0|ISLAND_QA_TIMEOUT|Fatal error:).*", re.I)
MAC_SHUTDOWN_INIT = re.compile(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d+ ActDemo\[\d+:\d+\] \[UE4\] Shutdown handler: initialize\.")
CHEST_RECEIPT = (r" round=(\d+) team=(\d+) mask=(\d+) points=(\d+) key=(\d+) outcome=(\d+)"
                 r" scouted=(\d+) coordinates=(\d+) fragments=(\d+) crossing=(\d+) boosted=(\d+) event=(\d+) effects=(\d+)")
DECOY_EVENTS = ("none", "wide_tools", "scatter", "disguise", "spring_tide", "ceasefire",
                "crew_rally", "sand_ramp", "sail_gust")
ONGOING_DECOYS = {1, 3, 4, 5}


def replay_decoy(seed, chest, players, teams, winners, world_mask):
    eligible = (bool(teams & winners), bool(teams & ~winners), players >= 2, players > 0, teams.bit_count() >= 2,
                players > teams.bit_count() and bool(world_mask & 64), bool(world_mask & 128), bool(world_mask & 256))
    pool = [event for event, allowed in enumerate(eligible, 1) if allowed]
    digest = hashlib.sha1(struct.pack("<III", 0x44454359, seed & 0xffffffff, chest)).digest()
    value = int.from_bytes(digest[:4], "big")
    while True:
        value = (196314165 * value + 907633515) & 0xffffffff
        if value >= (1 << 32) % len(pool):
            return pool[value % len(pool)]


def verify_scenery_view(role, log):
    chips = re.findall(r"SCENERY_CHIPS_VISIBLE count=(\d+) outward_cm=([\d.]+)", log)
    if not any(int(count) > 0 and float(outward) > 0 for count, outward in chips):
        raise RuntimeError(f"{role} did not render moving impact fragments")
    for motion in re.finditer(r"SCENERY_FALL_MOVING actor=(\S+) subject=\S+ parts=(\d+) displacement_cm=([\d.]+) drop_cm=([\d.]+)", log):
        actor, parts, displacement, drop = motion.groups()
        if int(parts) < 2 or not 0 < float(displacement) < float(drop):
            continue
        landed = re.search(rf"SCENERY_FALL_CONTACT actor={re.escape(actor)} .*?dust=1\b", log[motion.end():])
        if not landed:
            continue
        dust = re.search(rf"SCENERY_DUST_VISIBLE actor={re.escape(actor)} age=[\d.]+ puffs=(\d+) rise_cm=([\d.]+)",
                         log[motion.end() + landed.end():])
        if dust and int(dust[1]) > 0 and float(dust[2]) > 0:
            return dict(rendered_chip_bursts=len(chips), falling_parts=int(parts),
                        observed_displacement_cm=float(displacement), drop_cm=float(drop),
                        rendered_landing_dust=True, dust_puffs=int(dust[1]), dust_rise_cm=float(dust[2]))
    raise RuntimeError(f"{role} did not render the falling lintel and its landing dust in sequence")


def verify_interaction(logs, rounds, allow_exit):
    expected = {(int(r[0]), "out") for r in rounds}
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
    aborted = set(re.findall(r"PARTY_ABORT round=(\d+)", host))
    started = re.findall(r"PARTY_START round=(\d+) chest=(\d+)", host)
    if ([entry for entry in prepared if entry[0] not in aborted] != started
            or [site for _, site in started] != [row[2] for row in rounds]
            or len({site for _, site in started}) != len(rounds)):
        raise RuntimeError("Each physical chest must start and settle once, apart from cancelled load attempts")
    completed = [round_id for round_id, _ in started]
    restored = [round_id for round_id in re.findall(r"PARTY_SWAP round=(\d+) direction=out", host) if round_id in completed]
    if restored != completed:
        raise RuntimeError("Each completed round must restore players and apply its landing exactly once")
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
        team, mask, _, _, outcome, _, _, _, _, boosted, event, effects = old
        if team == claimant:
            collectors.append(role)
            if mask != before:
                raise RuntimeError(f"{role} had the wrong pre-claim inventory")
            mask = after
        points = bin(mask).count("1")
        expected_state = (0, team, mask, points, int(points >= 3), outcome, 0, mask, points % 3, int(mask == 15), boosted, event, effects)
        if tuple(map(int, receipts[-1])) != expected_state:
            raise RuntimeError(f"{role} received incorrect progress after the direct pickup")
    if not any("VOXEL_REJECT 本队已取得这个箱子的碎片" in logs[role] for role in collectors):
        raise RuntimeError("The collecting crew did not observe its repeated claim being rejected")
    return dict(site=site, crew=claimant, before=before, after=after, after_round=last_round,
                collectors=collectors, observed_by=list(previous), duplicate_rejected=True)


def verify_chest_effects(logs, rounds, winning_masks, events, allow_exit):
    evidence = []
    for role, log in logs.items():
        if role in allow_exit:
            continue
        receipts = {int(row[0]): tuple(map(int, row[1:]))
                    for row in re.findall("EXPEDITION_GAME_RECEIPT" + CHEST_RECEIPT, log)}
        expected_sites = 0
        for number, _, site, opener, _, outcome, _ in rounds:
            n = int(number)
            if n not in receipts:
                raise RuntimeError(f"{role} missed chest receipt for round {n}")
            team, mask, points, key, _, scouted, coordinates, fragments, crossing, boosted, event, effects = receipts[n]
            won = bool(winning_masks[n] & (1 << team))
            if outcome == "true" and (team == int(opener) or won):
                expected_sites |= 1 << int(site)
            count = expected_sites.bit_count()
            expected_effects = {1: int(won), 3: 2, 4: 4, 5: 8}.get(events[n], 0)
            if ((mask, coordinates, scouted, points, fragments, key, crossing, boosted, event, effects) !=
                    (expected_sites, expected_sites, 0, count, count % 3, int(count >= 3),
                     int(count == 4), int(events[n] == 1 and won), events[n], expected_effects)):
                raise RuntimeError(f"{role} received incorrect private points, inventory or tools in round {n}: {receipts[n]}")
            if outcome == "decoy":
                evidence.append(dict(round=n, role=role, team=team, event=DECOY_EVENTS[event], effects=effects, boosted=bool(boosted), chart_mask=mask))
    return evidence


def verify_decoy_landings(host, winning_masks, events, count):
    returns = {}
    for n, seat, team, won, safe, distance in re.findall(
            r"DECOY_QA_LANDING round=(\d+) seat=(\d+) team=(\d+) won=(\d+) safe_origin=(\d+) moved_cm=([\d.]+)", host):
        n, seat, team, won, safe, distance = int(n), int(seat), int(team), int(won), int(safe), float(distance)
        scatter = events[n] == 2 and not won
        if (bool(won) != bool(winning_masks[n] & (1 << team)) or (scatter and distance < 499)
                or (not scatter and events[n] != 6 and safe and distance >= 100.)):
            raise RuntimeError(f"Unexpected decoy landing: round {n}, team {team}, winner {won}, moved {distance} cm")
        returns[n,seat] = dict(round=n, seat=seat, team=team, won=bool(won), safe_origin=bool(safe), moved_cm=distance)
    if set(returns) != {(n, seat) for n, event in events.items() if event for seat in range(count)}:
        raise RuntimeError(f"Missing decoy landing after a completed round: {sorted(returns)}")
    return list(returns.values())


def verify_decoy_return(logs, winning_masks, events, count):
    host = logs["host"]
    previous_active = events[1] if events[1] in ONGOING_DECOYS else 0
    previous_boost = winning_masks[1] if events[1] == 1 else 0
    starts = [tuple(map(int, m)) for m in re.findall(
        r"DECOY_QA_START round=(\d+) preceding_boost=(\d+) current_boost=(\d+) previous_event=(\d+) active=0", host)]
    if starts != [(1, 0, 0, 0), (2, previous_boost, 0, previous_active)]:
        raise RuntimeError(f"The next successful start did not clear the previous event: {starts}")
    retries = re.findall(r"DECOY_QA_LOAD_RETRY round=2 retained_boost=(\d+) chest=(\d+) active=(\d+)", host)
    if len(retries) != 1:
        raise RuntimeError("Expected exactly one cancelled load and retry")
    boost, chest, active = map(int, retries[0])
    if boost != previous_boost or chest != 6 or active != previous_active:
        raise RuntimeError(f"The cancelled load altered the previous effect or consumed a chest: {retries}")
    observations = []
    wait_for_hold = previous_active in (3, 4, 5)
    for role, log in logs.items():
        first = re.search(r"EXPEDITION_GAME_RECEIPT round=1 [^\n]*? effects=(\d+)", log)
        if not first:
            raise RuntimeError(f"{role} missed the original effect receipt")
        original_effects = int(first[1])
        required = ["held", "cancelled", "started"] if wait_for_hold else ["cancelled", "started"]
        stages = []
        for stage, event, effects, elapsed in re.findall(
                r"DECOY_QA_CHECKPOINT stage=(\w+) event=(\d+) effects=(\d+) elapsed=([\d.]+)", log):
            expected = (0, 0) if stage == "started" else (previous_active, original_effects)
            if ((int(event), int(effects)) != expected or stage not in required or
                    (stage == "held" and float(elapsed) < 32.)):
                raise RuntimeError(f"{role} saw an invalid {stage} checkpoint: {event}, {effects}, {elapsed}")
            if stages and stages[-1] == stage:
                continue  # A delayed acknowledgement can trigger another observation on the same peer.
            stages.append(stage)
            observations.append(dict(role=role, stage=stage, event=DECOY_EVENTS[int(event)],
                                     effects=int(effects), seconds_since_return=float(elapsed)))
        if stages != required:
            raise RuntimeError(f"{role} did not observe every event transition in order: {stages}")
    landings = verify_decoy_landings(host, winning_masks, events, count)
    world_observations = []
    for n, event in events.items():
        if event not in (6, 7, 8):
            continue
        stages = (0, 1) if event == 7 else (0,)
        reference = {}
        for role, log in logs.items():
            rows = re.findall(rf"DECOY_QA_WORLD round={n} stage=(\d+) event=(\d+) seq=(\d+) crc=(\w+) sails=(\d+) measured_cm=([-\d.]+)", log)
            world_stages = {}
            for stage, observed_event, seq, crc, sails, measure in rows:
                stage, seq, sails, measure = int(stage), int(seq), int(sails), float(measure)
                if int(observed_event) != event or stage not in stages:
                    raise RuntimeError(f"{role} saw an unexpected world effect in round {n}")
                if event == 6 and measure > 65 or event == 8 and sails < 1:
                    raise RuntimeError(f"{role} missed the returned pawn or usable sail in round {n}")
                observed = (seq, crc, sails, measure if event == 7 else None)
                if stage in reference and reference[stage] != observed:
                    raise RuntimeError(f"{role} disagrees on the terrain or cloth in round {n} stage {stage}")
                reference[stage] = observed
                world_stages[stage] = dict(stage=stage, terrain_seq=seq, terrain_crc=crc, sails=sails, measured_cm=measure)
            if tuple(world_stages) != stages:
                raise RuntimeError(f"{role} missed the physical effect or its later manual edit in round {n}")
            world_observations.append(dict(round=n, role=role, event=DECOY_EVENTS[event], stages=list(world_stages.values())))
        if event == 7:
            rise = re.search(rf"DECOY_QA_RAMP round={n} rise_cm=([\d.]+) from_chest_cm=([\d.]+)", host)
            if (not rise or float(rise[1]) < 35 or not 250 < float(rise[2]) < 1400
                    or reference[0][3] - reference[1][3] <= 8
                    or reference[1][0] != reference[0][0] + 1 or reference[1][1] == reference[0][1]):
                raise RuntimeError(f"Round {n} did not raise a standable sand ramp and let a player dig it")
        elif event == 8 and reference[0] != reference[1]:
            raise RuntimeError(f"The manual re-opening of a sail altered the terrain or number of sails in round {n}")
        elif event == 6:
            rally = {int(seat): (int(team), bool(int(target)), float(error)) for seat, team, target, error in
                     re.findall(rf"DECOY_QA_RALLY round={n} seat=(\d+) team=(\d+) target=(\d+) anchor_cm=([\d.]+)", host)}
            if set(rally) != set(range(count)) or not any(target for _, target, _ in rally.values()):
                raise RuntimeError(f"Round {n} did not gather any crew mate")
            for row in landings:
                if row["round"] != n:
                    continue
                team, target, _ = rally[row["seat"]]
                if team != row["team"] or not target and row["safe_origin"] and row["moved_cm"] >= 100:
                    raise RuntimeError(f"A stationary anchor or singleton crew changed landing in round {n}")
            if any(not 85 < distance <= 450 for _, target, distance in rally.values() if target):
                raise RuntimeError(f"A rallying crew mate missed their stationary crew anchor in round {n}")
    physical_effects = []
    for n, event in events.items():
        if event == 5:
            contact = re.search(rf"DECOY_QA_CEASEFIRE round={n} clods=0 anchored=0 ground_rise_cm=([\d.]+)", host)
            if not contact or not 35 < float(contact[1]) < 48:
                raise RuntimeError(f"Missing ceasefire contact and ground geometry evidence in round {n}")
            physical_effects.append(dict(round=n, clods=0, anchored=False, pad_height_cm=float(contact[1])))
        if event == 4:
            jump = re.search(rf"DECOY_QA_JUMP round={n} normal_cm_s=([\d.]+) actual_cm_s=([\d.]+)", host)
            if not jump or float(jump[1]) <= 0 or abs(float(jump[2]) - float(jump[1]) * 1.5) > .1:
                raise RuntimeError(f"Missing ordinary jump physics in round {n}")
            physical_effects.append(dict(round=n, normal_jump_cm_s=float(jump[1]), actual_jump_cm_s=float(jump[2])))
    return dict(starts=starts, retained_after_cancel=DECOY_EVENTS[active], landings=landings,
                peer_checkpoints=observations, physical_effects=physical_effects, world_observations=world_observations)


def verify_decoy_brush_replay(logs, winner_mask):
    expected_order = [("dig", 0), ("dig", 1), ("mud", 0), ("mud", 1)]
    applied = [(int(seq), action, int(team), int(wide)) for seq,action,team,wide in re.findall(
        r"DECOY_TOOL_USE seq=(\d+) action=(dig|mud) team=(\d+) wide=(\d)", logs["host"])]
    if [(action, wide) for _,action,_,wide in applied] != expected_order:
        raise RuntimeError(f"The four terrain edits did not use the compared tools: {applied}")
    for _, _, team, wide in applied:
        if bool(wide) != bool(winner_mask & (1 << team)):
            raise RuntimeError(f"Team {team} got an incorrect brush width")
    expected_edits = [(seq, action, wide) for seq, action, _, wide in applied]
    pattern = (r"DECOY_TOOL_REPLAY seq=(\d+) action=(dig|mud) wide=(\d) crc=([\da-fA-F]+) "
               r"center_cm=([-\d.]+) side_cm=([-\d.]+) outer_cm=([-\d.]+) width_cm=([\d.]+) travel_cm=([-\d.]+)")
    profiles = None
    for role, log in logs.items():
        values = [(int(seq), action, int(wide), crc, float(center), float(side), float(outer), float(width), float(travel))
                  for seq, action, wide, crc, center, side, outer, width, travel in re.findall(pattern, log)]
        if [row[:3] for row in values] != expected_edits or (profiles is not None and profiles != values):
            raise RuntimeError(f"{role} disagrees on brush application or the synchronized terrain profile: {values}")
        profiles = values
    reach = {(int(team), int(wide)) for team,wide in re.findall(
        r"DECOY_TOOL_OUT_OF_REACH team=(\d+) wide=([01]) rejected=1", logs["host"])}
    if reach != {(team,wide) for _,_,team,wide in applied}:
        raise RuntimeError("A brush did not exercise the unchanged out-of-reach rejection")
    return [dict(seq=seq, action=action, wide=bool(wide), crc=crc, center_cm=center, side_cm=side,
                 outer_cm=outer, measured_width_cm=width, travel_cm=travel)
            for seq,action,wide,crc,center,side,outer,width,travel in profiles]


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
            startup = logs[name].strip()
            if process.returncode == -signal.SIGSEGV and (not startup or MAC_SHUTDOWN_INIT.fullmatch(startup)):
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
    entries = {int(n) for n in re.findall(r"ARENA_VIEW_ENTRY round=(\d+) first=\d", log)}
    returns = {int(n) for n in re.findall(r"ARENA_VIEW_RETURN round=(\d+) first=\d", log)}
    for n, *rest in rounds:
        n = int(n)
        if (set(observations.get(n, {})) != {"first", "third"} or n not in entries or n not in returns
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


def punch_collision_radius(scan):
    # Radius in cm at half of the physical depression depth, from rays spaced every 10 cm.
    threshold = scan[0] / 2
    for i in range(1, len(scan)):
        if scan[i] <= threshold < scan[i-1]:
            return round(10*(i-1) + 10*(scan[i-1]-threshold)/(scan[i-1]-scan[i]), 1)
    raise RuntimeError(f"No crater edge in sampled collision {scan}")


def verify_punch_depths(scans, location, authority=None):
    # Engine traces report millimetres. Slight starting slopes and interpolated voxel
    # collision cause a small difference from the brush's depth along the surface normal.
    depths = [scan[0] / 10 for scan in scans]
    powers = [trial["power"] for trial in authority[2:]] if authority else [0, 128, 255, 255]
    targets = [40 + 40 * power / 255 for power in powers]
    if any(abs(depth - target) > 6 for depth, target in zip(depths, targets)):
        raise RuntimeError(f"{location}: measured punch depths {depths} cm, expected {targets} cm")
    light, half, full, over = depths
    if half - light < 8 or full - half < 8 or abs(full - over) > 4:
        raise RuntimeError(f"{location}: tap, half, full and overcharge did not deepen then cap: {depths} cm")
    return depths


def punch_authority_samples(host, windows, marker, observed_sky_hold):
    result = []
    for trial, (start, end) in enumerate(windows):
        # The floor report and the punch travel on different reliable actors. A sky
        # swing can reach authority after its unchanged floor was already sampled.
        # Across sky + focus-cancel require exactly the sky release (and its real hold).
        if trial == 1:
            result.append(None)
            continue
        if trial == 0:
            end = windows[1][1]
        if start not in host or end not in host:
            raise RuntimeError(f"Missing physical timing window for trial {trial}")
        segment = host.split(start, 1)[1].split(end, 1)[0]
        records = re.findall(marker, segment)
        if len(records) != 1:
            raise RuntimeError(f"Trial {trial}: expected precisely one physical authority release, saw {records}")
        power, authority, client, ping = records[0]
        power = int(power)
        if trial == 0 and abs(float(client) - observed_sky_hold) > .18:
            raise RuntimeError(f"Sky/cancel pair: server release held {client}s, guest's sky gesture lasted {observed_sky_hold}s")
        if trial == 2 and power > 70 or trial == 3 and not 86 <= power <= 170 or trial >= 4 and power != 255:
            raise RuntimeError(f"Trial {trial}: authority charged to {power}/255 after {authority}s; client held {client}s")
        result.append(dict(power=power, authority_s=float(authority), client_s=float(client), ping_ms=float(ping)))
    return result


def verify_field_punches(logs):
    common = {}
    base = None
    for role, log in logs.items():
        frames = {}
        for trial, after, seq, heights in re.findall(
                r"FIELD_PUNCH_PROFILE trial=(\d+) after=(\d) seq=(\d+) height_mm=([-\d,]+)", log):
            t, state, number = int(trial), int(after), int(seq)
            if (t, state) in frames:
                raise RuntimeError(f"{role}: duplicate island floor observation in trial {t}")
            if t == 0 and state == 0: base = number
            if base is None or number != base + max(0, t-2) + (state if t >= 2 else 0):
                raise RuntimeError(f"{role}: unexpected island punch edit sequence {number} in trial {t}")
            frames[t, state] = tuple(map(int, heights.split(",")))
        if set(frames) != {(t,a) for t in range(6) for a in (0,1)} or any(len(h) != 17 for h in frames.values()):
            raise RuntimeError(f"{role}: missing island punch collision strip")
        if common and common != frames:
            raise RuntimeError(f"{role}: punched main island floor differs across players")
        common = frames
    deltas = [[a-b for a,b in zip(common[t,0],common[t,1])] for t in range(6)]
    if any(any(abs(d)>2 for d in depths) for depths in deltas[:2]):
        raise RuntimeError("Empty or cancelled island punch changed terrain")
    taps=deltas[2:]
    widths=[punch_collision_radius(scan) for scan in taps]
    if any(abs(scan[-1]) > 8 for scan in taps):
        raise RuntimeError(f"Island punch changed the untouched outside control: {deltas}")
    if not (widths[0]+10 < widths[1] < widths[2]-10 and abs(widths[2]-widths[3])<8):
        raise RuntimeError(f"Island tap, half, full and overcharged collision radii failed: {widths}")
    inputs=re.findall(r"ARENA_PUNCH_INPUT round=81 trial=(\d+) held=([.\d]+) alpha=([.\d]+) sky=(\d) retarget=(\d) flush=(\d)", logs["guest1"])
    if len(inputs)!=6 or not all(int(row[0])==i and tuple(map(int,row[3:]))==(i==0,i==4,i==1) for i,row in enumerate(inputs)):
        raise RuntimeError("Island guest did not produce the six measured physical button gestures")
    if not (float(inputs[2][2])<.3 and .35<float(inputs[3][2])<.75 and float(inputs[4][2])==float(inputs[5][2])==1
            and float(inputs[5][1])>float(inputs[4][1])+.35):
        raise RuntimeError("Island charge display or cap did not follow the physical mouse on guest1")
    evidence = dict(shooter="guest1", peers=len(logs), half_depth_radius_cm=widths,
                    radial_offsets_cm=list(range(0, 161, 10)),
                    collision_depth_profiles_cm=[[d/10 for d in scan] for scan in taps],
                    no_edit_cases=["sky", "focus_flush"], actual_holds_seconds=[float(row[1]) for row in inputs])
    if "client_seconds=" in logs["host"]:
        # The lookahead captures power first so the two tool formats expose the same tuple.
        marker = r"EXPEDITION_PUNCH_RELEASE (?=[^\n]*?power=(\d+))seconds=([.\d]+) power=\d+ client_seconds=([.\d]+) ping_ms=([-\d.]+)"
        windows = [(f"FIELD_PUNCH_PROFILE trial={i} after=0 ", f"FIELD_PUNCH_PROFILE trial={i} after=1 ") for i in range(6)]
        evidence["authority"] = punch_authority_samples(logs["host"], windows, marker, float(inputs[0][1]))
    evidence["center_depth_cm"] = verify_punch_depths(taps, "Main island", evidence.get("authority"))
    return evidence


def verify_arena_punches(logs, rounds):
    expected = {(int(n), game) for n, game, *_ in rounds}
    shared = {}
    for role, log in logs.items():
        scans = {}
        for n, game, trial, seq, depths in re.findall(
                r"ARENA_PUNCH_PROFILE round=(\d+) game=(\w+) trial=(\d+) seq=(\d+) depth_mm=([-\d,open]+)", log):
            key, trial = (int(n), game), int(trial)
            by_trial = scans.setdefault(key, {})
            if trial in by_trial or int(seq) != 3 + max(0, trial-1):
                raise RuntimeError(f"{role}: duplicate or unexpected punch edit in {key} trial {trial}")
            profile = tuple(None if d == "open" else int(d) for d in depths.split(","))
            if len(profile) != 17:
                raise RuntimeError(f"{role}: incomplete 160 cm collision strip in {key}")
            by_trial[trial] = profile
        if set(scans) != expected or any(set(grades) != set(range(6)) for grades in scans.values()):
            raise RuntimeError(f"{role}: missing arena punch trial")
        if shared and scans != shared:
            raise RuntimeError(f"{role}: the punched collision profile differs from another participant")
        shared = scans
    inputs = {}
    shooter = "guest1" if "guest1" in logs else "host"
    for n, trial, held, alpha, sky, turned, flushed in re.findall(
            r"ARENA_PUNCH_INPUT round=(\d+) trial=(\d+) held=([.\d]+) alpha=([.\d]+) sky=(\d) retarget=(\d) flush=(\d)", logs[shooter]):
        n, trial = int(n), int(trial)
        if (n, trial) in inputs or (int(sky), int(turned), int(flushed)) != (trial == 0, trial == 4, trial == 1):
            raise RuntimeError(f"{shooter}: unexpected physical input state in round {n}, trial {trial}")
        inputs[n, trial] = (float(held), float(alpha))
    evidence = {}
    for (n, game), by_trial in shared.items():
        if any(depth is None or abs(depth) > 8 for trial in (0, 1) for depth in by_trial[trial]):
            raise RuntimeError(f"{game}: empty swing or cancelling a live charge modified the floor")
        taps = [by_trial[i] for i in range(2,6)]
        if any(scan[-1] is None or abs(scan[-1]) > 8 for scan in taps):
            raise RuntimeError(f"{game}: punch changed the floor beyond the brush: {taps}")
        holds = [inputs[n,i] for i in range(6)]
        if not (holds[2][1] < .30 and .35 < holds[3][1] < .75 and holds[4][1] == holds[5][1] == 1
                and holds[5][0] > holds[4][0]+.35):
            raise RuntimeError(f"{game}: the sender's charge display or hold cap did not match real input: {holds}")
        evidence[str(n)] = dict(feature=game, shooter=shooter, peers=len(logs),
                                release_aim_changed=True,
                                empty_swing_edits=0, cancelled_edits=0)
        if "PARTY_PUNCH_RELEASE " in logs["host"]:
            first = f"ARENA_TERRAIN_PROFILE round={n} game={game} stage=3 "
            stage = lambda i: f"ARENA_PUNCH_PROFILE round={n} game={game} trial={i} "
            windows = [(first if i == 0 else stage(i-1), stage(i)) for i in range(6)]
            seat = 1 if shooter == "guest1" else 0
            marker = rf"PARTY_PUNCH_RELEASE seat={seat} (?=[^\n]*?power=(\d+))authority_seconds=([.\d]+) client_seconds=([.\d]+) ping_ms=([-\d.]+)"
            evidence[str(n)]["authority"] = punch_authority_samples(logs["host"], windows, marker, holds[0][0])
        record = evidence[str(n)]
        centers = [scan[0] for scan in taps]
        if any(value is None for value in centers):
            # The single-cell banks are thin enough to lose their bottom at full charge.
            # Both high-power trials must show an open ray, while lighter punches retain a floor.
            if centers[0] is None or centers[1] is None or centers[2:] != [None, None]:
                raise RuntimeError(f"{game}: unexpected punch-through at the crater centers: {centers}")
            powers = [trial["power"] for trial in record["authority"][2:4]] if "authority" in record else [0, 128]
            expected_mm = [400 + 400 * power/255 for power in powers]
            if any(abs(actual - target) > 60 for actual, target in zip(centers[:2], expected_mm)) or centers[1]-centers[0]<80:
                raise RuntimeError(f"{game}: tap and half charge did not deepen as expected before punching through: {centers}")
            record["center_depth_cm"] = [value / 10 if value is not None else None for value in centers]
            record["open_center_trace_cm"] = 245
        else:
            record["center_depth_cm"] = verify_punch_depths(taps, game, record.get("authority"))
        # Use half the intended 80 cm depth for a fully pierced bank. The collision
        # edge still has a measured shoulder, even where there is no physical bottom.
        widths = [punch_collision_radius([800 if d is None else d for d in scan]) for scan in taps]
        if not (widths[0]+10 < widths[1] < widths[2]-10 and abs(widths[2]-widths[3]) <= 4):
            raise RuntimeError(f"{game}: tap/half/full/over collision radii {widths} failed")
        record["collision_radius_at_half_depth_cm"] = widths
    return evidence


def verify_arena_controls(host):
    samples = []
    for line in host.splitlines():
        if "ARENA_CONTROL_SAMPLE " in line:
            fields = dict(entry.split("=") for entry in line.split("ARENA_CONTROL_SAMPLE ", 1)[1].split())
            samples.append({field: float(value) for field, value in fields.items()})
    if [sample["pass"] for sample in samples] != [0, 1, 2]:
        raise RuntimeError("Missing control measurements before entry, inside the arena or after return")
    island = samples[0]
    if (min(abs(island["yaw"]), abs(island["pitch"])) < .1 or island["side_cm_s"] < 100
            or abs(island["body_yaw"] - 90) > 1 or island["jump_cm"] < 30
            or island["camera_x"] > -200 or island["camera_y"] < 10 or island["fov"] < 45
            or abs(island["eye_x"]) > 1 or abs(island["eye_y"]) > 1 or island["eye_z"] < 10 or island["eye_fov"] < 45):
        raise RuntimeError(f"The main island did not establish a valid walking, jumping and shoulder-camera baseline: {island}")
    tolerance = dict(yaw=.1, pitch=.1, reverse_yaw=.1, reverse_pitch=.1, side_cm_s=1, body_yaw=1, jump_cm=4,
                     camera_x=1, camera_y=1, camera_z=1, fov=.1, eye_x=1, eye_y=1, eye_z=1, eye_fov=.1)
    for sample in samples[1:]:
        for field, delta in tolerance.items():
            if abs(sample[field] - island[field]) > delta:
                raise RuntimeError(f"Control mismatch in pass {sample['pass']}: {field} {sample[field]} vs island {island[field]}")
    return dict(zip(("before_entry", "arena", "after_return"), samples))


def verify_venue_demolition(logs, rounds, all_buildings):
    count = 10 if all_buildings else 1
    checked = {}
    masks = {0, *[(1 << (i + 1)) - 1 for i in range(count)]}
    for number, feature, *_ in rounds:
        n = int(number)
        if f"EXPEDITION_VENUE_PATH round={n} count=10" not in logs["host"]:
            raise RuntimeError(f"{feature}: a structure could not be reached from the course")
        shots = re.findall(rf"EXPEDITION_VENUE_CUT round={n} feature={feature} id=(\d+) partial=1", logs["host"])
        aim = re.findall(rf"EXPEDITION_VENUE_AIM round={n} id=(\d+) seat=\d+ range_cm=(\d+)", logs["host"])
        if list(map(int, shots)) != list(range(count)) or len(aim) != count or any(int(d)>236 for _,d in aim):
            raise RuntimeError(f"{feature}: expected partial contact punches on {count} reachable buildings")
        peers = [{(int(mask),digest) for mask,digest in re.findall(
            rf"EXPEDITION_VENUE_VIEW round={n} changed=(\d+) pieces=\d+ seat=\d+ digest=(\w+)",log)} for log in logs.values()]
        if {m for m,_ in peers[0]} != masks or any(p != peers[0] for p in peers[1:]):
            raise RuntimeError(f"{feature}: replicated local cuts disagreed across peers")
        checked[feature] = {"reachable": 10, "punched": count, "peer_digests": len(peers[0])}
    return checked


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


def summarize_body_mud(logs):
    expected = {2: (3, 0), 4: (3, 1), 23: (1, 1), 25: (2, 1), 26: (1, 1), 27: (0, 0)}
    observations = {}
    for stage, (clods, anchored) in expected.items():
        agreed = re.search(rf"MUD_QA_AGREED stage={stage} peers=3 clods={clods} anchored={anchored} digest=(\w+)",
                           logs["host"])
        if not agreed:
            raise RuntimeError(f"Missing three-peer clay agreement at stage {stage}")
        digest = agreed[1]
        for role, log in logs.items():
            if f"MUD_QA_BODY_LOCAL stage={stage} clods={clods} anchored={anchored} digest={digest}" not in log:
                raise RuntimeError(f"{role} did not reproduce the shape and support state at stage {stage}")
        observations[stage] = dict(clods=clods, anchored=bool(anchored), digest=digest, peers=3)
    return observations


def verify_field_echoes(logs):
    vector = r"(-?[\d.]+),(-?[\d.]+),(-?[\d.]+)"
    expected = re.findall(r"FIELD_QA_ECHO_EXPECT trial=(\d+) site=(\d+) decoy=([01]) candidates=(\d+) "
                          r"distance_cm=([\d.]+) direction=" + vector, logs["host"])
    end = re.search(r"FIELD_QA_ECHO_END trials=(\d+)", logs["host"])
    if not end or len(expected) not in (3, 5) or len(expected) != int(end[1]):
        raise RuntimeError("The search must compare three distances to a true lid and both distances to a configured decoy")
    host_search = logs["host"].partition("FIELD_QA_ECHO_EXPECT")[2].partition("FIELD_QA_ECHO_END")[0]
    if "ISLAND_EXCAVATION_ECHO strong=" in host_search or "ISLAND_EXCAVATION_ECHO strong=" in logs["guest2"]:
        raise RuntimeError("A player other than the excavator received a treasure echo")
    guest_search = logs["guest1"].partition("MUD_QA_BUTTON right=0 pressed=1")[0]
    samples = []
    previous_by_site = {}
    for index, (trial, site, decoy, candidates, distance, *direction) in enumerate(expected):
        segment = guest_search.partition(f"FIELD_QA_ECHO_STRIKE trial={trial}")[2].partition("FIELD_QA_ECHO_STRIKE")[0]
        observed = re.findall(r"ISLAND_EXCAVATION_ECHO strong=([01]) distance_cm=([\d.]+) "
                              r"proximity=([\d.]+) direction=" + vector, segment)
        if int(trial) != index or bool(int(decoy)) != (index >= 3) or len(observed) != 1:
            raise RuntimeError(f"Expected one directional echo for excavation trial {trial}, received {observed}")
        level, actual_distance, proximity, *actual_dir = map(float, observed[0])
        alignment = sum(float(a) * b for a, b in zip(direction, actual_dir))
        if abs(float(distance) - actual_distance) > 8 or alignment < .995 or not 0 < proximity <= 1.:
            raise RuntimeError(f"Echo did not describe the nearest eligible lid on trial {trial}: {observed[0]}")
        if (index in (1, 2, 4) and
                (site not in previous_by_site or proximity <= previous_by_site[site])):
            raise RuntimeError(f"Approaching the same lid must strengthen its echo on trial {trial}")
        previous_by_site[site] = proximity
        samples.append(dict(site=int(site), decoy=bool(int(decoy)), audible_candidates=int(candidates),
                            distance_cm=actual_distance, proximity=proximity, direction=actual_dir, strong=int(level)))
    return samples


def summarize_field(logs):
    if "FIELD_QA_PEEL_AT_CHEST released=1 assisted=0" not in logs["host"]:
        raise RuntimeError("A physical click over the lid must free the worker without rejoining the lock")
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
    echoes = verify_field_echoes(logs)
    first_lid = re.search(r"FIELD_QA_FIRST_LID sight=1 body_covered=([01]) distance_cm=([\d.]+)", logs["host"])
    if not first_lid:
        raise RuntimeError("The first exposed chest must have a clear sightline before widening the pit for three players")
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
        if f"FIELD_QA_TWO_HITS worker={worker}" not in logs["host"] or f"FIELD_QA_ANCHORED_LOCK worker={role}" not in logs["host"]:
            raise RuntimeError(f"The active {role} must be able to pry with unattached torso mud and drop out when the ankle connects to the floor.")
    if "FIELD_QA_HELD physical=1 held_edits=0 retargeted=1 released_edits=1" not in logs["host"]:
        raise RuntimeError("A physical punch must hold without editing, then strike once at the release aim.")
    if ("FIELD_QA_CHARGE_CANCEL reason=chart release_edits=0" not in logs["host"]
            or not re.search(r"EXPEDITION_PUNCH_RELEASE seconds=[0-9.]+ power=255", logs["host"])):
        raise RuntimeError("The remote player must both release a capped punch and cancel a charge with the chart")
    if ("FIELD_QA_LID_HELD physical=1 exposed=1 aimed=1 locked=0" not in logs["host"]
            or "FIELD_QA_UNUSED_E held_and_released=1" not in logs["guest1"]):
        raise RuntimeError("Holding the shovel through exposure and pressing E must leave the aimed chest unopened.")
    return {"scenario": "field", "processes": 3, "charged_punch": verify_field_punches(logs), "punch_depth_cm": int(shared[3]),
            "ground_delta_cm_at_4m": int(shared[4]),
            "guest_walk_forward_cm": int(walk[1]), "guest_walk_depth_cm": int(walk[2]),
            "rally_delivered_to_crew_only": True,
            "dig_echo_samples": [sample["strong"] for sample in echoes],
            "directional_echoes": echoes,
            "visible_lid_at_reveal": {"body_still_covered": first_lid[1] == "1", "view_distance_cm": float(first_lid[2])},
            "agreed_terrain_crc": shared[2], "edit_sequence": int(shared[1]),
            "replicated_crew_role_views": sum(map(len, expected.values())),
            "chest_transitions": events, "protected_lock": True, "seize_limit": 1,
            "abandon_resets_work": True, "three_sites_with_key_rejected": True, "unspent_keys_preserved": True,
            "physical_held_retarget_and_release": True, "held_shovel_did_not_open_lid": True, "e_did_not_open_lid": True,
            "free_clay_preserves_lock": True, "anchored_leader_handoff": True, "anchored_helper_removed": True,
            "self_peel_at_lid_without_rejoining": True}


NEUTRAL_KINDS = ("Bridge", "Dam", "SandDepot", "WindTower")


def summarize_neutral(logs, only=None):
    host = logs["host"]
    routes = {}
    for index, kind in enumerate(NEUTRAL_KINDS):
        if only and kind.lower() != only:
            continue
        travel = re.search(rf"ROUTE_QA_TRAVEL kind={kind} team=(\d+) span_cm=(\d+) measured_cm=(\d+) advance_cm=(\d+) peak=([.\d]+) sections=(\d+)", host)
        if not travel or int(travel[2]) < 5499 or min(int(travel[4]), int(travel[6])) <= 0:
            raise RuntimeError(f"Missing far-end guest travel for {kind}")
        row = dict(traveller_team=int(travel[1]), span_cm=int(travel[2]), traveller_distance_cm=int(travel[3]),
                   distance_walked_or_launched_cm=int(travel[4]), peak=float(travel[5]), completed_sections=int(travel[6]))
        for state, step in (("before", 1), ("after", 90)):
            shared = re.search(rf"NEUTRAL_QA_SHARED kind={kind} state={state} peers=3 digest=(\S+)", host)
            if not shared or any(f"NEUTRAL_QA_VIEW stage={index * 100 + step} digest={shared[1]}" not in log for log in logs.values()):
                raise RuntimeError(f"Missing peer placement, state, collision or terrain observation for {kind}/{state}")
            row[state] = shared[1]
        bypass = re.search(rf"ROUTE_QA_BYPASS kind={kind} advance_cm=(\d+) peak_speed=(\d+)", host)
        if not bypass or int(bypass[1]) < 180:
            raise RuntimeError(f"Missing actual bypass walk beside {kind}")
        if f"ROUTE_QA_CONCURRENT kind={kind} prior_hits=2" not in host:
            raise RuntimeError(f"Missing simultaneous final stroke from opposing crews at {kind}")
        row["bypass_walk_cm"] = int(bypass[1])
        row["concurrent_final_stroke"] = True
        if only == "windtower":
            hops = re.findall(r"ROUTE_QA_WIND_HOP hop=(\d+) .*?horizontal_cm=([.\d]+) peak_cm=([.\d]+) error_cm=([.\d]+)", host)
            if [int(hop[0]) for hop in hops] != [1, 2, 3] or any(float(hop[3]) > 145 for hop in hops):
                raise RuntimeError("The three consecutive wind jumps must land inside their intended mouths or exit")
            row["consecutive_jumps"] = [
                dict(horizontal_cm=float(distance), peak_cm=float(peak), landing_error_cm=float(error))
                for _, distance, peak, error in hops
            ]
        routes[kind] = row
    seed = re.search(r"EXPEDITION_QA_SEED (-?\d+)", host)
    result = dict(scenario="neutral", processes=3, teams=2, seed=int(seed[1]), routes=routes)
    if not only or only == "sanddepot":
        pour = re.search(r"ROUTE_QA_POUR first=(\d+) last=(\d+) frames=(\d+) frame95_ms=([.\d]+)", host)
        if not pour:
            raise RuntimeError("Missing frame and collision sampling during the sand release")
        first, last = int(pour[1]), int(pour[2])
        collision = {}
        for role, log in logs.items():
            samples = [float(ms) for seq, ms in re.findall(r"VOXEL_SAMPLE side=\w+ seq=(\d+) action=2 ms=([.\d]+)", log)
                       if first <= int(seq) <= last]
            if not samples:
                raise RuntimeError(f"Missing sand collision samples from {role}")
            collision[role] = round(p95(samples), 3)
        result["sand_pour"] = dict(frames=int(pour[3]), host_frame_p95_ms=float(pour[4]), collision_p95_ms=collision)
    return result


def summarize_neutral_audit(host):
    rows = re.findall(r"NEUTRAL_AUDIT_SEED seed=(\d+) count=4 kinds=15 regions=15 hash=(\w+) replay=1 routes=1 items=(\S+) min_span_cm=(\d+) max_length_cm=(\d+)", host)
    done = re.search(r"NEUTRAL_AUDIT_DONE seeds=64 passed=1 full_seed=(-?\d+) bridge=64 dam=64 sand=64 wind=64", host)
    if len({r[0] for r in rows}) != 64 or not done:
        raise RuntimeError("Every seed must produce four replayable, usable routes")
    seeds = []
    for seed, digest, items, shortest, longest in rows:
        entries = re.findall(r"(\d+)@([A-D])-([A-D])/(\d+)", items)
        if len(entries) != 4 or int(shortest) < 5500 or int(longest) > 7500:
            raise RuntimeError(f"Invalid route count or travel span on seed {seed}")
        seeds.append(dict(seed=int(seed), digest=digest, min_span_cm=int(shortest), max_length_cm=int(longest),
                          routes=[dict(kind=NEUTRAL_KINDS[int(k)], start=a, end=b, length_cm=int(n)) for k,a,b,n in entries]))
    return dict(seeds=seeds, full_seed=int(done[1]), per_kind=dict.fromkeys(NEUTRAL_KINDS, 64))


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
        if stages != [("close","out"), ("black","out"), ("open","out"), ("clear","out")]:
            raise RuntimeError(f"{role} observed unexpected blackout stages during the cancelled load: {stages}")
    attempts = re.findall(r"PARTY_PREPARE round=[12] chest=(\d+)", host)
    settled = re.findall(r"PARTY_CHEST_SETTLED round=(\d+) site=(\d+) outcome=(\d+) opener=\d+ winner=\d+ rewarded=(\d+)", host)
    if (len(attempts) != 2 or attempts[0] != attempts[1] or int(attempts[0]) <= 4 or len(settled) != 1
            or settled[0] != ("2", attempts[0], "4", "0")):
        raise RuntimeError("The two attempts did not settle the same false chest exactly once")
    moved = re.search(r"PARTY_ABORT_WALK target=guest cm=([\d.]+) unlocked=1", guest)
    if not moved or float(moved[1]) < 25.:
        raise RuntimeError("Guest movement did not recover after the cancelled load")
    return dict(scenario="games-abort", processes=2, cancelled_before_pawn_swap=True,
                guest_recovered_movement_cm=float(moved[1]), retried_chest=int(attempts[0]),
                cancelled_rewards=0, successful_retry_settlements=1)


def verify_party_briefings(logs, args, shot_output, directory, guests):
    host = logs["host"]
    pages = re.findall(r"PARTY_BRIEFING round=(\d+) feature=(\w+) players=(\d+)", host)
    starts = re.findall(r"PARTY_BRIEFING_START round=(\d+) agreed=(\d+) players=(\d+) forced=([01])", host)
    aborted = set(re.findall(r"PARTY_ABORT round=(\d+)", host))
    pages = [row for row in pages if row[0] not in aborted]
    if not pages or [row[0] for row in pages] != [row[0] for row in starts] or len(starts) != len({s[0] for s in starts}):
        raise RuntimeError("Every completed briefing must lead to exactly one start")

    def marker(log, text):
        match = re.search(r"^\[([\d.\-:]+)\][^\n]*? " + re.escape(text) + r"(?=\s|$)", log, re.MULTILINE)
        if not match:
            raise RuntimeError(f"Missing briefing event {text}")
        return datetime.strptime(match[1], "%Y.%m.%d-%H.%M.%S:%f")

    results = []
    for (round_id, feature, initial), (_, agreed, active, forced) in zip(pages, starts):
        force = args.briefing == "force"
        departed = args.briefing == "drop" and round_id == "1"
        if int(active) != int(initial) - departed or int(forced) != int(force) or int(agreed) != (0 if force else int(active)):
            raise RuntimeError(f"Unexpected briefing start: {round_id=} {agreed=} {active=} {initial=} {forced=}")
        ready = marker(host, f"PARTY_BRIEFING_READY round={round_id}")
        consent = marker(host, f"PARTY_BRIEFING_{'FORCE' if force else 'CONSENSUS'} round={round_id}")
        swap = marker(host, f"PARTY_SWAP round={round_id} direction=in")
        start = marker(host, f"PARTY_BRIEFING_START round={round_id}")
        if start < max(ready, consent) or (args.slow_briefing_load and not departed and consent >= ready):
            raise RuntimeError(f"Incorrect load/consent ordering in briefing {round_id}")
        if args.long_read and round_id == "1" and (consent - ready).total_seconds() < 15:
            raise RuntimeError("Host did not remain on the technically ready page past the asset timeout")
        for role in ["host", *guests]:
            if args.briefing == "drop" and role == guests[-1] and round_id != "1":
                continue
            log = logs[role]
            if f"PARTY_BRIEFING_PAGE round={round_id} feature={feature} host={int(role == 'host')}" not in log:
                raise RuntimeError(f"{role} missed briefing {round_id} for {feature}")
            votes = re.findall(rf"PARTY_BRIEFING_INPUT round={round_id} forced=(\d) slate=(\d)", log)
            should_press = (not force or role == "host") and not (departed and role == guests[-1])
            rendered = not args.headless_host if role == "host" else args.render_guests
            presented = marker(log, f"PARTY_BRIEFING_PRESENTED round={round_id} rendered={int(rendered)}")
            if not (departed and role == guests[-1]):
                loading = marker(log, f"PARTY_FEATURE_BEGIN round={round_id}")
                if loading < presented or re.search(rf"PARTY_VEIL stage=\w+ round={round_id} direction=in\b", log):
                    raise RuntimeError(f"{role} exposed an entry transition or loaded before the briefing in round {round_id}")
            elif args.briefing == "drop" and f"PARTY_FEATURE_ACTIVE round={round_id} success=1" in log:
                raise RuntimeError("Departing guest had already finished loading")
            if votes != ([(str(int(force)), str(int(rendered)))] if should_press else []):
                raise RuntimeError(f"{role} submitted an unexpected input in briefing {round_id}: {votes}")
            if rendered:
                paint = re.search(rf"PARTY_BRIEFING_PAINT round={round_id} feature={feature} screen=(\d+)x(\d+) viewport=(\d+)x(\d+)", log)
                if not paint:
                    raise RuntimeError(f"{role} missed the first painted briefing in round {round_id}")
                w, h, vw, vh = map(int, paint.groups())
                if min(w, h, vw, vh) < 1 or abs(w / vw - h / vh) > .01:
                    raise RuntimeError(f"{role} briefing does not fit its viewport: {paint[0]}")
                if f"PARTY_BRIEFING_VIDEO feature={feature} round={round_id} " not in log:
                    raise RuntimeError(f"{role} did not decode the tutorial for {feature} in round {round_id}")
                name = f"Brief-{round_id}-{feature}.png"
                path = shot_output / name if role == "host" else shot_output / role / name
                destination = directory / name if role == "host" else directory / role / name
                if not path.is_file():
                    raise RuntimeError(f"Missing {role} briefing screenshot: {path}")
                from PIL import Image
                with Image.open(path) as picture:
                    rgb = picture.convert("RGB")
                    # Both outside edges must show the same opaque page from top to bottom.
                    edges = [rgb.getpixel((int(rgb.width*x), int(rgb.height*y)))
                             for x in (.005, .995) for y in (.1, .4, .7, .95)]
                    if any(max(channel) - min(channel) > 3 for channel in zip(*edges)):
                        raise RuntimeError(f"{role} briefing leaves scenery or HUD visible along a viewport edge")
                if path != destination:
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
                if args.slow_briefing_load and not departed and role == "host":
                    waiting = path.with_name(f"Waiting-{round_id}-{feature}.png")
                    if not waiting.is_file():
                        raise RuntimeError(f"Host did not display the accepted start while the slow guest loaded: {waiting}")
                    if waiting.parent != destination.parent:
                        shutil.copy2(waiting, destination.parent / waiting.name)
        voters = re.findall(rf"PARTY_BRIEFING_AGREE round={round_id} player=(\d+)", host)
        waiting_votes = sum(marker(host, f"PARTY_BRIEFING_AGREE round={round_id} player={voter}") < ready for voter in voters)
        results.append(dict(round=int(round_id), feature=feature, players=int(active), agreed=int(agreed), forced=force,
                            votes_before_technical_readiness=waiting_votes,
                            consent_to_start_ms=round((start-consent).total_seconds()*1000),
                            scene_wait_after_consent_ms=round(max(0, (swap-consent).total_seconds())*1000),
                            pawn_wait_after_consent_ms=round(max(0, (ready-max(consent, swap)).total_seconds())*1000),
                            ready_to_start_ms=round((start-ready).total_seconds()*1000)))
    return results


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
    if args.scenery_replay and args.screenshot:
        shared.remove("-nosound")
        shared.append("-ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0")
    if args.profile:
        shared.append("-VoxelProfile")
    room = ROOM_CASES.get(args.scenario)
    guests = GUESTS
    if room:
        count, together = room
        guests = [f"guest{seat}" for seat in range(1, count)]
        shared.append(f"-ExpeditionPlayers={count}")
        if args.decoy_events:
            shared.append("-ExpeditionDecoyQA")
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
            if args.arena_controls:
                shared.append("-ExpeditionArenaControlsQA")
            if args.game_only:
                shared.append("-ExpeditionGameOnly=" + args.game_only)
            if args.scenario == "games-drop":
                shared.append("-ExpeditionGamesDropQA")
            if args.scenario == "games-abort":
                shared.append("-ExpeditionAbortQA")
        if args.scenario in CHARACTER_CASES:
            shared.append("-CharacterSelectionQA")
        if args.scenario == "mud":
            shared.append("-ExpeditionMudQA")
        if args.scenario == "neutral":
            shared.append("-ExpeditionNeutralQA")
            if args.structures:
                shared.append("-NeutralDemolitionQA")
            if args.scenery_replay:
                shared.append("-SceneryReplayQA")
            if args.neutral_kind:
                shared.append(f"-NeutralOnlyKind={tuple(k.lower() for k in NEUTRAL_KINDS).index(args.neutral_kind)}")
        if args.scenario == "field":
            # Keep the three lock participants in the same sightline fixture on both networks.
            shared.append("-ExpeditionFieldQA")
            if args.seed is None:
                shared.append("-ExpeditionSeed=1872304730")
    elif args.scenario != "hunt":
        shared.append("-IslandPartyQA")
    if args.scenario == "party-timeout":
        shared.append("-IslandPartyTimeout")
    shot_output = directory
    if (((args.scenario in ("field", "neutral", "mud") or args.scenario in GAME_CASES) and args.screenshot)
            or args.briefing) and not args.editor:
        with (args.app / "Contents/Info.plist").open("rb") as stream:
            bundle = plistlib.load(stream)["CFBundleIdentifier"]
        shot_output = Path.home() / "Library/Containers" / bundle / "Data/tmp/ExpeditionField" / str(time.time_ns())
    if args.interaction and args.screenshot and args.editor:
        for name in (*VEIL_SHOTS, *TREASURE_SHOTS, "TreasureFalseChart.png", "TreasureVaultOpened.png"):
            (shot_output / name).unlink(missing_ok=True)
    if args.render_guests:
        for guest in guests:
            (shot_output / guest).mkdir(parents=True,exist_ok=True)
            for name in VEIL_SHOTS:
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
            if args.long_read and role == "host":
                scenario_flags.append("-PartyBriefReadQA")
            if (args.slow_briefing_load or args.briefing == "drop") and role == guests[-1]:
                scenario_flags.append("-PartyBriefSlowLoadQA")
            if args.briefing:
                scenario_flags.extend(["-PartyBriefInspectQA", f"-PartyBriefShots={shot_output if role == 'host' else shot_output / role}"])
                if role == "guest1" and args.render_guests:
                    profile = shot_output / role / "profile"
                    ini = profile / "Saved/Config" / ("MacEditor" if args.editor else "Mac") / "GameUserSettings.ini"
                    ini.parent.mkdir(parents=True, exist_ok=True)
                    ini.write_text("[/Script/ActDemo.IslandClientSettings]\nLanguage=en\n", encoding="utf-8")
                    scenario_flags.append(f"-UserDir={profile}")
                if args.briefing == "force":
                    scenario_flags.append("-PartyBriefForceQA")
                if args.briefing == "drop" and role == guests[-1]:
                    scenario_flags.append("-PartyBriefHoldQA")
            if role == "host" and args.route_scenery_audit:
                scenario_flags.append("-RouteSceneryAudit")
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
            # Multiple editor Metal viewports can compile shaders before the first map frame.
            load_timeout = 240 if args.editor and args.scenery_replay and args.screenshot else 120
            initialized = wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs["host"], load_timeout, "Host map did not initialize")
            # UE may choose another port if a previous or concurrent playtest is still releasing the requested one.
            bound_port = re.search(r"IpNetDriver listening on port (\d+)", initialized["host"])
            if not bound_port:
                raise RuntimeError("The LAN host did not report a listening port")
            address = f"{ip}:{bound_port[1]}"
            for guest in guests:
                launch(guest, address)
                if args.scenario != "neutral":
                    wait_for(processes, lambda logs: "VOXEL_INITIAL crc=" in logs[guest], 220 if args.editor else 100, f"{guest} did not join and load terrain")
            if args.scenario == "neutral":
                # The room assigns crews after both guests are ready. All observations are by
                # in-game seat, so simultaneous slow editor starts do not affect their identity.
                wait_for(processes, lambda logs: all("VOXEL_INITIAL crc=" in logs[g] for g in guests), load_timeout, "Neutral guests did not load the terrain")
            print(f"{mode}: {len(guests) + 1} independent processes joined {address}", flush=True)
            if args.stall_during_load:
                intent = "FORCE" if args.briefing == "force" else "CONSENSUS"
                logs = wait_for(processes, lambda current: f"PARTY_BRIEFING_{intent} round=1" in current["host"],
                                150, "The host did not accept the early start")
                if "PARTY_BRIEFING_READY round=1" in logs["host"]:
                    raise RuntimeError("The guest was already ready before the planned server pause")
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
                    def ready_to_drop(logs):
                        if args.briefing == "drop":
                            return ("PARTY_BRIEFING_PAGE round=1 " in logs[guests[-1]]
                                    and all("PARTY_BRIEFING_INPUT round=1 " in logs[role] for role in ["host", *guests[:-1]]))
                        return "EXPEDITION_GAMES_DROP_READY" in logs["host"]
                    wait_for(processes, ready_to_drop, 150,
                             "The first arena did not open for the departing guest")
                    os.killpg(processes[guests[-1]][0].pid, signal.SIGTERM)
                    allow_exit = (guests[-1],)
                done_marker = "EXPEDITION_GAMES_DONE rounds="
            if args.scenario in CHARACTER_CASES:
                done_marker = "CHARACTER_PROOF_DONE"
            if args.repro_dig:
                done_marker = "EXPEDITION_QA_REPRO_DONE"
            if args.scenario == "mud":
                done_marker = "MUD_QA_DONE"
            if args.scenario == "field":
                done_marker = "FIELD_QA_DONE players=3"
            if args.scenario == "neutral":
                done_marker = f"NEUTRAL_QA_DONE players=3 teams=2 kinds={1 if args.neutral_kind else len(NEUTRAL_KINDS)}"
            if args.structures:
                done_marker = "SCENERY_QA_DONE"
            if args.scenery_replay:
                done_marker = "SCENERY_REPLAY_DONE"
            if args.scenario == "crew-drop":
                wait_for(processes, lambda logs: "EXPEDITION_QA_DROP_READY" in logs["host"], 45, "Three crews did not launch")
                os.killpg(processes[guests[-1]][0].pid, signal.SIGTERM)
                allow_exit = (guests[-1],)
                done_marker = "EXPEDITION_QA_DROP_DONE launched=3 remaining=2"
            timeout = 480 if args.scenario == "games-maze" and args.screenshot else 300
            if args.scenario == "solo":
                # This hunt waits for four distinct minigames and also excavates the vault.
                timeout = 600
            elif args.scenario in GAME_CASES and not args.game_only:
                decoys = args.decoys if args.decoys is not None else 2
                timeout = (130 if args.arena_terrain else 95) * (5 + decoys)
            logs = wait_for(processes, lambda logs: done_marker in logs["host"], timeout,
                            "The multiplayer scenario did not complete", allow_exit)
            if args.scenario in CHARACTER_CASES:
                expected = set(range(15))
                if count == 1:
                    expected -= {6, 7}  # A solo host has no withdrawing guest during countdown.
                observed = {}
                physical = {}
                for role, log in logs.items():
                    matches = re.findall(r"CHARACTER_PROOF_VIEW stage=(\d+) host=(\d+) guest=(-?\d+) phase=\d+ live_meshes=(\d+)", log)
                    rows = {int(n): (int(host), int(guest)) for n, host, guest, _ in matches}
                    physical[role] = {int(n): int(meshes) for n, _, _, meshes in matches}
                    close_stages = {0, 1, 4, 8, 9, 10, 12, 13, 14}
                    if any(physical[role].get(n) != count for n in close_stages):
                        raise RuntimeError(f"{role} did not observe every nearby party member's real mesh: {physical[role]}")
                    if set(rows) != expected:
                        raise RuntimeError(f"{role} did not observe every expected PlayerState and physical character mesh: {rows}")
                    observed[role] = rows
                if any(rows != observed["host"] for rows in observed.values()):
                    raise RuntimeError("The room peers observed different character models")
                result = dict(scenario=args.scenario, processes=count, observations=observed, bound_meshes=physical)
            elif args.repro_dig:
                result = {"scenario": "d-shaft-repro", "position_xy": [1830, -4673], "depth_below_surface_cm": 150, "exposed_and_opened": True}
            elif args.scenario == "mud":
                result = {"scenario": "mud", "processes": 3, "teams": 2, "body_mud": summarize_body_mud(logs)}
                for label in ("WALL_OCCLUSION", "CLIPPED", "RESCUE", "PIT", "STEPS", "EXIT", "BUTTONS"):
                    match = re.search(r"MUD_QA_" + label + r" (.*)", logs["host"])
                    if not match:
                        raise RuntimeError(f"Missing {label} evidence")
                    result[label.lower()] = match[1].strip()
                shaft = logs["host"].split("MUD_QA_STEP stage=7", 1)[1].split("MUD_QA_STEP stage=9", 1)[0]
                launches = shaft.count("MUD_QA_THROW_POSE ")
                pads = len(re.findall(r"VOXEL_SAMPLE side=host seq=\d+ action=2\b", shaft))
                if not 0 < pads <= launches:
                    raise RuntimeError(f"Missing physical throws or committed soil in the shaft: {launches} throws, {pads} pads")
                result["shaft_throws"] = launches
                result["shaft_placed_pads"] = pads
                shared = logs["host"].split("MUD_QA_STEP stage=17", 1)[1].split("MUD_QA_STEP stage=18", 1)[0]
                if shared.count("EXPEDITION_GAS_USE action=Expedition.Action.Throw ") < 2 or "EXPEDITION_GAS_USE action=Expedition.Action.Dig " in shared:
                    raise RuntimeError("A held punch must charge silently while right-button throws repeat")
                continued = logs["host"].split("MUD_QA_STEP stage=18", 1)[1].split("MUD_QA_STEP stage=19", 1)[0]
                if continued.count("EXPEDITION_GAS_USE action=Expedition.Action.Dig ") != 1:
                    raise RuntimeError("Releasing the punch must make exactly one strike")
                if continued.count("EXPEDITION_GAS_USE action=Expedition.Action.Throw ") < 2:
                    raise RuntimeError("Releasing dig interrupted held throw")
                if args.screenshot:
                    views = ["MudChest.png", "MudWall.png", "MudFoot.png", "MudDouble.png", "MudFreed.png"]
                    for role in (["host", *guests] if args.render_guests else ["host"]):
                        folder = Path() if role == "host" else Path(role)
                        shots = views + (["MudSteps.png"] if role == "host" else [])
                        (directory / folder).mkdir(parents=True, exist_ok=True)
                        for shot in shots:
                            source = shot_output / folder / shot
                            if not source.is_file():
                                raise RuntimeError(f"Missing {role} {shot}")
                            if shot_output != directory:
                                shutil.copy2(source, directory / folder / shot)
                        result[f"{role}_screenshots"] = [str(folder / shot) for shot in shots]
            elif args.scenario == "crew-drop":
                result = {"scenario": args.scenario, "launched_crews": 3, "remaining_players": 2, "original_berths_preserved": True}
            elif args.scenery_replay:
                mud = {role: set(re.findall(r"SCENERY_REPLAY_RAY stage=2 observation=(mud:\w+:\d+)", log))
                       for role, log in logs.items()}
                holes = {role: set(re.findall(r"SCENERY_REPLAY_RAY stage=5 observation=(\S+)", log))
                         for role, log in logs.items()}
                bore = re.search(r"SCENERY_REPLAY_BORE strokes=(\d+)", logs["host"])
                release = re.search(r"SCENERY_REPLAY_RELEASED cuts=(\d+)", logs["host"])
                if (not set.intersection(*mud.values()) or "clear" not in set.intersection(*holes.values())
                        or not bore or not release or "SCENERY_REPLAY_FOOT supported=1" not in logs["host"]):
                    raise RuntimeError("The peers did not confirm the wall deposit, standing tread, through-hole and falling lintel")
                result = dict(scenario="scenery-replay", mud_contact=sorted(set.intersection(*mud.values())),
                              through_hole_strokes=int(bore[1]), support_strokes=int(release[1]))
                if args.screenshot:
                    from expedition_ui_qa import encode_replay, recording_encoder
                    encoder = recording_encoder()
                    clips = {}
                    views = {}
                    for role in (["host", *guests] if args.render_guests else ["host"]):
                        source = shot_output if role == "host" else shot_output / role
                        dest = directory / role
                        dest.mkdir(exist_ok=True)
                        views[role] = verify_scenery_view(role, logs[role])
                        clips[role] = [encode_replay(source, clip, dest, encoder, audio=source / (clip + ".wav"))
                                       for clip in ("Mud", "Impact", "Collapse")]
                    result["viewport_clips"] = clips
                    result["rendered_interactions"] = views
            elif args.structures:
                names = ("Cairn", "TrailBeacon", "Arch", "SignalNeedles", "Quarry", "Fin", "Dock",
                         *NEUTRAL_KINDS, "FanPalm", "Saltbush", "SplitReef", "CoralSpur", "Chest")
                done = re.search(r"SCENERY_QA_DONE classes=(\d+) strikes=(\d+) peers=3", logs["host"])
                inventory = {name: dict(actors=int(actors), fields=int(fields)) for name, actors, fields in re.findall(
                    r"SCENERY_VOXEL_INVENTORY class=(\w+) actors=(\d+) fields=(\d+)", logs["host"])}
                cases = {name: dict(initial=int(initial), final=int(final), cuts=int(cuts), partial=int(partial),
                                    different=int(different), repeated=int(repeated))
                         for name, initial, final, cuts, partial, different, repeated in re.findall(
                             r"SCENERY_VOXEL_CASE class=(\w+) initial=(\d+) final=(\d+) cuts=(\d+) partial=(\d+) different=(\d+) repeated=(\d+)", logs["host"])}
                if not done or int(done[1]) != len(names) or set(inventory) != set(names) or set(cases) != set(names):
                    raise RuntimeError("Missing a voxel inventory or physical shovel observation for a scenery class")
                if inventory["Dock"]["actors"] != 8 or sum(inventory[name]["actors"] for name in names[:2]) != 48:
                    raise RuntimeError("The eight landings and 48 trail markers must all have voxel fields")
                if any(row["initial"] <= row["final"] or not row["cuts"] for row in cases.values()):
                    raise RuntimeError("A scenery sample failed to lose occupied material to a normal shovel stroke")
                route_names = names[11:15]
                route = [cases[name] for name in route_names]
                if any(row["final"] or not row["partial"] for row in route) or cases["Cairn"]["final"]:
                    raise RuntimeError("Each new shape must survive a local cut and be completely excavatable")
                if not any(r["different"] for r in route) or not any(r["repeated"] for r in route):
                    raise RuntimeError("No evidence of excavation at distinct points and repeated strikes along the same ray")
                opened = {name for name, a, b in re.findall(
                    r"SCENERY_VOXEL_CUT class=(\w+) from=\d+ to=\d+ depth_before=(-?\d+) depth_after=(-?\d+)", logs["host"])
                          if int(a) >= 0 and (int(b) < 0 or int(b) > int(a) + 1)}
                if not set(route_names).issubset(opened):
                    raise RuntimeError("One of the four new shapes did not open physical space behind a hit")
                shared = re.findall(r"SCENERY_VOXEL_SHARED class=\w+ stage=(\d+) observed=(\S+) peers=3", logs["host"])
                if len(shared) != int(done[2])+len(names):
                    raise RuntimeError("The host did not compare the initial surface and every changed surface")
                impact_pattern = re.compile(r"SCENERY_CHIPS_RECEIVED surface=(\d+) tint=([0-9A-F]{8})")
                impacts = impact_pattern.findall(logs["host"])
                if len(impacts) < int(done[2]):
                    raise RuntimeError("At least one accepted shovel cut did not emit impact feedback")
                for role, log in logs.items():
                    observed = set(re.findall(r"SCENERY_VOXEL_VIEW stage=(\d+) observed=(\S+)", log))
                    if not set(shared).issubset(observed):
                        raise RuntimeError(f"{role} missed a surface's occupancy, rendered triangles or collision trace")
                    if impact_pattern.findall(log) != impacts:
                        raise RuntimeError(f"{role} received a different sequence of shovel impact materials or tints")
                result = dict(scenario="structures", processes=3, inventory=inventory, samples=cases,
                              strikes=int(done[2]), synchronized_observations=len(shared), synchronized_chip_events=len(impacts))
                if args.route_scenery_audit:
                    audit = re.search(r"ROUTE_SCENERY_AUDIT seeds=64 pass=64 minimum=(\d+) maximum=(\d+) docks=8 landmarks=4 lanes=(\d+) sightlines=(\d+) decoys=(\d+)", logs["host"])
                    if not audit or min(int(audit[i]) for i in range(1, 5)) <= 0:
                        raise RuntimeError("Missing successful 64-seed route, sightline and excavation audit")
                    result["route_audit"] = dict(seeds=64, minimum_sites=int(audit[1]), maximum_sites=int(audit[2]),
                                                  protected_lanes=int(audit[3]), landmark_sightlines=int(audit[4]), decoys=int(audit[5]))
            elif args.scenario in ("field", "neutral"):
                result = summarize_field(logs) if args.scenario == "field" else summarize_neutral(logs, args.neutral_kind)
                if args.screenshot:
                    shots = [f"Field{name}.png" for name in ("01Surface", "02Dig", "03Assist", "04Seized")]
                    if args.scenario == "neutral":
                        shots = [f"Neutral{kind}{state}.png" for kind in NEUTRAL_KINDS for state in ("Before","After") if not args.neutral_kind or kind.lower() == args.neutral_kind]
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
                events = {int(n): int(event) for n, event in re.findall(
                    r"EXPEDITION_GAME_RETURN round=(\d+) [^\n]*? event=(\d+)", logs["host"])}
                settlements = [(int(chest), int(outcome)) for chest, outcome in re.findall(
                    r"PARTY_CHEST_SETTLED round=\d+ site=(\d+) outcome=(\d+)", logs["host"])]
                draws = [tuple(map(int, row)) for row in re.findall(
                    r"ISLAND_DECOY_DRAW round=\d+ chest=(\d+) event=(\d+) players=(\d+) crews=(\d+) winners=(\d+) world_mask=(\d+)", logs["host"])]
                if ([(chest, event) for chest, event, *_ in draws] !=
                        [(int(row[2]), events.get(int(row[0]))) for row in rounds if row[5] == "decoy"]):
                    raise RuntimeError("A false chest drew repeatedly, skipped the draw, or changed its result")
                seed_record = re.search(r"EXPEDITION_QA_SEED (-?\d+)", logs["host"])
                for chest, event, players, teams, winners, world_mask in draws:
                    if not seed_record or replay_decoy(int(seed_record[1]), chest, players, teams, winners, world_mask) != event:
                        raise RuntimeError(f"Chest {chest} did not match the eligible seeded draw: {event=}, {world_mask=}")
                if args.expect_decoy_event and DECOY_EVENTS[events[1]] != args.expect_decoy_event:
                    raise RuntimeError(f"Expected first draw {args.expect_decoy_event}, got {DECOY_EVENTS[events[1]]}; actual draws: {draws}")
                if (len(settlements) != len(rounds) or len(events) != len(rounds)
                        or [chest for chest, _ in settlements] != [int(row[2]) for row in rounds]
                        or any(event not in range(len(DECOY_EVENTS)) for event in events.values())
                        or any((events[int(row[0])] == 0) != (row[5] == "true") for row in rounds)):
                    raise RuntimeError("Expected one event per settled false chest and none for true chests or failed loads")
                games = {row[1] for row in rounds}
                if not rounds or any(game not in VOXEL_GAMES for game in games):
                    raise RuntimeError("A completed arena and its reveal are missing")
                if args.game_only:
                    if len(rounds) != (2 if args.scenario == "games-maze" or args.decoy_events else 1) or games != {args.game_only}:
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
                    if outcome not in ("true", "decoy") or int(awarded) != credited:
                        raise RuntimeError(f"Unexpected credit mask at round {n}: got {awarded}, expected {credited}")
                for role, log in logs.items():
                    if role in allow_exit:
                        continue
                    venues = [int(n) for n in re.findall(r"PARTY_VENUE_VISIBLE round=(\d+)", log)]
                    if args.decoy_events:
                        if venues not in ([1, 3], [1, 2, 3]):
                            raise RuntimeError(f"{role} missed a completed arena around the cancelled load: {venues}")
                    elif len(venues) != len(rounds):
                        raise RuntimeError(f"{role} did not stream every selected map")
                if set(re.findall(r"EXPEDITION_STONE_DUG feature=(\w+)", logs["host"])) != games - {MAZE}:
                    raise RuntimeError("A played arena did not allow excavation of its stone foundation")
                result = {"scenario": args.scenario, "processes": count, "departed": len(allow_exit),
                          "games": [dict(round=int(n), feature=g, site=int(site), opener=int(o), winner=int(w),
                                         result=kind, event=DECOY_EVENTS[events[int(n)]], winning_team_mask=winning_masks[int(n)], credited_team_mask=int(teams))
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
                    if set(per_game) != games:
                        raise RuntimeError(f"{role} missed a played arena")
                    for index, (_, board) in enumerate(settled):
                        name, _, _, edits, winner, scores = board
                        if name == MAZE:
                            continue
                        zero_round = args.decoy_events and index == 1
                        if (winner < 0) != zero_round or (not zero_round and edits == 0):
                            raise RuntimeError(f"{role} round {index+1} unexpected edits or winner: {board}")
                        if zero_round and any(int(entry.split(":")[1]) for entry in scores.split(",")):
                            raise RuntimeError(f"{role} zero-score round recorded points: {scores}")
                    if snapshots and snapshots != per_game:
                        raise RuntimeError(f"{role} and host disagree on the settled terrain, score or winner")
                    snapshots = per_game
                result["replicated_boards"] = snapshots
                revisit = verify_chest_revisit(logs, rounds, allow_exit, args.scenario != "games-maze")
                if revisit:
                    result["direct_chest_pickup"] = revisit
                if args.scenario == "games-maze":
                    result["decoy_landings"] = verify_decoy_landings(logs["host"], winning_masks, events, count)
                    if "MAZE_QA_DEFAULT limit=180" not in logs["host"]:
                        raise RuntimeError("The unmodified chase duration was not verified")
                    if args.screenshot and not re.search(r"MAZE_CHASE_START [^\n]*seconds=180\b", logs["host"]):
                        raise RuntimeError("The photographed round did not run the full three-minute pursuit clock")
                    if winning_masks[1].bit_count() != 1 or winning_masks[2].bit_count() != 2:
                        raise RuntimeError("Expected one winning evader crew, then two winning hunter crews")
                    layouts = {}
                    wall_profiles = {}
                    for role, log in logs.items():
                        views = {(int(n), int(stage)) for n, stage in re.findall(r"MAZE_QA_VIEW round=(\d+) stage=(\d+)", log)}
                        if views != {(n,s) for n in (1,2) for s in (1,3,4,6,10,16)}:
                            raise RuntimeError(f"{role} missed a wall collision update or capture state")
                        profiles = {(int(n),int(s)): h for n,s,h in re.findall(
                            r"MAZE_COLLISION_PROFILE round=(\d+) stage=(\d+) hash=(\w+) rays=117", log)}
                        if set(profiles) != {(n,s) for n in (1,2) for s in (1,3,4,10,16)} or wall_profiles and profiles!=wall_profiles:
                            raise RuntimeError(f"{role} disagreed on the sampled maze wall or hole geometry")
                        wall_profiles=profiles
                        peer_layouts = {n: (seed, digest) for n, seed, digest in re.findall(
                            r"MAZE_QA_LAYOUT round=(\d+) seed=(-?\d+) hash=(\w+) reachable=81", log)}
                        if (set(peer_layouts) != {"1", "2"} or peer_layouts["1"][1] == peer_layouts["2"][1]
                                or layouts and peer_layouts != layouts):
                            raise RuntimeError(f"{role} missed a connected layout or disagrees with another peer's generated walls")
                        layouts = peer_layouts
                    result["maze_layouts"] = layouts
                    result["maze_wall_profiles"] = {f"{n}.{s}":hash_value for (n,s),hash_value in wall_profiles.items()}
                    all_views = "\n".join(logs.values())
                    for seed, _ in layouts.values():
                        swings = set(re.findall(rf"MAZE_QA_TOOL seed={seed} place=([01]) target=(\w+)", all_views))
                        if not {("1", "wall"), ("0", "wall"), ("1", "gap"), ("1", "runner")} <= swings:
                            raise RuntimeError(f"Expected real cursor inputs for the occluded runner, wall, opening and tag in seed {seed}")
                    spectated = {int(n) for log in logs.values() for n in re.findall(r"MAZE_QA_SPECTATE round=(\d+)", log)}
                    if spectated != {1,2}:
                        raise RuntimeError("A frozen evader did not watch the surviving teammate")
                    passages = re.findall(r"MAZE_PUNCH_CROSSED first_blocked=1 full_hits=2 cm=(\d+)",logs["host"])
                    narrow = re.findall(r"MAZE_PUNCH_NARROW light_hits=2 sight=1 capsule_blocked=1 moved_cm=([\d.]+)",logs["host"])
                    if len(passages)!=2 or len(narrow)!=2 or logs["host"].count("MAZE_REPAIR_OCCUPIED")<2:
                        raise RuntimeError("Expected a walked full-size opening, sight-only narrow hole and occupied repair on both layouts")
                    result["maze_passage_cm"] = [int(cm) for cm in passages]
                    result["narrow_hole_blocked_after_cm"] = [float(cm) for cm in narrow]
                    result["maze_checks"] = ["blocked capture", "cut and restored wall on every peer", "occupied repair rejected", "tag through narrow opening",
                                             "caught runner frozen and watching teammate", "survival and full capture"]
                result["replicated_boards_by_round"] = [
                    dict(round=int(r[0]), seed=int(seed), feature=name, grid=grid, changed=changed, winner=winner, scores=scores)
                    for r, (seed, (name, _, grid, changed, winner, scores)) in zip(rounds, snapshots_by_round)]
                result["decoy_effects"] = verify_chest_effects(logs, rounds, winning_masks, events, allow_exit)
                if args.decoy_events:
                    if ([int(row[2]) for row in rounds] != [5, 6]
                            or any(row[5] != "decoy" for row in rounds)
                            or winning_masks[1].bit_count() != 1 or winning_masks[2] != 0):
                        raise RuntimeError("Expected a won decoy followed by a zero-score decoy")
                    boosted = [row["boosted"] for row in result["decoy_effects"] if row["round"] == 1]
                    if sum(boosted) != ((1 if count == 1 else 2) if events[1] == 1 else 0):
                        raise RuntimeError("Only a wide-tools draw can grant the tool boost to the winning crew")
                    result["decoy_return"] = verify_decoy_return(logs, winning_masks, events, count)
                    if count > 1 and events[1] == 1:
                        result["decoy_tool_profiles"] = verify_decoy_brush_replay(logs, winning_masks[1])
                if args.interaction:
                    result["transition_views"] = verify_interaction(logs, rounds, allow_exit)
                    if not args.game_only:
                        for role, log in logs.items():
                            if role not in allow_exit and "TREASURE_VAULT_REVEALED winner=" not in log:
                                raise RuntimeError(f"{role} never saw the open final vault")
                        result["vault_revealed_on_remaining_clients"] = True
                if args.arena_terrain:
                    result["continuous_terrain"] = verify_arena_terrain(logs, rounds)
                    result["charged_punch"] = verify_arena_punches(logs, rounds)
                if args.arena_views:
                    result["arena_camera_distances_cm"] = {role: verify_arena_views(log, rounds) for role, log in logs.items()}
                if args.arena_controls:
                    result["control_parity"] = verify_arena_controls(logs["host"])
                if args.venue_demolition or args.venue_smoke:
                    result["venue_demolition"] = verify_venue_demolition(logs, rounds, args.venue_demolition)
                for role, log in logs.items():
                    if role in allow_exit:
                        continue
                    receipts = {int(n) for n in re.findall(r"EXPEDITION_GAME_RECEIPT round=(\d+)", log)}
                    if receipts != set(range(1, len(rounds) + 1)):
                        raise RuntimeError(f"{role} missed a private progress snapshot")
                if args.screenshot:
                    shots = (["MazeChasePlay.png", "PartyMazeChaseOverview.png", "MazePunchIntact.png",
                              "MazePunchDent.png", "MazePunchThrough.png", "MazePunchRepair.png"] if args.scenario == "games-maze" else
                             [game + suffix for game in sorted(games) for suffix in (".png", "Overview.png")])
                    shots.append("PartyEntry.png")
                    if args.arena_terrain:
                        shots.extend(terrain_shots(rounds))
                    if args.arena_views:
                        shots.extend(arena_view_shots(rounds))
                    if args.venue_demolition and "PartyBridgeDash" in games:
                        shots.extend(["VenueBridgeBefore.png", "VenueBridgeAfter.png"])
                    if args.scenario != "games-maze" and not args.decoy_events and not (args.interaction and args.game_only):
                        shots.append("PartyTreasureHint.png")
                    if args.interaction:
                        shots.extend(VEIL_SHOTS)
                        if any(row[5] == "decoy" for row in rounds):
                            shots.append("TreasureFalseChart.png")
                        if not args.game_only:
                            shots.extend(TREASURE_SHOTS)
                            shots.append("TreasureVaultOpened.png")
                    if not args.game_only:
                        shots.extend(["TreasureChart3.png", "TreasureChart4.png"])
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
            if args.briefing:
                result["briefings"] = verify_party_briefings(logs, args, shot_output, directory, guests)
            if args.stall_during_load:
                stalls = re.findall(r"PARTY_BRIEFING_HOST_STALL round=1 gap=([\d.]+)", logs["host"])
                if not any(float(gap) >= 16 for gap in stalls):
                    raise RuntimeError("The authority did not observe the requested long frame during preparation")
                result["server_pause_during_load_seconds"] = 17
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
    parser.add_argument("--decoy-events", action="store_true", help="Verify two real random draws, separate effects, a cancelled load and a zero-score round")
    parser.add_argument("--expect-decoy-event", choices=DECOY_EVENTS[1:], help="Require this result from the first natural draw in --decoy-events")
    parser.add_argument("--game-only", choices=sorted(VOXEL_GAMES), help="Use a single trap game during expedition games QA")
    parser.add_argument("--arena-terrain", action="store_true", help="Dig and fill off-grid with the normal mouse controls, compare collision profiles on each peer")
    parser.add_argument("--arena-views", action="store_true", help="Toggle V in each arena and verify local camera, visible body, center aim and island return")
    parser.add_argument("--arena-controls", action="store_true", help="Compare mouse response, turning, running, jumping and both camera views before entry, in an arena and after return. Use --headless-host to isolate the simulated mouse from desktop input")
    venue = parser.add_mutually_exclusive_group()
    venue.add_argument("--venue-demolition", action="store_true", help="Dig every building on each minigame map and compare geometry and collision after every strike on all three processes")
    venue.add_argument("--venue-smoke", action="store_true", help="Walk up to a building in each arena and compare the punched opening across all three players")
    parser.add_argument("--mode", choices=("normal", "emulated", "both"), default="both")
    parser.add_argument("--editor", action="store_true", help="Use editor game processes during iteration")
    parser.add_argument("--briefing", choices=("all", "force", "drop"), help="Verify the mandatory pre-game page, votes and start route")
    parser.add_argument("--headless-host", action="store_true", help="Disable Metal for iteration only")
    parser.add_argument("--render-guests", action="store_true", help="Render the scenario in each guest's Metal viewport as well as the host's")
    parser.add_argument("--stress", action="store_true", help="Exercise 300 concurrent authoritative edits in an eight-player expedition")
    parser.add_argument("--structures", action="store_true", help="Excavate 16 scenery classes and compare occupied cells, render geometry and collision across all peers")
    parser.add_argument("--scenery-replay", action="store_true", help="Throw onto the stone arch, stand on the mud, punch through stone and sever its supports; --screenshot records the three interactions")
    parser.add_argument("--route-scenery-audit", action="store_true", help="Audit the roadside layout on 64 actual-map seeds alongside --structures")
    parser.add_argument("--neutral-kind", choices=tuple(k.lower() for k in NEUTRAL_KINDS), help="Run one neutral interaction for diagnosis")
    parser.add_argument("--seed", type=int, help="Replay the private expedition round seed")
    parser.add_argument("--neutral-audit", action="store_true", help="Check neutral placement and routes on 64 actual-map seeds during setup")
    parser.add_argument("--profile", action="store_true", help="Add terrain edit preparation, geometry and collision profiling to all processes")
    parser.add_argument("--repro-dig", action="store_true", help="Replay the recorded D excavation and trace actual cross-sections and occluders")
    parser.add_argument("--screenshot", action="store_true", help="Capture the completed chart or the party prompt on the host")
    parser.add_argument("--interaction", action="store_true", help="Exercise movement through both portal directions and capture private treasure feedback")
    parser.add_argument("--long-read", action="store_true", help="Keep the host reading the prepared first round for 50 seconds before consent")
    parser.add_argument("--stall-during-load", "--stall-before-black", action="store_true", help="Pause the host for 17 seconds after early consent while a guest finishes loading")
    parser.add_argument("--slow-briefing-load", action="store_true", help="Delay the last guest’s scene load by 8 seconds while its briefing stays interactive")
    parser.add_argument("--app", type=Path, default=APP)
    parser.add_argument("--map", help="Level package to host, defaults to the scenario's corresponding island")
    parser.add_argument("--output", type=Path, default=ROOT / "ActDemo/Saved/IslandQA")
    args = parser.parse_args()
    if args.arena_controls:
        if (args.scenario not in ("games", "games-solo", "games-crew") or args.game_only == MAZE or args.decoy_events
                or args.interaction or args.arena_views or args.arena_terrain or args.venue_demolition or args.venue_smoke):
            parser.error("--arena-controls needs a score-game round with exclusive control of the host's inputs")
        args.game_only = args.game_only or "PartyReefRun"
    if args.expect_decoy_event and not args.decoy_events:
        parser.error("--expect-decoy-event requires --decoy-events")
    if args.decoy_events:
        if (args.scenario not in ("games", "games-solo") or args.decoys not in (None, 2)
                or args.game_only not in (None, "PartyReefRun") or args.interaction):
            parser.error("--decoy-events uses two reef races and two decoys in games or games-solo")
        args.game_only = "PartyReefRun"
        args.decoys = 2
    if args.scenario == "games-maze":
        if args.game_only not in (None, MAZE) or args.decoys == 0:
            parser.error("games-maze plays two maze rounds using one genuine chest and one decoy")
        args.game_only = MAZE
    if args.game_only == MAZE and (ROOM_CASES.get(args.scenario, (0, False))[1] or args.arena_views or
                                   args.venue_demolition or args.venue_smoke or args.arena_terrain or args.interaction):
        parser.error("Maze chase needs at least two crews and its own wall and spectator checks (--scenario games-maze)")
    if args.long_read:
        if args.scenario not in GAME_CASES or args.scenario in ("games-abort", "games-drop"):
            parser.error("--long-read requires an expedition minigame that will start successfully")
        args.briefing = args.briefing or "all"
    if args.stall_during_load:
        if args.scenario not in GAME_CASES or args.scenario in ("games-solo", "games-drop", "games-abort"):
            parser.error("--stall-during-load needs a multiplayer minigame that will start successfully")
        args.slow_briefing_load = True
        args.briefing = args.briefing or "all"
    if args.slow_briefing_load and (not args.briefing or args.long_read or args.scenario == "games-solo"):
        parser.error("--slow-briefing-load requires multiplayer --briefing without --long-read")
    if args.scenario == "games-abort":
        if args.decoys == 0 or args.interaction:
            parser.error("games-abort exercises one recoverable false chest and must be run independently of --interaction")
        args.game_only = args.game_only or "PartyReefRun"
    if (args.arena_views or args.arena_terrain) and args.scenario not in ("games", "games-crew", "games-solo"):
        parser.error("Arena view and terrain checks require a games, games-crew or games-solo scenario")
    if args.briefing and args.scenario not in (*GAME_CASES, "party"):
        parser.error("--briefing requires a games or party scenario")
    if args.briefing == "drop" and args.scenario != "games-drop":
        parser.error("--briefing drop requires --scenario games-drop")
    if args.render_guests and not (args.briefing or ((args.interaction or args.arena_views or args.arena_terrain or args.scenery_replay or args.structures or args.scenario == "mud") and args.screenshot and not args.headless_host)):
        parser.error("--render-guests requires a visual scenario, --screenshot and a rendered host")
    if args.scenery_replay and (args.scenario != "neutral" or args.neutral_kind or args.structures):
        parser.error("--scenery-replay uses a separate neutral scenario")
    if args.interaction and (args.scenario not in GAME_CASES or (args.screenshot and args.headless_host)
                             or (args.game_only and args.decoys == 0)):
        parser.error("--interaction needs a games scenario, a rendered host for screenshots and a decoy in single-game mode")
    if args.game_only and args.scenario not in GAME_CASES:
        parser.error("--game-only requires a games scenario")
    if (args.venue_demolition or args.venue_smoke) and args.scenario != "games":
        parser.error("--venue-demolition and --venue-smoke run the three-player games scenario")
    if args.neutral_audit and args.scenario not in ROOM_CASES:
        parser.error("--neutral-audit requires an expedition room scenario")
    if args.route_scenery_audit and not args.structures:
        parser.error("--route-scenery-audit requires --structures")
    if args.structures and args.scenario != "neutral":
        parser.error("--structures uses --scenario neutral")

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
