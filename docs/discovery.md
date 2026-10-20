# Vestige — Discovery

> **Purpose — so that later, anyone can tell whether the thing being
> built is still the thing that was wanted.**

This is what features are checked against for the life of the project.
The process behind it is `~/.claude/workflow.md` § 3.

**Status:** agreed (2026-10-20). The owner answered each section, and
confirmed each *never* line on its own.

## The problem

The owner wanted to know whether they could build a 3D engine
themselves. Scripture inspired them to build scenes from it and share
them with the world. Places such as the Tabernacle exist only as text
and drawings; no one can walk through them.

## Who it is for

At 1.0:

- **The owner**, walking through the Tabernacle to see it as the text
  describes it.
- **A person who studies or teaches the Bible** and wants to see the
  layout and scale for themselves.
- **A curious member of the public** who downloads the walkthrough,
  walks around, and leaves understanding the place better.
- **A person who makes games** and wants to build their own walkthrough
  or game on the engine.

When finished, Vestige should also be:

- a set of walkable biblical places beyond the Tabernacle, starting
  with Solomon's Temple;
- a general engine that other people build and publish full games on;
- a showcase for modern graphics (Vulkan, ray tracing).

## Signs it is working

Each sign is something you could check by using the 1.0 release.

- **SIGN-1** — You can walk the whole Tabernacle, from the courtyard
  through the Holy Place into the Most Holy Place. It never drops below
  60 FPS on the dev hardware, measured the way `CLAUDE.md` § Performance
  says.
- **SIGN-2** — Sizes and materials follow the text (cubits, gold, linen,
  acacia wood), and a written source backs each choice.
- **SIGN-3** — A stranger downloads it on Linux or Windows, starts it,
  and is walking around within a few minutes without help.
- **SIGN-4** — Pointing at an object, such as the altar or the
  lampstand, tells you what it is and where scripture describes it.

## What it deliberately does not do

- **Never:** show a guess as if it were fact. Where the text is silent,
  the scene marks the choice as a guess and gives the reason.
- **Never:** ads, in-app purchases or paid unlocks inside the scripture
  walkthroughs. Selling a game on a store is a separate question and is
  not ruled out.
- **Not yet:** online multiplayer. Not before 1.0.
- **Not yet:** consoles, phones and tablets. The owner's walkthroughs
  target Linux and Windows PCs. After 1.0, the engine should be able to
  support consoles for other people's games.
