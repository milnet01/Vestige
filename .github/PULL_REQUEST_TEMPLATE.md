<!--
Thank you for sending a PR! Please keep this template; tick the boxes
that apply. Remove any sections that are truly not applicable to your
change (and say so in the description).
-->

## Summary

<!-- What does this PR change, and why? One to three sentences. -->

## Related issues / discussions

<!-- e.g. Closes #123, Refs #456. If this started from a roadmap
item or a design doc, link that too. -->

## What kind of change is this?

- [ ] Bug fix (non-breaking)
- [ ] New feature (non-breaking)
- [ ] Breaking change (API / behaviour change that contributors or
      downstream users need to adapt to)
- [ ] Documentation / tooling / CI only
- [ ] Refactor (no behaviour change)

## Checklist

- [ ] I've read [`CONTRIBUTING.md`](../CONTRIBUTING.md) and agree to
      the DCO.
- [ ] Every commit in this PR is signed off (`git commit -s`, producing
      a `Signed-off-by:` line).
- [ ] Code follows [`CODING_STANDARDS.md`](../CODING_STANDARDS.md)
      (Allman braces, naming conventions, one class per file, etc.).
- [ ] I added or updated tests for the change (new features and bug
      fixes both require tests).
- [ ] `ctest --test-dir build --output-on-failure` passes locally.
- [ ] `python3 tools/audit/audit.py -t 1 2 3` reports nothing new
      introduced by this PR.
- [ ] `CHANGELOG.md` updated (engine-level changes) or the relevant
      tool changelog (for `tools/audit` / `tools/formula_workbench`
      changes).
- [ ] If this touches the audit tool: `tools/audit/VERSION` is bumped
      in the same commit.
- [ ] No magic numerical constants hand-coded — formula-driven values
      go through the Formula Workbench
      (`tools/formula_workbench/`), per project rule #11.
- [ ] No workarounds dressed up as fixes: if this PR is a workaround
      for something you couldn't root-cause, the commit message and
      a code comment say so explicitly.

## AI-assistance disclosure

Per [`CONTRIBUTING.md`](../CONTRIBUTING.md#ai-assisted-contributions-welcome-must-be-disclosed),
please describe briefly whether this PR was drafted with AI
assistance. Either is fine; the goal is transparency.

- [ ] No AI assistance used.
- [ ] AI-assisted (tool: ___). I reviewed, tested, and own the
      outcome.

## Screenshots / videos

<!-- For rendering, UI, or editor-visible changes, attach a screenshot
or short video. For headless changes, feel free to remove this
section. -->

## Additional notes for the reviewer

<!-- Anything non-obvious: surprising design choices, known
follow-ups, performance measurements, etc. -->
