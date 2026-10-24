---
name: world-divide-building-production
description: Develop World Divide building assets from visual references through staged Blender modelling, Unreal candidate previews, producer approvals, final consolidation and a Claude Code integration handoff. Use for new buildings or continuing a building milestone; excludes UI panels and direct production gameplay integration.
---

# World Divide building production

Deliver reference-led architecture that convinces in exterior close-ups and the game's RTS view. Reuse the successful HQ workflow without importing HQ-specific dimensions, national markings, equipment or historical candidate numbers into another building.

This repository copy is authoritative for Codex and Claude. Read it from the checkout being worked on, not a personal cached copy. Both agents share the building's repo records; role-specific permissions still come from their applicable instructions. During Claude integration, use the accepted final delivery and the integration guidance below; do not restart completed art stages. Only update this workflow for demonstrated reusable lessons, with approval to maintain shared instructions.

## Start or resume

1. Read the current project's AGENTS.md and applicable art/editor instructions. This skill grants no additional write scope. World Divide normally allows the assigned Art delivery, Tools/Art generators and Unreal assets under /Game/Art/Candidates/; Claude owns production integration. Do not change these boundaries or editor configuration merely because this skill is loaded.
2. Inspect the assigned building's current source, reference and state record. In an existing delivery, read its brief, checklist and handoff before creating new tracking files. Resolve the authoritative source and dependency assembly by inspection, not by choosing the highest candidate number.
3. Reconcile the latest direct producer decisions with saved status. A producer's approval in the conversation is valid even if the checklist is stale: record it, preserve it and proceed. Do not repeat a completed milestone to repair a documentation gap. Keep visual approval, technical verification and gameplay integration as distinct states.
4. Use the Unreal MCP skill when available for live editor operations; otherwise use the client's configured native Unreal tools and project editor instructions. Discover available tool schemas and verify a read-only live call; a tool listing or listening port alone is insufficient. Use live Blender when available; an installed background Blender is a valid fallback. Do not terminate other sessions' processes or overwrite an open modified source as connection cleanup.
5. For a new building, inspect its references first. Establish only unresolved decisions that affect production: intended views, dimension/envelope authority, moving parts, ownership markings, usable entrances and vehicle paths, and the boundary between attached building detail and preview terrain. Recommend exterior close-ups plus RTS views when consistent with the user's goal. Save the reference in the assigned delivery.

Use [references/state-and-handoff.md](references/state-and-handoff.md) to initialise or repair the building's current-state record and continuation prompt. Keep it short and current; archive historical narratives rather than repeatedly appending them to the active handoff.

## Stage the visual work

Adapt the stages to the building; skip features it does not possess. The producer can combine or reorder them. A stage ends with concrete views ready for review. Record actual approval before advancing through an agreed approval gate; an approval with small remaining fixes preserves the accepted design and tracks the fixes separately.

| Stage | Deliverable and review |
| --- | --- |
| Reference and architecture | Full envelope, massing, connections, entrances and clear routes in neutral materials; matched top/front/side/three-quarter views and an actual Unreal preview. Resolve inconsistent reference views explicitly. |
| Representative quality section | Finish one important entrance/tower/façade section in Blender and Unreal to establish geometry, materials and lighting quality before extending it. |
| Remaining frames, façades and walls | Extend the approved standard: supports, layered construction, recesses, glazing, sills, joints, roof-edge transitions and clean bevels. |
| Roofs and functional equipment | Connected equipment, mounting supports, flashing, drainage, service access and clear motion envelopes, appropriate to this building. |
| Functional bays and mechanisms | Visible interior, plausible mechanisms, separate moving parts and pivots; demonstrate full travel with route and aperture clearance. |
| Attached ground and courtyard | Paving, steps, props and planting inside the agreed asset boundary. Exclude map terrain from reusable exports unless requested. |
| Whole-building finish | Consistent surface scale, reflections, owner colours, illumination and motion; final consolidation and integration delivery. |

Do not substitute resolution, triangle counts or successful packaging for reference fidelity. Check silhouette and layered construction early. Use neutral daylight alongside cinematic lighting. Judge bevels and surface detail at both the closest intended view and RTS distance. Retain an approved master and bake its fine detail into the game mesh's maps; the game mesh, its two LODs and its triangle, material and texture budgets follow [docs/art/MODEL_PERFORMANCE.md](../../../docs/art/MODEL_PERFORMANCE.md). Do not add microscopic geometry everywhere.

## Author and verify incrementally

- Work from one authoritative source per active candidate. Fork recoverable checkpoints for meaningful stages or risky changes, not every tiny adjustment. Inventory inherited dependencies when a new preview references earlier candidates.
- Make generators safe to import, parameterise input/output roots and isolate exports from source edits. Repeated stages should replace their own named output rather than duplicate it. Mark destructive full rebuilds and one-time recipes; never replay them on an approved final source just to render or export.
- Use named modules, stable material slots, local coordinates, pivots and gameplay helpers. Record transforms/units once; independently check an export/reimport before propagating the workflow across the building.
- Assemble in an isolated candidate preview level. Never use production L_TestMap as a scratch scene. Save only in the authorised candidate area.
- Review material scale and geometry in Unreal at each stage, not only at the end. After meaningful import/material changes, check results and logs, save, reload, and inspect persisted bindings.
- Check meaningful risks: tangents/normals, degenerate export faces, UV suitability, route clearances, moving-part intersections, ownership isolation, motion bounds and visible LOD transitions. Keep expensive checks scoped to changed components and their affected neighbours; broaden when new evidence warrants it.
- Preserve texture streaming and mipmaps unless there is a demonstrated reason to differ. Separate high-detail source geometry from an efficient assembly; track material sections/components as well as triangles before assembly cost becomes a final-stage surprise.

Read [references/hq-lessons.md](references/hq-lessons.md) when handling generator reuse, export shading, motion, material persistence, performance measurement or cleanup. It records observed HQ failure modes and their targeted remedies. Before every modelling round, read [references/modelling-lessons.md](references/modelling-lessons.md) and check the delivery against it; Claude appends the lessons of each reviewed round.

## Evidence and continuity

Capture a fixed set of reference-aligned cameras plus stage-specific details. Label Blender renders, Unreal editor views, Simulate/PIE and actual gameplay views accurately. Save camera parameters. A native editor angle resembling RTS is not proof of the production camera or runtime behaviour. Every delivery includes the master-against-game-mesh comparison at the 12 m, 150 m and 450 m RTS views and the pair at the 260 m LOD switch (MODEL_PERFORMANCE.md, rules 7 and 8).

For motion, test the full cycle, reversals/loop boundaries, pivots and bounds, and save actual Unreal samples. A sampled WebP is evidence of poses, not necessarily real-time frame rate. Autonomous preview loops do not implement gameplay authority, power behaviour or networking.

At each stage update the active state, exact source/asset paths, approval receipt, evidence index, remaining defects and next task. Use exact approved user wording when practical; do not invent approval dates or infer approval from silence. At handoff, give one short new-chat starter that invokes this skill and points to the state record. Users should not have to reconstruct context or recopy the full workflow.

## Finish, consolidate and hand to Claude

Consolidation and removal of older Unreal candidates require the user's requested scope. Do not clean other buildings or production assets. Before authorised cleanup:

1. Archive affected saved packages outside Content with a file/hash receipt and document recovery.
2. Copy/migrate the actual dependency closure to one final building candidate, retaining historical portable source files. Explicitly persist material parents and slot bindings; verify both object references and registry dependencies.
3. Save and validate in a fresh editor process when feasible before deleting historical dependencies. Coordinate a restart around unsaved work; do not discard it. If restart is unavailable, report the reduced verification and retain doubtful dependencies.
4. Query external referencers, remove only superseded authorised assets with Unreal tools, fix redirectors and validate again. Never force-delete filesystem .uasset files to resolve reference problems. Keep externally referenced assets for Claude to migrate.
5. Reopen the final preview, inspect materials/motion and repeat the affected visual smoke checks. Pre-cleanup evidence cannot certify the surviving assembly.

Produce a Claude implementation prompt in the assigned Art delivery with verified inputs: portable files/hashes, mesh-slot bindings, material graphs/instance overrides, texture import settings, assembly transforms/pivots/helpers, motion parameters and reproducible preview controls, LOD settings, dependency inventory, reference gallery and unresolved items. Repoint reimport source paths to the final portable files; filename suffixes alone are not reliable mappings.

Claude's acceptance includes idempotent setup/regeneration, production assembly and gameplay state wiring, collision/navigation/selection, placement and rotations, production/spawn/exit, owner colours and low power, motion/culling/shadows and representative runtime measurements. State which of these remain untested. Do not claim production readiness from a single-building editor benchmark.
