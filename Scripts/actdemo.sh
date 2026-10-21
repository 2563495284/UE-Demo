#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UE_ROOT="${UE_ROOT:-/Users/Shared/Epic Games/UE_5.8}"
PROJECT="$ROOT/ActDemo/ActDemo.uproject"
EDITOR="$UE_ROOT/Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor"
LOGS="$ROOT/ActDemo/Saved/Logs"
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
  package)
    "$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh" BuildCookRun "-project=$PROJECT" -noP4 -platform=Mac -clientconfig=Development -build -cook -stage -pak -package -archive "-archivedirectory=$ROOT/Builds/Mac" -utf8output -unattended
    # Modern Xcode needs Package to assemble a self-contained app before Archive.
    test -d "$ROOT/Builds/Mac/ActDemo.app/Contents/UE"
    ;;
  *)
    printf '%s\n' 'Usage: bash Scripts/actdemo.sh {build|assets [-Force]|smoke|pie|play|package|packaged-smoke}' 'Optional UE_ROOT selects the installed Unreal Engine directory.'
    ;;
esac
