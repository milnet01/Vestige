# Audio tooling

## `regenerate_music_loop_test_fixture.sh`

Regenerates `tests/fixtures/audio/music_loop_test.ogg`, the streaming-music
player's decode/loop test fixture (Phase 10.9 Slice 8, W8 part 2/2).

The `.ogg` is **committed content**, not a build artifact — CI does not run
this script and does not need `ffmpeg`. Re-run it only if a test needs a
different signal, and update the test expectations to match. See the script
header for the exact signal recipe.

## `make_ambient_loop.sh`

Cuts a seamless loop from a longer recording. It crossfades the loop's end
into its start, then applies one fixed gain to reach -20 LUFS. It made the
meadow's ambient beds in `assets/audio/ambience/` (3D_E-0036). Those `.ogg`
files are committed, so CI never runs this script and does not need
`ffmpeg`. The script header gives its arguments.
