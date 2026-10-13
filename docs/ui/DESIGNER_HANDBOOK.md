# World Divide — UI/UX Designer Handbook

You are the UI/UX designer of **World Divide**, an original modern-military real-time strategy game set in 2012 (Unreal Engine 5.8, PC, mouse and keyboard). This handbook comes with every design brief. It explains how the HUD is built, the file formats you deliver in, the visual style, and how a delivery is packaged. When the brief and this handbook disagree, the brief wins for that panel only.

The game engine does not ship a fixed HUD. It is built from text files, much like the `.gui` and `.gfx` files of Paradox games. Layout files are read while the game runs. Sprite and theme files are turned by the project's setup script into Unreal's Common UI styles (button, text and border styles per faction). You never touch those generated styles: the text files are the source.

| File | Role | Paradox equivalent |
|---|---|---|
| `UI/Layouts/<Panel>.layout.json` | which elements a panel has, where they sit, what data they show, what a click does | `.gui` |
| `UI/Sprites/<Set>.sprites.json` | named images: which PNG, its 9-slice margins, its button states | `.gfx` |
| `UI/Themes/<Faction>.theme.json` | colours and fonts per faction | — |
| `UI/Textures/...`, `UI/Fonts/...` | the PNGs and font files themselves | `gfx/interface` |

So what you deliver is not only pictures: your layout file **is** the panel. The programmers write the C++ that supplies the data and carries out the clicks; everything about look and placement is yours.

---

## 1. The game and the screen

- The player looks down at a battlefield from an angled RTS camera, selects units with the mouse, and orders them with right clicks, command buttons and hotkeys.
- Three factions share one set of panels and differ only by theme and sprites: **USA**, **China**, and **Insurgents** (our own fictional group, no real-world symbols).
- **Faction theme is not team colour.** Units and minimap markers use the team colour of their player (any colour, two USA players can face each other). The HUD uses the faction theme of the local player. Never bake a team colour into art: deliver white areas the engine tints.

### HUD budget (reference resolution 1920x1080)

All layout numbers are in **HUD units at 1920x1080**. The engine scales them by the shortest side of the screen (1.0 at 1080p, 1.33 at 1440p, 2.0 at 4K) and anchors keep 16:10 and 21:9 working, so design at 1920x1080 and paint textures large enough for 2x.

```
+--------------------------------------------------------------------------------------+
| TOP BAR 48 high: credits | power                                        | scoreboard  |
|   notifications stack under the top bar, left                                        |
|                                                                                      |
|                               BATTLEFIELD (keep it free)                             |
|                                                                                      |
|                tooltips appear here, just above the bottom band                      |
+-------------+------------------------------------------------+-----------------------+
| MINIMAP     | SELECTION PANEL                                | QUEUE + COMMAND GRID  |
| 230 x 230   | fills the rest                                 | 460 wide, 3 x 5 grid  |
+-------------+------------------------------------------------+-----------------------+
  BOTTOM BAND at most 230 high
```

- The HUD covers at most about 25% of the screen. The battlefield is the game.
- Only two modals exist in a match: the **match result** (Victory/Defeat) and the **in-game menu** (Esc). They are screens on a modal layer above the HUD: both dim everything behind them, the top bar included, block the battlefield while open, and Esc closes the in-game menu.
- The game is mouse and keyboard only. Design for hover and click, not for gamepad focus.
- A click on any hit-testable element never reaches the battlefield.

---

## 2. Layout files (`UI/Layouts/<Panel>.layout.json`)

A layout file holds one root element. Elements nest through `children`.

```json
{
  "name": "ResourceBar",
  "type": "Panel",
  "anchor": "TopStretch",
  "position": [0, 0],
  "size": [0, 48],
  "sprite": "TopBar",
  "children": [
    {
      "name": "Credits",
      "type": "HBox",
      "anchor": "Left",
      "position": [16, 0],
      "spacing": 8,
      "children": [
        { "type": "Image", "sprite": "Icon.Credits", "size": [28, 28], "color": "Accent" },
        { "type": "Text", "text": "{player.credits}", "font": "Number", "color": "Text" }
      ]
    },
    {
      "name": "Power",
      "type": "HBox",
      "anchor": "Left",
      "position": [220, 0],
      "spacing": 8,
      "visibleWhen": "player.usesPower",
      "children": [
        { "type": "Image", "sprite": "Icon.Power", "size": [28, 28], "color": "Accent", "visibleWhen": "!player.power.low" },
        { "type": "Image", "sprite": "Icon.PowerLow", "size": [28, 28], "color": "Warning", "visibleWhen": "player.power.low" },
        { "type": "Text", "text": "{player.power.produced} / {player.power.consumed}", "font": "Number",
          "color": "Text", "bind": { "color": "player.power.color" } }
      ]
    }
  ]
}
```

### Element types

| Type | What it is | Extra properties |
|---|---|---|
| `Panel` | a container with an optional background sprite; children are placed freely by anchor and position | `sprite` |
| `HBox` / `VBox` | a row / column; children are placed one after another | `spacing`, `align` (`Start`, `Center`, `End`, `Fill`) |
| `Grid` | fixed cells | `columns`, `rows`, `cellSize` [w,h], `spacing` |
| `Image` | a sprite | `sprite`, `color` (tint, for `tint` sprites) |
| `Text` | a label | `text`, `font` (`Title`, `Body`, `Number`), `fontSize`, `align` (`Left`, `Center`, `Right`), `wrap` (true/false), `maxLines` |
| `Button` | clickable; draws the state of its sprite (Normal, Hover, Pressed, Disabled, Selected) and can hold children (icon, hotkey plate, cost) | `sprite`, `action`, `tooltip` (true: hovering fills the tooltip with this item) |
| `ProgressBar` | a 0–1 fill | `sprite` (background), `fillSprite`, `fillColor`, `bind.progress` |
| `Slot` | a hook: places another layout file, or a panel the engine draws itself (the minimap terrain and markers) | `layout` (layout file name without extension) or `widget` (engine panel name given in the brief) |

### Properties every element has

| Property | Meaning | Default |
|---|---|---|
| `name` | unique within the file; the programmers and the tests refer to it | none, optional for plain decoration |
| `type` | see the table above | required |
| `anchor` | where in the parent it hangs: `TopLeft`, `Top`, `TopRight`, `Left`, `Center`, `Right`, `BottomLeft`, `Bottom`, `BottomRight`, or a stretch: `TopStretch`, `BottomStretch`, `LeftStretch`, `RightStretch`, `Fill` | `TopLeft` |
| `position` | [x, y] offset from the anchor point, in HUD units; positive x goes right, positive y down; for right/bottom anchors the offset goes inward | [0, 0] |
| `size` | [w, h] in HUD units; 0 on a stretched axis means "fill the parent" | the content size |
| `padding` | [left, top, right, bottom] inside the element | [0, 0, 0, 0] |
| `color` | a theme colour name (section 4), `Team` (the team colour of the item), or `#RRGGBB` / `#RRGGBBAA` | white |
| `opacity` | 0–1 | 1 |
| `hitTest` | true: the element catches the mouse and blocks the battlefield | true for `Panel` with a sprite and `Button`, false otherwise |
| `visibleWhen` | a condition: `key` (true or non-empty), `!key` (false or empty), `key=value`, `key!=value` | always visible |
| `bind` | binds properties to data: `{ "progress": "item.progress", "sprite": "item.icon", "enabled": "item.enabled", "selected": "item.selected", "color": "item.teamColor" }` | none |
| `children` | elements inside it (containers and `Button`) | none |

### Text and data

- Data reaches a layout only through **binding keys**, written as `{key}` inside `text` or as values in `bind` and `visibleWhen`.
- Each brief lists the exact keys its panel has; use only those. Keys you need but do not have: write them in the spec under "Requests for code". Do not invent them in the layout.
- Numbers and times arrive already formatted (`1,250`, `0:12`). Texts arrive translated, so plan for them to be 30% longer than the English.
- Game data (names, descriptions, costs, build times, stats, hotkeys, which icon an entity uses) lives in the game data files under `Data/`, not in layouts. You never edit or repeat it: a layout shows it through the brief's keys, so a balance change never touches your files.

### Lists: `repeat` and `template`

Command buttons, queue slots, selection tiles, notifications and scoreboard rows are lists. A container with `repeat` makes one copy of its `template` per item; inside the template, `item.*` keys refer to that item.

```json
{
  "name": "Grid",
  "type": "Grid",
  "columns": 5,
  "rows": 3,
  "cellSize": [84, 64],
  "spacing": 4,
  "repeat": "commands",
  "count": 15,
  "template": {
    "type": "Button",
    "sprite": "Button",
    "action": "IssueCommand",
    "tooltip": true,
    "bind": { "enabled": "item.enabled" },
    "visibleWhen": "!item.empty",
    "children": [
      { "type": "Image", "anchor": "Fill", "bind": { "sprite": "item.icon" } },
      { "type": "Image", "sprite": "DisabledOverlay", "anchor": "Fill", "visibleWhen": "item.disabledReason=MissingPrerequisite" },
      { "type": "Text", "anchor": "TopLeft", "position": [4, 2], "text": "{item.hotkey}", "font": "Number", "fontSize": 12 }
    ]
  }
}
```

`count` fixes the number of copies; items beyond the data get `item.empty` = true, so you decide what an empty slot looks like.

### Actions

A `Button` sends an **action name** from the brief's list (`IssueCommand`, `CancelQueueItem`, `SelectGroup`, ...). Inside a template the action carries its item automatically. Hotkeys belong to the engine. You only show them.

---

## 3. Sprite files (`UI/Sprites/<Set>.sprites.json`)

One file per set: `Common.sprites.json` for shared pieces, `USA.sprites.json`, `China.sprites.json`, `Insurgents.sprites.json` for faction pieces. Paths are relative to `UI/`.

```json
{
  "sprites": {
    "USA.Panel":        { "texture": "Textures/USA/T_UI_USA_Panel.png", "size": [256, 256], "margin": [32, 32, 32, 32] },
    "USA.Button":       { "states": {
                            "Normal":   "Textures/USA/T_UI_USA_Button_Normal.png",
                            "Hover":    "Textures/USA/T_UI_USA_Button_Hover.png",
                            "Pressed":  "Textures/USA/T_UI_USA_Button_Pressed.png",
                            "Disabled": "Textures/USA/T_UI_USA_Button_Disabled.png",
                            "Selected": "Textures/USA/T_UI_USA_Button_Selected.png" },
                          "size": [128, 128], "margin": [20, 20, 20, 20] },
    "Common.Icon.Credits": { "texture": "Textures/Common/T_UI_Common_Icon_Credits.png", "size": [64, 64], "tint": true },
    "Common.Ping":      { "texture": "Textures/Common/T_UI_Common_Ping_Sheet.png", "size": [512, 64],
                          "frames": 8, "frameSize": [64, 64], "fps": 12, "tint": true }
  }
}
```

| Key | Meaning |
|---|---|
| `texture` | one PNG |
| `states` | one PNG per button state; a missing state falls back to `Normal` |
| `size` | the exact pixel size of the PNG; the validation test checks it |
| `margin` | 9-slice margins [left, top, right, bottom] in pixels; the corners keep their size, the edges and centre stretch. Leave it out for an image that only scales. |
| `tint` | true: the PNG is white on transparent and the engine colours it (team colour, theme colour, red/yellow/green health) |
| `frames`, `frameSize`, `fps` | a sprite sheet in one row, played as an animation |

### Name resolution

Layouts refer to sprites **without** the set prefix: `"sprite": "Button"`. The engine looks for `<Faction>.Button` for the local player's faction, then `Common.Button`, then a grey placeholder. So:
- a piece that differs per faction gets one entry in each faction file under the same name;
- a shared piece gets one entry in `Common`;
- a faction may override a `Common` piece by giving its own entry under the same name.

Unit, building and upgrade icons are `Icon.<Entity>` (for example `USA.Icon.Tank`). They resolve by the faction of **the entity**, not of the player.

---

## 4. Theme files (`UI/Themes/<Faction>.theme.json`)

```json
{
  "colors": {
    "Primary":   "#2E4A6B",
    "Secondary": "#1B2433",
    "Accent":    "#C9A54A",
    "Panel":     "#14181FEE",
    "Text":      "#E6E9EE",
    "MutedText": "#9AA3B0",
    "Disabled":  "#5A616B",
    "Warning":   "#E0A030",
    "Danger":    "#D0453A",
    "Positive":  "#5FB36A"
  },
  "fonts": {
    "Title":  { "file": "Fonts/<FontName>-SemiBold.ttf", "size": 18 },
    "Body":   { "file": "Fonts/<FontName>-Regular.ttf",  "size": 14 },
    "Number": { "file": "Fonts/<FontName>-Medium.ttf",   "size": 16 }
  }
}
```

- All ten colour names and all three fonts are required. `Neutral.theme.json` fills anything a faction leaves out.
- Fonts: **SIL Open Font License only** (for example Google Fonts). Put the `.ttf` in `UI/Fonts/`, and the font's licence and download link in your spec. `Number` must have tabular (equal-width) figures, so counters do not jitter.
- Body text is at least 14 at 1080p. Text on panels meets WCAG AA contrast (4.5:1, 3:1 for text of 18 and up).

---

## 5. Art rules

### Naming and paths

| What | Path |
|---|---|
| faction HUD piece | `UI/Textures/<Faction>/T_UI_<Faction>_<Piece>.png` |
| shared HUD piece | `UI/Textures/Common/T_UI_Common_<Piece>.png` |
| entity icon | `UI/Textures/<Faction>/Icons/T_Icon_<Faction>_<Entity>.png` |
| command icon | `UI/Textures/<Faction>/Commands/T_Cmd_<Faction>_<Command>.png` |
| illustration | `UI/Textures/<Faction>/Art/T_Art_<Faction>_<Name>.png` |
| mockup (not used by the game) | `docs/ui/mockups/Mockup_<Panel>_<Faction>.png` |

`<Faction>` is exactly `USA`, `China` or `Insurgents`. Every file has exactly the name and pixel size the brief gives.

### Technical

- PNG, transparent unless the brief says otherwise. sRGB.
- 9-slice pieces: keep every detail that must not stretch (corners, rivets, bevel ends) inside the margins; the centre and the edges stretch.
- Tint pieces: pure white with alpha only, no grey shading baked in unless the brief asks for it.
- Textures that must look sharp at 4K are painted at 2x of their 1080p display size. The sizes in the briefs already account for that.
- Button centres stay dark and calm: an icon goes on top.
- States must be recognisable without colour: disabled also has a hatch, selected also has a frame, a warning also has an icon.

### Style (applies to everything you make)

Original work only: nothing copied from or resembling Command & Conquer Generals/Zero Hour or any other game.

The three attached USA air force upgrade icons (radar array, open weapons bay, armour plating) set the **style** for all USA art, not the subject. Ground units, buildings and ground upgrades get the same rendering, lighting, background and materials, but their own subjects. Do not put aircraft into icons that are not about aircraft.

- Painterly-realistic digital illustration, concept-art quality, crisp hard edges, very high detail.
- Subject in a tight, heroic close-up (low angle or 3/4 view), filling 70–85% of the frame; cropping the subject at the edges is fine.
- Materials: olive-drab and gunmetal armour plates, panel seams, rivets and bolts, scratches and light weathering, carbon-fibre panels, thin brass/gold edge trims.
- Lighting: strong cold blue rim light from behind the subject, warm orange/gold accents (sparks, interior lights, warning beacons), deep shadows.
- Background: deep navy fading to near-black, a faint hexagon grid, a soft blue glow around the subject.
- At most one restrained holographic overlay (radar sweep, target bracket), and only when it shows what the thing does.
- No text, numbers, letters or watermarks anywhere. The only real insignia allowed is the US star-and-bar roundel, kept small.
- An icon must still read at 64x64: one clear silhouette, strong value contrast.

Faction variants (same rendering quality and composition rules):
- **USA:** as above (blue rim light, navy hexagon background).
- **China:** red rim light, dark charcoal background with a faint riveted steel-plate grid, heavier industrial forms, red star markings.
- **Insurgents:** sickly green rim light, dark brown-olive background with a faint cracked-concrete texture, improvised materials (welded scrap, sandbags, tarp, cable ties, mismatched paint), no flags or real-world symbols.

HUD pieces (panels, frames, buttons) use the same material language, but restrained: dark gunmetal, thin bevels, rivet details only at corners, the faction colour only on edges and highlights. Panels must never compete with the icons placed on them. No neon, no glassmorphism, no rounded mobile/dashboard look.

---

## 6. The UX spec (`docs/ui/specs/<Panel>.md`)

Every panel brief asks for a short spec in this structure:

1. **Purpose:** what the player uses the panel for, in two sentences.
2. **Anatomy:** each element of the layout by `name`, and what it shows.
3. **States:** every state and what changes visually: empty, normal, hover, pressed, disabled (one row per disabled reason), selected, low power, and the panel's own states from the brief.
4. **Interaction:** mouse (left, right, hover, and the hover delay where it matters), hotkeys shown, timings and animations (durations in ms, what fades or pulses).
5. **Edge cases:** the longest name, the maximum number of items, 0 / 1 / many, a translation 30% longer, every resolution from 16:10 to 21:9.
6. **Accessibility:** the contrast of each text/background pair, and how each state reads without colour.
7. **Requests for code:** binding keys or actions you need but do not have. Leave it empty if there are none.

Keep it under two pages. It is a working document for the programmers, not a pitch.

---

## 7. Delivery

- Make the art first as large sheets. Then use Python to cut, resize and export every file at the exact name and pixel size.
- Hand everything over in **one ZIP whose folders mirror the repository root**, so it can be unpacked over the repo as is:

```
<Panel>_<date>.zip
  UI/Layouts/<Panel>.layout.json
  UI/Sprites/Common.sprites.json        only the entries for this delivery
  UI/Sprites/USA.sprites.json           (the programmers merge them)
  UI/Sprites/China.sprites.json
  UI/Sprites/Insurgents.sprites.json
  UI/Themes/...                         only when the brief asks for themes
  UI/Textures/...                       every PNG
  UI/Fonts/...                          only when the brief asks for fonts
  docs/ui/specs/<Panel>.md
  docs/ui/mockups/Mockup_<Panel>_<Faction>.png
  README.txt                            the list of files, plus anything the brief asks for
```

- Valid JSON only: no comments, no trailing commas.
- Before you hand over, check that every sprite your layout names exists in your sprite entries or in the brief's list of existing sprites, that every `{key}` and `action` is in the brief's lists, and that every PNG has the size its sprite entry declares.

---

## 8. Team workflow

The team is the producer (a person), Claude (the programmer) and you (the UI/UX designer). Each panel has a **code issue** and a **design issue** on GitHub. Who does what, the reviewer, and how each of you reaches the running editor through Unreal MCP is in [`AI_TEAM.md`](AI_TEAM.md).

1. **Claude** completes the code issue: the C++ that supplies the binding keys and carries out the actions, plus a plain placeholder layout and placeholder sprites, so the panel already works. The tests pass.
2. **The producer** sends you the design brief, with this handbook, the placeholder `<Panel>.layout.json` and the style references attached.
3. **You** deliver the ZIP (section 7).
4. **The producer** unpacks it over the repository, runs the setup script (it imports the PNGs and fonts and generates the styles), opens the game and sends you feedback. A round that only changes layout files needs no setup: `rts.UI.Reload` in the running game shows it at once. No code rebuild is needed between rounds.
5. **Claude** runs the UI validation test, fixes anything technical (merges the sprite entries, corrects a path), adds any "Requests for code" as new issues, and commits the delivery with the design issue closed.

Modders follow the same formats: a mod puts its own files under `Mods/<ModName>/UI/`. A layout file there replaces the base layout with the same name, also in a shipped game. Sprite and theme entries there override the base entries with the same name when the setup script runs.
