#!/usr/bin/env bash
# make_ambient_loop.sh <in> <out.ogg> <start_s> <length_s> [fade_s]
# Made the meadow's ambient beds in assets/audio/ambience/ (3D_E-0036).
# Prereq: ffmpeg with libvorbis (a maintainer tool, not a CI dependency).
#
# Cuts seg = in[start, start+length+fade], then crossfades seg's tail
# (length..length+fade) over its head (0..fade). The result is `length`
# seconds long and its end runs straight into its start, so it loops
# without a seam. One fixed gain to -20 LUFS integrated (a dynamic
# normaliser would give the two ends different gain), Vorbis q4.
set -euo pipefail
in=$1; out=$2; start=$3; len=$4; fade=${5:-4}
total=$(awk "BEGIN{print $len+$fade}")
loop="[0:a]asplit=2[x][y];[x]atrim=start=$fade,asetpts=PTS-STARTPTS[tail];[y]atrim=end=$fade,asetpts=PTS-STARTPTS[head];[tail][head]acrossfade=d=$fade:c1=qsin:c2=qsin"
I=$(ffmpeg -hide_banner -nostats -ss "$start" -t "$total" -i "$in" -filter_complex "$loop,ebur128=framelog=quiet" -f null - 2>&1 | awk '/^ +I:/{print $2}' | tail -1)
gain=$(awk "BEGIN{print -20 - ($I)}")
ffmpeg -hide_banner -loglevel error -y -ss "$start" -t "$total" -i "$in" -filter_complex "$loop,volume=${gain}dB,alimiter=limit=0.7:level=false,aresample=44100[o]" \
  -map "[o]" -c:a libvorbis -q:a 4 "$out"
echo "$(basename "$out"): measured $I LUFS, gain ${gain} dB"
