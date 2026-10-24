# Kit lighting in game — DA_TestKitLighting (#392)

Taken 2026-10-24 at 2b5fd70b with the framing and night of #424:

```
ue.ps1 -Task buildingshots -Building DA_TestKitLighting
ue.ps1 -Task buildingshots -Building DA_TestKitLighting -Night
```

Buildingshots shows the building Idle unless the run sets `rts.Debug.BuildingState` (#442). These shots were taken before
that with `-Cvars rts.Debug.BuildingState=0`, which gives the same picture. L_Bench's team is low on power, so with
`rts.Debug.BuildingState=-1` (the game's own state) the showcase shows its LowPower look: the emergency lamp 03 on
battery, the warning beacon 08 flashing, the mast LEDs 12 and 13 at full on battery and every other lamp out.

The LowPower shots (#393) were taken the same way at 4a42426e with `rts.Debug.BuildingState=2`, by day and with `-Night`.

| File | What |
| --- | --- |
| `close_12m_day.png`, `close_12m_night.png` | the front wall (01, 02, 03, 05, 09, 04, 07) and the roof (06, 08, 12, 13) from 15 m |
| `default_150m_*.png`, `far_450m_*.png` | the RTS camera at 150 and 450 m (LOD1 of every kit piece at 450 m) |
| `pieces_12m_day_night.png` | each piece cropped from the 12 m frames, day left, night right |
| `*_lowpower_day.png`, `*_lowpower_night.png` | the same three views in LowPower |
| `pieces_12m_lowpower.png` | 12, 13 and 08 (top to bottom) cropped from the LowPower 12 m frames, day left, night right |
| `indicator_09_441.png` | 09 before and after #441: 12 m night before, 12 m day, night and LowPower night after, 150 m night before and after |

Seen in game: every fixture of 01–08, 12 and 13 reads by day and lights at night; 06's twin heads glow neutral white.
09's blue indicator did not show at night: its diffuser is modelled on the underside, out of the camera's sight. Since #441 its
`Wall` mount point turns it to face out from the wall; `close_12m_day.png`, `close_12m_night.png` and
`close_12m_lowpower_night.png` were taken again at b053a1a2 with that (`-Night`, `rts.Debug.BuildingState=2` for LowPower).
At night its blue reads at 12 m and is out in LowPower; at 150 m the camera sees the roof and the back, so none of the
front wall's fixtures shows there (`pieces_12m_day_night.png` still has 09's old crop).
In LowPower 12 and 13 stay lit and 08 lights the roof amber; the other lamps but 03 are out. A frame catches one moment of
the blink, so a mast LED can show in its dark phase (13 at night).
