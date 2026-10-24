# Kit lighting in game — DA_TestKitLighting (#392)

Taken 2026-10-24 at 58a81166 with the framing and night of #424, the building turned half a turn so its front faces the RTS
camera (#450):

```
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Turn
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Turn -Night
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Turn -Cvars rts.Debug.BuildingState=2
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Turn -Cvars rts.Debug.BuildingState=2 -Night
```

Buildingshots shows the building Idle unless the run sets `rts.Debug.BuildingState` (#442). L_Bench's team is low on power,
so with `rts.Debug.BuildingState=-1` (the game's own state) the showcase shows its LowPower look: the emergency lamp 03 on
battery, the warning beacon 08 flashing, the mast LEDs 12 and 13 at full on battery and every other lamp out. The LowPower
shots (#393) set `rts.Debug.BuildingState=2`.

Without `-Turn` the front faces +X, away from the RTS camera, which looks north: the 12 m view looks back at the front, but
at 150 and 450 m the camera sees the roof and the back, and none of the wall fixtures. Turned, the 12 m view still frames
the front, lit by L_Bench's own sun by day.

| File | What |
| --- | --- |
| `close_12m_day.png`, `close_12m_night.png` | the front wall (01, 02, 03, 05, 09, 04, 07) and the roof (06, 08, 12, 13) from 15 m |
| `default_150m_*.png`, `far_450m_*.png` | the RTS camera at 150 and 450 m (LOD1 of every kit piece at 450 m), the front wall in view |
| `pieces_12m_day_night.png` | each piece cropped from the 12 m frames, day left, night right |
| `*_lowpower_day.png`, `*_lowpower_night.png` | the same three views in LowPower |
| `pieces_12m_lowpower.png` | 12, 13 and 08 (top to bottom) cropped from the LowPower 12 m frames, day left, night right |
| `indicator_09_441.png` | 09 before and after #441: 12 m night before, 12 m day, night and LowPower night after, 150 m night before and after |

Seen in game: every fixture of 01–08, 12 and 13 reads by day and lights at night; 06's twin heads glow neutral white. 09's
`Wall` mount point turns its diffuser out from the wall (#441), so its blue reads at night and is out in LowPower.
At 150 m by night each wall fixture is a lit point on the front wall, 09's blue among them; 07 sits behind the health
bar, its pool lighting the wall around it. In LowPower 12 and 13 stay lit and 08 lights the roof amber; the other lamps but
03 are out. A frame catches one moment of the blink, so a mast LED can show in its dark phase.
