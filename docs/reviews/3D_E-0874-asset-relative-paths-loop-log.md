# 3D_E-0874 asset relative paths — review record

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Verified | Fixed | Outcome |
|---|---|---|---|---|---|---|---|---|
| 1 | 2026-11-06 | 2 (neutral-lane; each held every question) | 1 | 1 | 1 | 5 | 5 | Both lanes found all five; Q4 2 (no Q4 column). Fixed: `loadModel` hands `GltfLoader` the lexical path, so linked-folder glTF textures save root-relative; `toKey` root-empty row first; `propPath` and its `fs::exists` probes join through `resolve`; INV-3 asserts not-default under `GLTestFixture`, INV-5's traversal test sets sandbox roots and is mutation-only; INV-6's log search ignores case and adds `fail` and `rejected`. Packet pass found the glTF defect first; 1d clean; open questions (`m_rootPath`, `build-release/bin/assets`) resolved clean. |
