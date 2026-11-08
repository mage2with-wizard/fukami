#!/bin/bash
# 深見 (日本語版) Linuxアプリフォルダを組み立てるスクリプト。
# 用法: package-jp-app.sh <source-root> <base-app-dir> <out-dir> [--force]
set -euo pipefail

usage() {
    printf 'Usage: %s <source-root> <base-app-dir> <out-dir> [--force]\n' "$0" >&2
}

if [ "$#" -lt 3 ] || [ "$#" -gt 4 ]; then
    usage
    exit 1
fi

source_root="$1"
base_app_dir="$2"
out_dir="$3"
force_flag="${4:-}"

if [ -n "$force_flag" ] && [ "$force_flag" != "--force" ]; then
    printf 'Unknown option: %s\n' "$force_flag" >&2
    usage
    exit 1
fi

require() {
    if [ ! -e "$1" ]; then
        printf 'Missing required path: %s\n' "$1" >&2
        exit 1
    fi
}

# --- 入力チェック ---------------------------------------------------------
require "$source_root"
require "$base_app_dir"

jp_bin="$source_root/jp-build/bin/Fukami"
jp_ini="$source_root/jp-experiment/fukami-default-jp.ini"
jp_workload="$source_root/jp-experiment/product-workload.txt"
jp_launcher="$source_root/jp-experiment/Fukami-launcher.sh"

require "$jp_bin"
require "$jp_ini"
require "$jp_workload"
require "$jp_launcher"

base_lib="$base_app_dir/lib"
base_game="$base_app_dir/share/game"
base_share="$base_app_dir/share"

require "$base_lib"
require "$base_game"
require "$base_share"

# --- 出力ディレクトリの準備 -----------------------------------------------
if [ -e "$out_dir" ]; then
    if [ ! -d "$out_dir" ]; then
        printf 'Output path exists but is not a directory: %s\n' "$out_dir" >&2
        exit 2
    fi
    if [ -n "$(ls -A -- "$out_dir" 2>/dev/null)" ]; then
        if [ "$force_flag" = "--force" ]; then
            rm -rf -- "$out_dir/bin" "$out_dir/lib" "$out_dir/share" "$out_dir/Fukami"
        else
            printf 'Output dir exists and is non-empty: %s (pass --force to overwrite)\n' "$out_dir" >&2
            exit 2
        fi
    fi
fi

mkdir -p -- "$out_dir/share/workload"

# --- 配置 -----------------------------------------------------------------
# ランチャー -> out/Fukami
cp -a -- "$jp_launcher" "$out_dir/Fukami"
chmod 755 -- "$out_dir/Fukami"

# 本体バイナリ -> out/bin/Fukami
mkdir -p -- "$out_dir/bin"
cp -a -- "$jp_bin" "$out_dir/bin/Fukami"
chmod 755 -- "$out_dir/bin/Fukami"

# lib と share/game を丸ごとコピー (隠しファイル .rrv-* も含む)
cp -a -- "$base_lib" "$out_dir/lib"
cp -a -- "$base_game" "$out_dir/share/game"

# 設定・ワークロード
cp -a -- "$jp_ini" "$out_dir/share/fukami-default.ini"
cp -a -- "$jp_workload" "$out_dir/share/workload/bound-workload.txt"

# ライセンス (存在する場合のみ)
if [ -f "$base_share/LICENSE.txt" ]; then
    cp -a -- "$base_share/LICENSE.txt" "$out_dir/share/LICENSE.txt"
fi
if [ -f "$base_share/LICENSE-NOTICES.txt" ]; then
    cp -a -- "$base_share/LICENSE-NOTICES.txt" "$out_dir/share/LICENSE-NOTICES.txt"
fi

# --- ワークロードの検証 ---------------------------------------------------
# 最終行が "end N" (N = 行数 - 1) であることを確認する。
total_lines=$(wc -l < "$jp_workload")
total_lines=$((total_lines + 0))
last_line=$(tail -n 1 -- "$jp_workload")
expected="end $((total_lines - 1))"
if [ "$last_line" != "$expected" ]; then
    printf 'Workload file %s failed validation: last line is "%s", expected "%s"\n' \
        "$jp_workload" "$last_line" "$expected" >&2
    exit 1
fi

# --- SHA256SUMS ------------------------------------------------------------
(
    cd -- "$out_dir"
    sha256sum -- "bin/Fukami" "share/fukami-default.ini" \
        "share/workload/bound-workload.txt" "Fukami" > "SHA256SUMS.tmp"
    mv -f -- "SHA256SUMS.tmp" "SHA256SUMS"
)

printf '%s\n' "$out_dir"
