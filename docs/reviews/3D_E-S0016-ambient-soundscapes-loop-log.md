# 3D_E-S0016 ambient soundscapes — review loop log

Written by `review-contract`, one row per loop, oldest first.

## Cold-eyes loop log

| Loop | Date | Lanes | Q1 | Q2 | Q3 | Q4 | Outcome |
|---|---|---|---|---|---|---|---|
| 1 | 2026-10-02 | 2 (neutral-lane; each held every question) | 1 | 0 | 2 | 2 | Verified 5, fixed 5, dismissed 0. Both lanes: the one-shot scheduler starts with `timeUntilNextFire` 0, so a zone fired on its first audible frame and INV-5's test had its cases backwards; now armed when first seen (Q4). Both lanes: `setSourceVolume` could not re-apply the clip's loudness makeup, since `SourceMix` stores none; the engine now keeps it per source (Q3). One lane: INV-3's "exactly one bed" contradicted the failed-start retry (Q4). One lane: a clip edit was never acted on and the planner lacked the clip (Q3). From a lane's open question, settled by the orchestrator: every `playSound*` call announces its caption before the device check, so §6's "nothing plays" with no device was incomplete; beds are now not started without a device (Q1). Three open questions resolved clean (2D loop flag is 3D_E-0738's; AudioSystem never stops a source; loadBuffer re-warns at most once a second). Lanes arrived holding the global CLAUDE.md and principles page only; no git snapshot. Before dispatch, building the packet found that eviction reuses a source's name, filed and fixed as 3D_E-0739 and folded into §4.3. |
