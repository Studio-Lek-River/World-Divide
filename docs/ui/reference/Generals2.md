# Generals 2 UI reference

The cancelled C&C Generals 2 build (EA, 2013) holds a complete RTS HUD. This page tells us where it is, what we may take from it and which of its files match our panels.

## The rule: structure, not art

- Use Generals 2 to answer **what** a panel shows, how its information is grouped and which states a control has (disabled, queued, cooldown, flashing).
- Never copy its art, shapes, proportions or layouts into `UI/`, and never send its images to the designer. The handbook asks for original work, with nothing copied from or resembling Command & Conquer ([DESIGNER_HANDBOOK.md](../DESIGNER_HANDBOOK.md), "Original work only"), and EA's readme allows the assets for private use only.
- When a finding goes into a brief or a spec, write it as our own requirement ("the build tooltip lists cost, power drain, build time, hotkey and missing tech") and do not name or attach Generals 2.

## Where it is

`D:\Downloads\Generals 2` on the producer's machine (not in the repository):

| Item | Contents |
|---|---|
| `Command & Conquer.zip` (2.2 GB) | The game build. The UI is under `Command & Conquer/UTFWinAssets/`: `layouts/` (113 XML `.layout` files), `images/` (about 1,340 PNGs), `fonts/`, `cursors/`, `TextStyles.css`. Levels are Frostbite `.sb/.toc` bundles under `Data/Win32`. |
| `Generals 2 Certified Tools/Gen2LostAssets` | Loose cursors, fonts, images and `CCLogo.psd`. |
| `Frostbite 3 Editor`, `CNCGenerals2tools.zip`, `bws-0424.zip` | Community tools for the Frostbite bundles. |
| `C&C_G2 Mesh Extraction (720p).mp4` | Video on extracting meshes. |

Read one layout without extracting the zip:

```powershell
Add-Type -A System.IO.Compression.FileSystem
$z = [IO.Compression.ZipFile]::OpenRead("D:\Downloads\Generals 2\Command & Conquer.zip")
$e = $z.Entries | ? Name -eq "bottomBarUI.layout"
$r = New-Object IO.StreamReader($e.Open()); $xml = $r.ReadToEnd(); $r.Close(); $z.Dispose()
[regex]::Matches($xml, 'name="Comment"[^>]*value="([^"]+)"') | % { $_.Groups[1].Value }
```

A layout is a tree of `Window` objects; each has an `Area` rect (designed at 1600x1200) and a `Comment` that names it, so listing the comments gives the panel's outline.

## Files per panel

| Our panel | Generals 2 layouts | Generals 2 image folders |
|---|---|---|
| CommanderBar (build, queue, abilities) | `bottomBarUI`, `sidebarUI`, `BuildButton`, `buildQueueLayout`, `selectQueueButton`, `abilityButton`, `toggleableAbilityButton`, `UnitUpgradeButton` | `BuildButtons`, `ProductionButtons`, `AbilitiesUpgrades`, `BottomBarSkins` |
| Selection and unit info | `unitSelectionLayout`, `unitDetailLayout`, `combatUnitDetailLayout`, `unitWithUpgradesDetailLayout`, `structureSelectionLayout`, `SelectionButton`, `SelectionOverviewGarrison`, `unitFrame`, `VeterancyUnitFrame`, `veterancyIndicator` | `SelectionButtons`, `UnitPortraits`, `UnitFrames` |
| Unit stats | `weaponInfoLayout`, `weaponsInfoContainerLayout`, `armorInfoLayout`, `movementInfoLayout`, `damageTypesInfoLayout`, `vulnerabilitiesInfoLayout` | |
| Tooltips | `tooltipLayout`, `buildButtonTooltip`, `abilityTooltip`, `standardAbilityButtonTooltip`, `researchUpgradeTooltip`, `UnitDetailTooltipLayout`, `buildShortcutTooltip`, `forwardDeployTooltip` | |
| ResourceBar | `bottomBarUI` (Resource Display Container), `DepositIndicator` | `HUD` |
| MapButtons and minimap | `bottomBarUI` (Minimap container), `Minimap*` (pings, attack arrows, objectives, capture points) | `MiniMap` |
| Generals powers and superweapon | `playerAbilityButton`, `playerPowerTimer`, `PlayerAbilityExclusionZone`, `skillTree`, `skillTreeTier`, `skillTreeButton` | `PlayerAbility`, `SuperWeapon`, `Generals 64x64` |
| Control groups and idle workers | `selectionGroupButton`, `SelectIdleWorkerLayout` | `ControlGroups` |
| Objectives and messages | `objectiveWindow`, `ObjectiveFrame`, `objectiveIconWindow`, `objectiveIconStackWindow`, `scriptedDisplayUnitFrame` | `ObjectivesSystem` |
| MatchResult | `*GameVictory`, `*GameDefeat` (one pair per faction: APA, EU, GLA) | `Victory_Defeat` |
| Loading screens | `loadingScreen`, `GeneralsLoadingScreen`, `LoadingScreen_*` (one per general) | `LoadingScreen` |
| InGameMenu | `bottomBarUI` (Pause Menu, Menu Button) | |

## Takeaways

**Two HUD variants.** `bottomBarUI` puts everything in a bottom bar: minimap and resources bottom left, general info (control groups, general powers, experience bar, rank, portrait) in the middle, production queue, build grid and selection bottom right. `sidebarUI` is a classic sidebar: player powers in five tiers, a seven-slot queue, a 14-button build grid, 23 selection slots, then power toggle, repair and the minimap. They built both, so the choice was still open late in the project.

**Bottom bar contents.** Edge-scroll zones on all eight edges; a taunt button; credits shown two ways (with and without power, so the readout changes when power matters); a team-colour toggle and a player ping button next to the minimap; ten control-group slots with a "no group set" state; six general-power slots; the general's experience bar, rank and portrait; a build-queue strip of six; the build grid in three heights (tall, medium, short) to fit the number of buttons; separate single- and multi-selection containers; an ally/enemy panel with a match timer and taunts; four message lines; a score display; a scripted timer; a wave announcement with countdown for the survival mode.

**Build button.** Only three parts: the button, a flash animation (for new or ready) and a count (for queued units). The rest goes in the tooltip.

**Build tooltip.** Portrait; name; build time and hotkey on one line; power cost and credit cost with icons; description; "affects" and gameplay notes; a tech-requirements title with up to three missing dependencies. The plain tooltip is title, hotkey, description and frame.

**Unit detail (single selection).** Portrait, name with upgrade icons, health and a resource readout (for harvesters). Combat units add weapon, armour, movement, damage-type and vulnerability panels in their own layouts, so the stats can be shown or hidden as a block.
