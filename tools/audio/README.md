# Audio tooling

## `regenerate_music_loop_test_fixture.sh`

Regenerates `tests/fixtures/audio/music_loop_test.ogg`, the streaming-music
player's decode/loop test fixture (Phase 10.9 Slice 8, W8 part 2/2).

The `.ogg` is **committed content**, not a build artifact — CI does not run
this script and does not need `ffmpeg`. Re-run it only if a test needs a
different signal, and update the test expectations to match. See the script
header for the exact signal recipe.
