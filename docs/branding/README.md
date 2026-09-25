# Vestige branding

- `vestige-logo.svg`: the logo. A stone doorway with light pouring through,
  beside the name. Text is converted to outlines, so no font is needed to
  display it. Transparent background; made to sit on a dark panel.
- `vestige-logo.png`: the same logo rendered at 1200 px wide.
- `packaging/vestige.png`: the app icon. It is not a logo on its own.

The lettering is Cinzel (Copyright 2020 The Cinzel Project Authors), used
under the SIL Open Font License 1.1. The doorway mark is drawn by hand in
`make_logo.py`.

## Rebuilding

`make_logo.py` expects `Cinzel.ttf` (the variable font from
`google/fonts`, path `ofl/cinzel/`) in the current directory, and needs
fontTools. It writes `vestige-logo.svg`. Render the PNG with any SVG
renderer; ImageMagick's SVG reader is disabled by this machine's policy,
so Qt's `QSvgRenderer` was used.

Colours: lettering and arch `#ede3cf`, "ENGINE" `#f0b64a`, doorway glow
`#ffd978` to `#e89a2c`.
