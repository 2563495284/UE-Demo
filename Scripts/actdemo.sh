#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/ActDemo/ActDemo.uproject"
EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
LOGS="$ROOT/ActDemo/Saved/Logs"
VOXEL_MAP="/Game/VoxelPlayground/L_VoxelPlayground"
ISLAND_ARCHIVE="$ROOT/Builds/TreasureIsland/Mac"
ISLAND_MAP="/Game/TreasureIsland/L_TreasureIsland"
mkdir -p "$LOGS"
case "${1:-help}" in
  build)
    shift
    "$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" ActDemoEditor Mac Development "$PROJECT" -NoHotReload "-log=$LOGS/ImplementationBuild.log" "$@"
    ;;
  party-feature)
    shift
    "$EDITOR" "$PROJECT" -run=ActPartyFeature -unattended -nullrhi -nosound -NoSplash "-abslog=$LOGS/PartyFeature.log" "$@"
    grep -F 'PARTY_FEATURE_ASSET_READY' "$LOGS/PartyFeature.log"
    ;;
  island-front)
    "$EDITOR" "$PROJECT" -run=ActIslandFront -unattended -nullrhi -nosound -NoSplash "-abslog=$LOGS/IslandFrontMap.log"
    test -f "$ROOT/ActDemo/Content/TreasureIsland/L_IslandFront.umap"
    ;;
  play)
    shift
    "$EDITOR" "$PROJECT" -game -NoSplash "-abslog=$LOGS/Playtest.log" "$@"
    ;;
  voxel-map|island-map)
    MAP_ARGS=()
    MAP_LOG="$LOGS/VoxelMap.log"
    if [[ "$1" == island-map ]]; then
      MAP_ARGS=(-Island)
      MAP_LOG="$LOGS/IslandMap.log"
      mkdir -p "$ROOT/ActDemo/Content/TreasureIsland"
    fi
    shift
    "$EDITOR" "$PROJECT" -run=ActVoxelMap -unattended -nullrhi -nosound -NoSplash "-abslog=$MAP_LOG" ${MAP_ARGS[@]+"${MAP_ARGS[@]}"} "$@"
    grep -F 'VOXEL_MAP_READY' "$MAP_LOG"
    ;;
  voxel-host|voxel-join|island-host|island-join)
    MAP="$VOXEL_MAP"
    if [[ "$1" == island-* ]]; then
      MAP="$ISLAND_MAP"
    fi
    if [[ "$1" == *-host ]]; then
      ADDRESS="$MAP?listen?bIsLanMatch"
    else
      ADDRESS="${2:?Specify the host LAN IP, with optional :port}"
    fi
    open -n "$ISLAND_ARCHIVE/远征岛.app" --args "$ADDRESS" -IslandLAN
    ;;
  package|voxel-package|island-package)
    shift
    exec python3 "$ROOT/Scripts/package_mac.py" "$UE_ROOT" "$@"
    ;;
  *)
    printf '%s\n' 'Usage: bash Scripts/actdemo.sh {build|party-feature|play|package|voxel-map|voxel-package|voxel-host|voxel-join HOST_IP|island-front|island-map|island-package|island-host|island-join HOST_IP}' 'Optional UE_ROOT selects the installed Unreal Engine directory.'
    ;;
esac
