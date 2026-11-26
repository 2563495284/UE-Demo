#!/bin/bash
set -eu
folder="$(cd "$(dirname "$0")" && pwd)"
app="$folder/远征岛.app"
if [[ ! -d "$app" ]]; then
    app="$folder/../Builds/ExpeditionCrew/Mac/远征岛.app"
fi
if [[ ! -d "$app" ]]; then
    printf '%s\n' '请将试玩入口和解压后的「远征岛.app」放在同一目录。' >&2
    exit 1
fi
exec /usr/bin/open -n "$app" --args /Game/TreasureIsland/L_ExpeditionIsland -IslandLAN
