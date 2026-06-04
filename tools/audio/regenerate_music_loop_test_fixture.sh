#!/usr/bin/env bash
# Regenerates tests/fixtures/audio/music_loop_test.ogg — the streaming-music
# player's loop/decode test fixture (Phase 10.9 Slice 8 W8 part 2/2).
#
# When to run this: basically never. The .ogg is committed content, not a
# build output. Re-run ONLY if a test needs a different signal (e.g. a louder
# loop seam, a different length) — then bump the test expectations to match.
#
# Prereq: ffmpeg with the libvorbis encoder (a maintainer-machine tool, NOT a
# CI dependency — the fixture ships committed).
#
# Signal: 0.5 s of a 440 Hz fundamental + 880 Hz harmonic, 48 kHz stereo. The
# harmonic makes the waveform non-trivial so the loop-seam assertion has
# something worth checking.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${REPO_ROOT}/tests/fixtures/audio/music_loop_test.ogg"

ffmpeg -hide_banner -loglevel error \
    -f lavfi \
    -i "aevalsrc=0.4*sin(2*PI*440*t)+0.2*sin(2*PI*880*t):s=48000:c=stereo:d=0.5" \
    -c:a libvorbis -q:a 4 "${OUT}" -y

echo "Wrote ${OUT}"
ffprobe -hide_banner -v error \
    -show_entries stream=channels,sample_rate,duration \
    -of default=noprint_wrappers=1 "${OUT}"
