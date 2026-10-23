# Selection — Clean Minimal (#241)

Mockup: [`../mockups/Mockup_Selection_CleanMinimal_USA.png`](../mockups/Mockup_Selection_CleanMinimal_USA.png), made from the game's own screenshots (`ue.ps1 -Task uishots -Panel Selection@<Scene>`, then `python Tools/Scripts/selection_sheet.py`). The earlier framed-portrait design (#94) is replaced for the single selection; the Multi view, the production queue, construction and aircraft ammo keep their behaviour.

## 1. Purpose
Say what the one selected unit or building is, at a glance: large hero art, its name and its type or role, and the upgrades installed on it. No health in this panel: world-space health bars stay as they are, and the group tiles of the Multi view keep theirs.

## 2. Composition
Local HUD units at 1080p. The Selection area is 444 × 180 inside the 460-wide `SelectionFrame` of the compact bottom frame (#126 owns the frame and the HUD scale; this panel does not grow it).

`selection.detail` decides between two views, so the hero never covers what sits below it:

| `selection.detail` | When | View |
|---|---|---|
| `None` | a unit or a finished building without a queue | **Full view** |
| `Construction` | a construction site | **Compact view** + construction progress and cancel |
| `Aircraft` | an aircraft | **Compact view** + ammo and REARMING |
| `Queue` | a building that produces (as `queue.visible`) | **Compact view** + `ProductionQueue` in `QueueSlot` |

**Full view** (`FullView`)

| Element | x, y, w, h | Content |
|---|---|---|
| `HeroFull` | 0, 0, 272, 136 | `HeroBackdrop` (Common: a soft light glow, faction-neutral), then the art |
| `HeroArtFull` | fills `HeroFull` | `selection.heroArt` when `selection.hasHeroArt`: a 2:1 cut-out, no frame |
| `HeroIconFull` | 112 × 112, centred | `selection.heroArt` when the data has no hero art: its icon, large |
| `SelectionName` | 274, 6, 170, 24 | `selection.name`, Title 18, one line, ellipsis |
| `SelectionRole` | 274, 32, 170, 20 | `selection.role`, Body 14, Accent |
| `NameRule` | 274, 58, 32, 2 | a short Accent rule, the one blue accent |
| `SelectionDescription` | 274, 66, 170, 62 | `selection.description`, Body 13, MutedText, three lines, ellipsis |
| `UpgradesFull` | 8, 144, 436, 32 | installed upgrades: 32-square slots, 6 apart, then the `+N` entry |

**Compact view** (`CompactView`)

| Element | x, y, w, h | Content |
|---|---|---|
| `HeroCompact` | 0, 0, 176, 88 | backdrop, art or an 80-square icon, `ConstructionOverlay` on a site |
| `SelectionNameCompact` | 184, 2, 260, 24 | Title 18 |
| `SelectionRoleCompact` | 184, 28, 260, 20 | Body 14, Accent |
| `UpgradesCompact` | 184, 56, 260, 26 | 24-square slots, 4 apart, then `+N` |
| `ConstructionStatus`, `AircraftStatus`, `QueueSlot` | 4, 96, 436, ≤ 84 | unchanged content, now full width below the hero |

## 3. Hero art
- `URTSEntityData::HeroArtSprite`, for example `Art.M20Guardian`, resolving like a layout sprite: `<Faction>.Art.<Entity>`. File `UI/Textures/<Faction>/Art/T_Art_<Faction>_<Entity>.png`, **512 × 256** (2× of the 256 × 128 it shows at), transparent around the subject, left and bottom edges faded out so it sits in the panel without a frame. No text in the art.
- The M20 Guardian's: `Tools/Art/m20_hero_render.py` renders the saved Blender scene on a transparent film (studio light, low three-quarter front view, gun leading towards the text, a shadow-catcher contact shadow, team-mask panels greyed so no team colour is baked in); `python Tools/Scripts/unit_icon.py <render> USA M20Guardian --art` crops it 2:1, fades the edges and adds the sprite entry. The USA HQ's: `Tools/Art/hero_render.py`, the same render for any blend with the camera, light scale, team materials to grey and texture folder as arguments (the HQ's command is in its docstring), then `unit_icon.py <render> USA Headquarters --art`. Any unit or building gets art the same way, with no code change.
- **Fallback:** no `HeroArtSprite` shows the entity's icon at 112 (80 compact) on the same backdrop; no icon shows `Icon.Placeholder`. Nothing selected shows nothing: `selection.heroArt` is empty.
- **Role:** `URTSEntityData::RoleText`; empty shows the kind: Aircraft, Building or Unit. (Infantry / vehicle / naval wording follows the entity tags of #237.)

## 4. Installed upgrades
- **What counts:** the player's completed upgrades whose `Targets` include the selected unit's or building's data, each once, in the order completed (`URTSUpgradeData::GetInstalled`). A building no longer lists what it researches; that stays the upgrade panel's `done`. Unresearched, locked or queued research never appears here, and the slots have no action, so they cannot start research.
- **Counts:** one slot per installed upgrade. 0 collapses the row (`selection.hasUpgrades`), with no empty sockets. 1–6 sit left-aligned from the hero's edge at a fixed gap. No tiers, stages or pips; an upgrade with several effects is still one slot.
- **Capacity and overflow:** `URTSSettings::SelectionUpgradeSlots` (`DefaultGame.ini`, default 6) icons show; the rest go behind one `+N` entry (`selection.upgradeMore`), whose tooltip lists every hidden upgrade with its effect. Nothing is ever cut off silently. Raise the setting for a wider HUD: no layout or code change.
- **Lists:** `selection.upgrades` (`item.icon`, `item.name`, `item.description`, `item.status`, `item.disabledText`) and `selection.upgradeMore` (`item.label` "+3", `item.name` "3 more upgrades", `item.description` the hidden ones, one per line).
- **Tooltip:** hover shows name, description and effect ("Weapon damage x1.5"), and status "Installed". `item.disabledText` is empty: installed upgrades cannot be lost or switched off in the game yet; when they can, it carries the reason and the slot gets a hatch, never a colour alone. The tooltip is the plain one until the rich tooltip of #69/#97.

## 5. Refresh
- The installed list is worked out again only when the selected data or the number of completed upgrades changes (`ARTSPlayerController::GetSelectionInstalledUpgrades`), never per frame. Upgrades are only ever added, so the count tells a change, on the server and on a client when `CompletedUpgrades` replicates.
- The upgrade rows are repeats without a fixed count: the HUD builds their copies anew when the count changes, and only then.
- Switching to another unit, deselecting or the selection dying clears the art, the icons and their tooltips on the same frame.

## 6. Responsive and long labels
- The HUD scales with the screen height (DPI scale), as the rest of the bottom frame: shots at 1920 × 1080, 2560 × 1440, 3440 × 1440 and a 1680 × 1050 16:10 window. HUD scale settings (min / default / max) come with #126; check this panel there.
- Name and role are one line each and end in an ellipsis; the description three lines. A 30 % longer localised string (scene `LongLabel`) never pushes the upgrade row.
- Nothing overlaps the command panel, the minimap, the queue or the tooltip band: the panel stays inside its 444 × 180.

## 7. Screenshot scenes
`ue.ps1 -Task uishots -Panel Selection@<Scene>` puts the UI test state in a scene of `RTSUITestState::ApplyScene`, with fixture upgrades made at run time: `Hero0` … `Hero9` (the M20 with that many upgrades), `LongLabel`, `Infantry`, `Vehicle`, `Aircraft`, `Building`. `BottomFrame@Hero6` and `BottomFrame@Building` show the whole bottom frame for the space budget.

## 8. Requests for design
- Paint a hero cut-out per unit in the house style when the roster art comes (#93); the render pipeline stays the fallback.
- A hatch and an icon for an unavailable upgrade once gameplay has the state.
