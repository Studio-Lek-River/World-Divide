# Current state and handoffs

## Where to store context

Use an existing authorised documentation location if available. Otherwise place the records inside the assigned Art/<faction>/buildings/<building>/ delivery; this respects the building-art scope without requiring changes to global project instructions. Do not create a duplicate tracking system for an existing building.

Use a concise production brief, a current state file and a review index. Existing PRODUCTION_BRIEF/CHECKLIST/HANDOFF records can serve these roles. Keep historical evidence in candidate folders and link to it; avoid copying all historical handoffs into the active record.

## Minimum current-state content

```text
Building / current task:
Reference paths and precedence / explicit departures:
Envelope, axes, units, routes, attached-ground boundary:
Latest source and revision:
Authoritative Unreal folder / preview level / assembly:
Inherited dependencies (including older candidate folders):
Stages: each planned | in progress | ready for review | approved
Approval receipts: exact producer statement, stage/version, known date
Technical verification: evidence paths and scope
Open defects: visual / technical / integration, with affected components
Next single milestone and completion criteria:
Safe reproduction steps / commands that reset or accumulate work:
Recovery checkpoint:
```

Approved visuals can still have integration work outstanding. Write “garage visual approved; runtime door control pending” instead of “garage incomplete.” A local correction should identify the affected geometry and preserve neighbouring approvals.

If the user says a stage is approved, record that fact before advancing. If docs disagree with the current user, fix docs within scope rather than asking the user to repeat approval. If actual files do not contain the claimed completed work, explain the concrete discrepancy and inspect plausible alternate candidates before rebuilding.

## Fresh-chat starter

Generate this with actual values:

> Use $world-divide-building-production for <building>. Read <current-state path> and its linked brief, reference and latest evidence. Continue <next milestone>. Approved stages are <list>; preserve their designs. Work within current AGENTS.md scope, update the state and deliver actual Unreal review evidence. Stop at the agreed visual review gate. Do not commit or push.

Fresh chats are useful at milestone boundaries, not mandatory after an arbitrary message count. Keep the same workspace/source of truth and one writer on an editor asset at a time.

Ordinary repo requests such as “continue the barracks” should work without a long pasted prompt: AGENTS.md and CLAUDE.md route building work to this skill. Discover the assigned building and read its current state before asking the producer to reconstruct context. For Claude intake, start with “Integrate the accepted <building> from its final repo handoff and implementation prompt using the shared building workflow.”

## Reproduction and delivery authority

The saved final state and serialized manifests override intermediate generator defaults. Record which scripts are safe to rerun and which would reset, duplicate or destroy accepted changes. A final review index should point to the surviving post-cleanup assets, not historical paths that have been removed.
