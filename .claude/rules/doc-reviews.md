---
paths: ["docs/specs/**","docs/plans/**","docs/architecture/**","ARCHITECTURE.md"]
---

# Documentation reviews

Moved verbatim from `CLAUDE.md` on 2026-10-10.

9. **Documentation reviews are independent cold reads — `review-contract`.** Spec, plan, design-doc and architecture-doc reviews dispatch fresh subagents with no authoring context. ROADMAP and CHANGELOG are not subjects — `review-contract` refuses them, and they get `check-doc-facts` instead. Re-reading it yourself is not a review. Iterate review → fix → review until it converges or the skill's loop cap is reached; both are clean exits. This is the same principle `review-code` applies to code, extended to documentation.
