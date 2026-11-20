#!/bin/sh
# Regression run: a fixed 3600-frame boot, coin-up, character select and
# fight, printing a hash of the whole machine state at the end.
#
#   tools/regress.sh PROGRAM SET [RUN_DIR] [WORK_DIR]
#
# PROGRAM is the headless program, SET the game set (primrage), RUN_DIR the
# folder holding primrage.zip and tms32031.zip (default: run), WORK_DIR
# where translated code is cached (default: a temporary folder).
#
# Translated and interpreted runs (RR_NOREC=1 RR_NODSP=1) must print the
# same hash, as must every compiler that builds the translated code.
set -e
prog=$1
set_name=$2
run_dir=${3:-run}
work_dir=$4
if [ -z "$prog" ] || [ -z "$set_name" ]; then
    echo "usage: $0 PROGRAM SET [RUN_DIR] [WORK_DIR]" >&2
    exit 2
fi
if [ "$set_name" != primrage ]; then
    echo "$0: only the primrage set is supported" >&2
    exit 2
fi
case $prog in /*) ;; *) prog=$(pwd)/$prog ;; esac
case $run_dir in /*) ;; *) run_dir=$(pwd)/$run_dir ;; esac
if [ -z "$work_dir" ]; then
    work_dir=$(mktemp -d)
    trap 'rm -rf "$work_dir"' EXIT
fi
mkdir -p "$work_dir"

# Two coins, start, confirm the fighter, then a fixed stream of moves.
script="coin1 600, coin1 620, start1 700, start1 1000"
f=1300
while [ $f -le 3580 ]; do
    case $(( (f / 23) % 8 )) in
    0) k="p1right $f+20" ;;
    1) k="p1b1 $f" ;;
    2) k="p1b2 $f" ;;
    3) k="p1left $f+12" ;;
    4) k="p1b3 $f" ;;
    5) k="p1down $f+10" ;;
    6) k="p1right $f+30" ;;
    7) k="p1b1 $f" ;;
    esac
    script="$script, $k"
    f=$((f + 23))
done

cd "$work_dir"
"$prog" --roms "$run_dir" --data-dir "$work_dir" --frames 3600 --script "$script" --hash |
    grep '^state hash'
