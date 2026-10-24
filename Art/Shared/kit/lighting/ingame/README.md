# Kit lighting in game — DA_TestKitLighting (#392)

Taken 2026-10-24 at 2b5fd70b with the framing and night of #424:

```
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Cvars rts.Debug.BuildingState=0
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Cvars rts.Debug.BuildingState=0 -Night
```

`rts.Debug.BuildingState=0` (Idle) is needed: L_Bench's team is low on power, so without it the showcase shows its
LowPower look (only the emergency lamp 03 on battery, the warning beacon 08 flashing, every other lamp out).

| File | What |
| --- | --- |
| `close_12m_day.png`, `close_12m_night.png` | the front wall (01, 02, 03, 05, 09, 04, 07) and the roof (06, 08, 12, 13) from 15 m |
| `default_150m_*.png`, `far_450m_*.png` | the RTS camera at 150 and 450 m (LOD1 of every kit piece at 450 m) |
| `pieces_12m_day_night.png` | each piece cropped from the 12 m frames, day left, night right |

Seen in game: every fixture of 01–08, 12 and 13 reads by day and lights at night; 06's twin heads glow neutral white.
09's blue indicator does not show at night (open, follow-up of #393).
