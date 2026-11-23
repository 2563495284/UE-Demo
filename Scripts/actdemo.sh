#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/ActDemo/ActDemo.uproject"
EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
LOGS="$ROOT/ActDemo/Saved/Logs"
VOXEL_ARCHIVE="$ROOT/Builds/VoxelPlayground/Mac"
VOXEL_MAP="/Game/VoxelPlayground/L_VoxelPlayground"
ISLAND_ARCHIVE="$ROOT/Builds/TreasureIsland/Mac"
ISLAND_MAP="/Game/TreasureIsland/L_TreasureIsland"
mkdir -p "$LOGS"
case "${1:-help}" in
  build)
    "$UE_ROOT/Engine/Build/BatchFiles/Mac/Build.sh" ActDemoEditor Mac Development "$PROJECT" -NoHotReload "-log=$LOGS/ImplementationBuild.log"
    ;;
  assets)
    # Existing assets are preserved unless the caller explicitly supplies -Force.
    shift
    "$EDITOR" "$PROJECT" -run=ActDemoBuild -unattended -nullrhi -nosound -NoSplash "-abslog=$LOGS/BuildAssets.log" "$@"
    ;;
  smoke)
    "$EDITOR" "$PROJECT" /Game/ActDemo/Maps/L_AshenCourt -game -ActDemoSmoke -nullrhi -nosound -unattended -NoSplash "-abslog=$LOGS/Smoke.log"
    # macOS RequestExitWithStatus may return zero on failure; the marker is authoritative.
    grep -Eq 'ACTDEMO_SMOKE_COMPLETE checks=[0-9]+ failures=0' "$LOGS/Smoke.log"
    ;;
  kwang)
    shift
    "$EDITOR" "$PROJECT" -run=ActKwang -Build -unattended -nullrhi -nosound -NoSplash "-abslog=$LOGS/KwangAssets.log" "$@"
    grep -F 'ACTDEMO_KWANG_READY' "$LOGS/KwangAssets.log"
    ;;
  play)
    "$EDITOR" "$PROJECT" /Game/ActDemo/Maps/L_AshenCourt -game -windowed -ResX=1280 -ResY=720 -NoSplash "-abslog=$LOGS/Playtest.log"
    ;;
  packaged-smoke)
    "$ROOT/Builds/Mac/ActDemo.app/Contents/MacOS/ActDemo" -ActDemoSmoke -nullrhi -nosound -unattended "-abslog=$LOGS/PackagedSmoke.log" 2>&1 | tee "$LOGS/PackagedSmoke.stdout.log"
    grep -Eq 'ACTDEMO_SMOKE_COMPLETE checks=[0-9]+ failures=0' "$LOGS/PackagedSmoke.stdout.log"
    ;;
  pie)
    "$EDITOR" "$PROJECT" -unattended -nullrhi -nosound -NoSplash '-ExecCmds=Automation RunTests ActDemo.Editor.PIE' '-TestExit=Automation Test Queue Empty' "-ReportExportPath=$ROOT/ActDemo/Saved/Automation/PIE" "-abslog=$LOGS/PIEAutomation.log"
    grep -F 'Test Completed. Result={Success} Name={PIE} Path={ActDemo.Editor.PIE}' "$LOGS/PIEAutomation.log"
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
    ARCHIVE="$VOXEL_ARCHIVE"
    MAP="$VOXEL_MAP"
    if [[ "$1" == island-* ]]; then
      ARCHIVE="$ISLAND_ARCHIVE"
      MAP="$ISLAND_MAP"
    fi
    if [[ "$1" == *-host ]]; then
      ADDRESS="$MAP?listen"
    else
      ADDRESS="${2:?Specify the host LAN IP, with optional :port}"
    fi
    open -n -a "$ARCHIVE/ActDemo.app" --args "$ADDRESS" -windowed -ResX=1280 -ResY=720
    ;;
  package|voxel-package|island-package)
    ARCHIVE="$ROOT/Builds/Mac"
    if [[ "$1" == voxel-package ]]; then ARCHIVE="$VOXEL_ARCHIVE"; fi
    if [[ "$1" == island-package ]]; then ARCHIVE="$ISLAND_ARCHIVE"; fi
    "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun "-project=$PROJECT" -noP4 -platform=Mac -clientconfig=Development -build -cook -stage -pak -package -archive "-archivedirectory=$ARCHIVE" -utf8output -unattended
    # Modern Xcode needs Package to assemble a self-contained app before Archive.
    test -d "$ARCHIVE/ActDemo.app/Contents/UE"
    ;;
  *)
    printf '%s\n' 'Usage: bash Scripts/actdemo.sh {build|assets [-Force]|kwang|smoke|pie|play|package|packaged-smoke|voxel-map|voxel-package|voxel-host|voxel-join HOST_IP|island-map|island-package|island-host|island-join HOST_IP}' 'Optional UE_ROOT selects the installed Unreal Engine directory.'
    ;;
esac
