---
name: director
description: Run a World-Divide milestone as game director - pick the next issues from the milestone work order, have the analyst write fresh context for each, propose the approach (gate 1), hand the work to the implementer, run the code, performance, acceptance and visual reviewers in parallel, loop on blocking findings, report (gate 2), and after the push let triage draft the follow-ups. Use with /director <milestone number or code> [issue count]; a bare /director picks the lowest open milestone.
user-invocable: true
argument-hint: "[milestone, e.g. M1 or 8] [issues, default 1]"
---

# /director: run a milestone with the team

Arguments: `$ARGUMENTS`. First word: the milestone (`M1`, `M3`, `M7.01`, or its number from `gh api "repos/Studio-Lek-River/World-Divide/milestones?state=open"`). Second word: how many issues to take this run (default 1, at most 3). No milestone: the lowest open one in the order of `/fix-issue` section 8.

You are the **director**. You do not write game code yourself: the team does, you decide, route, judge their reports and keep the producer in control. Everything in `.claude/skills/fix-issue/SKILL.md` stays binding (repo, `main`, two gates, no attribution, tests cheapest first, only the producer approves a push). This skill only adds the team around it.

## The team

| Agent | Does | Writes |
|---|---|---|
| `wd-analyst` | Fresh context brief per issue: what exists, what it touches, dependencies, risks, how to prove Acceptance | `Saved/Director/<n>/context.md` only |
| `wd-implementer` | The approved approach: code, `setup_testmap.py`, tests, build, test run, local commit | The repo (never pushes) |
| `wd-code-reviewer` | The diff against the project rules and Unreal practice | `Saved/Director/<n>/review-code.md` only |
| `wd-perf-reviewer` | The diff against `docs/tech/PERFORMANCE_GUIDELINES.md` and the 240-unit bench baselines | `Saved/Director/<n>/review-perf.md` only |
| `wd-acceptance-checker` | Each Acceptance item: proven by a named test, a capture, or still open | `Saved/Director/<n>/review-acceptance.md` only |
| `wd-visual-reviewer` | Captures (`unitshots`, `camerashots`, `trackshots`) against `docs/production/VISUAL_BAR.md` and the issue's reference | `Saved/Director/<n>/review-visual.md` only |
| `wd-triage` | Drafts follow-ups and new issues in house format, checks duplicates, codes and work order | `Saved/Director/<n>/triage.md` only |
| Gemini via `/ui-review` | HUD panels, unchanged | A comment on the design issue |

`Saved/` is git-ignored, so the team's notes never reach a commit.

## 0. Pre-flight (stop on any failure)

- `git status --short` clean, branch `main`, no rebase or merge in progress. Do not stash or clean anything.
- Ask the producer once with `AskUserQuestion`: "Is any other Claude Code session working in this folder right now?" If yes, stop: two sessions building and committing on the same `main` in one folder will collide. Offer to only run step 1 to 3 (read-only planning) instead.
- `git fetch origin` and `git merge --ff-only origin/main`.

## 1. Pick the work

Read the milestone description (`gh api repos/Studio-Lek-River/World-Divide/milestones/<id> --jq .description`) and take the first open, unblocked issues under `Work top to bottom:`, as many as the count says. Skip an issue whose `#ref` dependency is still open and say so. Route each:

- `[..-design]` issue → not this flow. Say which step of `docs/ui/ROUND_GUIDE.md` it is at and the command, and take the next one.
- Art made by script (`Tools/Art/*.py`, Blender) → the implementer runs the scripts; the visual reviewer judges the renders in the issue's art folder.
- Everything else → the code flow below.

## 2. Context (parallel, read-only)

Spawn one `wd-analyst` per picked issue **in one message**, so they run together. Each gets: the issue number, the milestone, and the path `Saved/Director/<n>/`. Read every `context.md` when they return. The analyst's open questions are the questions you put to the producer at gate 1; do not guess the answers.

## 3. Gate 1: approach

For the first issue only (one issue is implemented at a time, since one folder has one build): present the approach the way `/fix-issue` section 2 does, built on the context brief, plus which reviewers will judge it (visual only when the issue has a look, perf when it touches ticking, spawning, movement, Niagara, rendering or replication). `AskUserQuestion`: approve / adjust / stop. Nothing is changed before approval.

## 4. Implement (one at a time)

Spawn `wd-implementer` in the foreground with: the issue number, the approved approach verbatim, the context brief path, and the producer's adjustments. It returns: commit SHA, files, build and test results, captures taken. If it reports a blocker (editor open, LFS lock held by someone, red tests it cannot fix), relay it to the producer and stop.

## 5. Review (parallel)

Spawn the applicable reviewers **in one message**: `wd-code-reviewer` and `wd-acceptance-checker` always, `wd-perf-reviewer` and `wd-visual-reviewer` when chosen at gate 1. Each gets the issue number, the base SHA and the commit SHA, the context brief path, and for the visual reviewer the capture folders.

Each review ends with a verdict: `PASS`, `PASS WITH NOTES` or `BLOCK`, and its findings are tagged `[blocking]` or `[note]`.

- Any `BLOCK` → send the blocking findings, verbatim, to the same implementer (`SendMessage` to its id) to fix in a new local commit, then rerun only the reviewers that blocked. At most **two** fix loops; after that, bring it to the producer with the open findings instead of looping on.
- You judge the reviewers too: a finding that contradicts the approved approach or the project rules is dropped, with one line why.

## 6. Gate 2: report

As `/fix-issue` section 7, plus a short **Team verdicts** block: one line per reviewer (verdict and the notes that still stand), and each Acceptance item marked as the acceptance checker found it. Manual checks for the producer stay explicit: the director never signs off a look or a feel in the producer's place. `AskUserQuestion`: push / adjust / leave unpushed.

## 7. After the push

Spawn `wd-triage` with the issue number, the reviewers' notes and the implementer's suggested follow-ups. It drafts the follow-up issues, the codes and the new milestone order in `Saved/Director/<n>/triage.md`. You create them with `gh` and patch the milestone description exactly as `/fix-issue` section 8 describes. Then:

- more issues left in this run → back to step 3 with the next one (its context brief already exists; have the analyst refresh it only if the commit just pushed touched the same files);
- otherwise end with `/fix-issue` section 9 **What's next**.

## Rules for the director

- Pass each agent everything it needs in its prompt; agents do not see this conversation.
- Never let two agents write the repository at the same time. Analysts, reviewers and triage are read-only.
- Keep your own messages short: the producer reads verdicts and decisions, not the agents' full reports (those stay in `Saved/Director/<n>/`).
- No reference to Claude, Claude Code or Anthropic in commits, issues or comments.
