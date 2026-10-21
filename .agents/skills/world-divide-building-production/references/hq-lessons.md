# Observed HQ lessons

These are conditional remedies from World Divide HQ production, not requirements to reproduce its design or machinery. Original receipts remain in Art/USA/buildings/usa_hq/ and docs/art/usa_hq/ when available. Do not load the entire HQ archive when starting another building.

| Observed failure | Preventive or recovery action |
| --- | --- |
| Legacy generator import ran an unguarded build and rewrote seven Candidate12 files during Candidate13 work. | Inspect entrypoints before reuse; isolate pure helpers, guard main execution, pass input/output roots, preserve protected files. Do not import legacy usa_hq_blender.py. |
| Stale handoffs sent the user back to already approved stages. | Keep one current status and per-stage approval receipts; latest explicit producer approval outranks old checklist boxes. |
| A latest preview assembled geometry/materials from several older candidates. | Inventory the actual assembly and dependencies; highest folder number is not a self-contained delivery. |
| Re-beveling inherited shells created slivers and tangent/binormal errors. | Inspect bevel history, validate exported triangles/UVs, and independently reimport. Repair affected shells/export topology; retain MikkTSpace rather than hiding warnings. Temporary per-triangle UVs are not final texture UVs. |
| Connector coping crossed; connector terminals later protruded through the garage liner. | Review junctions top-down and from inside/outside adjacent spaces. Test changed geometry against neighbouring envelopes as well as the exterior silhouette. |
| Dense dish supports and moving equipment required clearance corrections. | Check full motion envelopes around ribs, parapets and roofs, not just the rest pose. |
| Flag import ignored source vertex colours under the default import setting. | Verify import policy; use explicit import settings or preserved UV weights for pinned-edge WPO. Check deformed normals, two-sided shading and bounds. |
| Native material-instance set_parent updated the loaded object but did not persist its ownership master dependency; cleanup misclassified the master as unused. | Explicitly dirty/save changed instances, inspect serialized Parent and registry dependencies, and validate a fresh process before removing source assets. Repair persistence before cleanup. |
| Material edits during repair triggered a D3D12 crash. | Checkpoint before risky graph edits; after recovery verify persisted state and recapture final evidence. Do not loop the same crashing mutation without a changed approach. |
| Preview terrain accidentally entered the reusable asset scope. | Define slab/attached landscaping versus map terrain early; separate preview collections and export membership. |
| Growth reached millions of instanced triangles and many material sections before final optimization. | Track changed-stage geometry, sections and components; reuse detail/instances where appropriate and inspect projected detail at real camera distances. Do not impose an unmeasured universal budget. |
| Early performance CSVs were background-throttled; later measurements were frame paced and showed no FPS gain. | Warm up, confirm foreground/throttling and pacing, record hardware/resolution/settings/frame count and CPU/GPU timings. Use fresh filenames and reject invalid runs explicitly. Similar capped FPS does not establish equal GPU cost or an optimization win. |
| Final packages retained historical reimport filenames and intermediate scripts retained obsolete material values. | Deliver an explicit portable-to-package map, repoint sources, serialize final graphs/overrides and document one-time versus repeatable recipes. |
| A door loop and editor RTS-angle captures could be mistaken for production behaviour. | Label preview evidence precisely; Claude still implements authoritative production/vehicle/door state and tests the actual gameplay camera. |

## Targeted verification

For a changed assembly: validate units and transforms, named material bindings, geometric route/motion clearances, shading and saved package state. Preserve unaffected accepted source data with scoped receipts rather than generating huge repeated audits for every minor render.

For final consolidation: verify master-material dependencies, all surviving slots, portable reimport paths, no missing project references, one intended preview World and no obsolete redirectors. Recheck after deletion and a fresh-process reload. Candidate22's ownership_persistence_repair.json, cleanup.json and postcleanup_smoke.json demonstrate why this order matters.

For gameplay handoff: distinguish collision/route geometry checks from navigation and actual spawned-vehicle tests; material-state previews from power/ownership wiring; sampled motion from runtime controller behaviour; estimated compressed mip sizes from measured GPU residency.
