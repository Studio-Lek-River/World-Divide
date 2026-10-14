# ProductionQueue — USA / Common

## 1. Purpose
Show what the selected production building is making and what waits behind it. Let the player cancel one production order while clearly distinguishing slowed production from a pause.

## 2. Anatomy / placement
ProductionQueue occupies Selection's QueueSlot, nominal 436×116 at local 12,164 inside a 460×280 parent. Root visibleWhen uses the scalar queue.visible. Reuse existing Panel/PanelInset as needed and ProgressBar/ProgressFill for active progress. New QueueSlot has transparent icon centre, four states; Common.Icon.LowPowerSlow provides the warning.

| Element | Queue-local rectangle / rule |
|---|---|
| QueueTitle | 0,0,192,20, Body 14, Production queue |
| LowPowerMarker | 202,0,234,20, bolt/clock 18 then LOW POWER — SLOW, Warning; visible only for queue.lowPower |
| QueueItems | row at 0,28; five 64×64 slots at x 0/88/176/264/352, or a 5×1 Grid with fixed cells and spacing 24 |
| QueueItemIcon | 10,5,44,44 within occupied frame; item.icon |
| ActiveReadout | 6,34,52,20 within active frame; Number 14, item.timeLeft |
| QueueItemProgress | 6,57,52,4 within active frame; item.progress; visible only for item.isActive |
| QueueHint | 0,94,436,20, Body 14, Hover an item for cancel details |

Repeat **queue**, count=5, including framework-padded empty cells. Never use a list as visibleWhen. Each cell has a tooltip: empty says Empty queue slot/no action; occupied shows item.name, active item.timeLeft, and Click to cancel. Emptiness is structural, not a guessed key. Mockup's 42% annotates preview progress; implement with supplied timeLeft plus proportional fill, without inventing a percentage key.

## 3. States
| State | Art / text / input |
|---|---|
| Waiting | QueueSlot.Normal, actual icon/name tooltip; no running-progress claim |
| Active | QueueSlot.Selected, stronger blue frame, item.isActive progress + timeLeft; first item in current source |
| Hover occupied | QueueSlot.Hover: red edge plus X plate; tooltip includes Click to cancel; active readout/progress remain |
| Pressed | Normal fallback under existing sprite format; release dispatches existing action |
| Empty | QueueSlot.Disabled, dark recessed border and transparent centre; no icon/time/progress, no cancel; harmless explanatory tooltip |
| Low power | queue.lowPower: LowPowerSlow plus LOW POWER — SLOW wording; keep progress and current timeLeft; do not disable cancellation |
| Hidden | queue.visible=false: no queue frame, text, tooltip or input; zero desired height for the entire QueueSlot |

Low power means **slower**, not paused: keep authoritative progress/timeLeft, which already reflects production rate. No extra timer/multiplier/reason key. Zero items with queue.visible true still shows five empty frames; only false visibility releases space.

## 4. Interaction
Occupied left click sends **CancelQueueItem**; server validates/refunds. Current source cancels the **last queued occurrence of that entity type**. Tooltip: Click to cancel one [item.name] (last queued of this type). Clicking active with later duplicates preserves the active occurrence; without duplicates it can cancel active. Duplicate orders remain separate cells. Refresh from authoritative data; empty/right clicks send nothing. Revalidate expired selections. Proposed tooltip 350 ms/hover 100 ms/press immediate. No new hotkeys, modal or reorder action.

## 5. Edge cases / reflow
Five cells cover 0/1/4/5 orders: first active, others waiting, then empty. More than 5 needs agreed paging/scroll with stable identities, not silent truncation. Localized names/full durations live in tooltips; Number 14 mm:ss fits the readout. At 16:10–21:9 parent stays 460/content 436. Count 5 is capacity, not a binding key.

Compact 196-high parent: QueueSlot 436×80 at x 12,y 116. Header height 20; row starts y 24 with five 48-square frames at x 0/88/176/264/352; icon 32, active readout 40×20 at 4,24, progress 40×3 at 4,44. Omit QueueHint; compact low-power marker uses icon plus SLOW at right. No font smaller than 14. When hidden, QueueSlot contributes zero height: selection description remains one line, but upgrades/status/aircraft/construction content can extend down to the bottom padding. The parent must use natural flow or a visibility callback; no unauthorized queue key is bound in Selection.

## 6. Accessibility
Active progress length/time, hover X, recessed empty frame and slowed-production bolt/clock plus words remain meaningful without colour. Red is an additional cancel cue, not the only one. Avoid hatch behind icons/text. Text/MutedText/Warning retain the approved USA theme contrast; actual dynamic colours must retain AA. No pulsing warning is necessary.

## 7. Requests for code
Confirm framework structural empty cells in intake/presenter; no guessed binding. Provide occupied/empty tooltips and hidden QueueSlot zero-height propagation. For exact clicked-order cancellation, change **existing CancelQueueItem** handling to stable order/index; do not misrepresent current last-of-kind behaviour. Duration fitting/paging if needed. No new key/action proposed.
