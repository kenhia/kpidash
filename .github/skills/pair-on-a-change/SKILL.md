---
name: "pair-on-a-change"
description: "Make a code change collaboratively with explicit checkpoints — clarify intent, propose a plan, confirm, work in small reviewable steps, and review together. Use when the human wants to pair on an edit rather than hand it off."
---

# Pair on a change

Drive a code change as a pair-programming session. The human is your partner at
every checkpoint below. **Each checkpoint is a hard stop** — do not roll past one
without the human's input.

## 1. Clarify

- Restate the goal in your own words and list the acceptance criteria as you
  understand them.
- Name any unknowns, assumptions, or ambiguities.
- Ask focused questions for anything that would change the approach.
- **Stop.** Wait for the human to confirm or correct before planning.

## 2. Plan

- Propose the smallest sequence of steps that gets there.
- For each step, name the files/areas it touches and what "done" looks like.
- Call out risks, trade-offs, and anything you'd do differently with more info.
- **Stop.** Wait for approval of the plan (or revise it) before writing code.

## 3. Step

- Implement **one** step from the approved plan — no more.
- Show the diff and explain *why*, not just *what*.
- If the step turns out bigger or riskier than planned, stop and re-sync instead
  of pushing through.
- **Stop.** Wait for the human before moving to the next step.

## 4. Review

- After each step, put on a reviewer's hat: point out anything you'd flag if you
  were reviewing this diff cold — edge cases, naming, missing tests, follow-ups.
- Distinguish "should fix now" from "worth noting for later."
- **Stop.** Let the human decide what to act on before continuing.

## 5. Wrap

- Recap what changed across all steps.
- State what's verified vs. unverified, and how it was checked.
- List anything deferred or still open.

## Conduct throughout

- Small diffs over big ones; one logical change at a time.
- No unrequested scope — surface it, don't silently do it.
- Think out loud so the human can catch a wrong turn early.
- When in doubt, ask rather than assume.
