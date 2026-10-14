# Placeholder art

Stand-in models for testing. All of them are CC0 (public domain): free to use, change and ship, no attribution
required. They are credited here anyway. The models under `Original/` are our own work. `Tools/Editor/setup_testmap.py` imports them through `placeholder_art.py`.

| File | Used for | Source | Author |
|---|---|---|---|
| `Original/usa_mbt.glb` | Tank (USA main battle tank) | Built by `Tools/Art/build_usa_tank_blender.py`; `usa_mbt.blend` next to it | Lek River Studio |
| `Quaternius/tank.glb` | Unused, the former tank | [Tank](https://poly.pizza/m/jWS1CLA0RO) | Quaternius |
| `Kenney/MiniCharacters/character-male-a.glb` | Infantry | [Mini Characters](https://kenney.nl/assets/mini-characters) | Kenney |
| `Kenney/CarKit/delivery.glb` | Harvester | [Car Kit](https://kenney.nl/assets/car-kit) | Kenney |
| `Kenney/CarKit/tractor-shovel.glb` | Builder | [Car Kit](https://kenney.nl/assets/car-kit) | Kenney |
| `Kenney/CityKitIndustrial/building-m.glb` | Power Plant | [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | Kenney |
| `Kenney/CityKitIndustrial/building-j.glb` | Supply Depot | [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | Kenney |
| `Kenney/CityKitIndustrial/building-l.glb` | Vehicle Factory | [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | Kenney |
| `Kenney/CityKitCommercial/building-skyscraper-a.glb` | Headquarters | [City Kit (Commercial)](https://kenney.nl/assets/city-kit-commercial) | Kenney |
| `Kenney/SurvivalKit/tent-canvas.glb` | Barracks | [Survival Kit](https://kenney.nl/assets/survival-kit) | Kenney |
| `Kenney/TowerDefenseKit/tower-round-build-c.glb` | Tower | [Tower Defense Kit](https://kenney.nl/assets/tower-defense-kit) | Kenney |
| `Kenney/CastleKit/wall.glb` | Test map walls | [Castle Kit](https://kenney.nl/assets/castle-kit) | Kenney |
| `Kenney/FactoryKit/box-wide.glb` | Supply source, airfield | [Factory Kit](https://kenney.nl/assets/factory-kit) | Kenney |
| `Quaternius/Spaceship/spaceship.glb` | Jet | [Spaceship](https://poly.pizza/m/uCeLfsdmNP) | Quaternius |

Each Kenney folder also holds that kit's `Textures/colormap.png`.

The tank comes skinned and animated. Unreal places skinned parts wrongly when it imports them as a static mesh, so it
was flattened into its rest pose with `Tools/Art/bake_gltf_static.py`. The jet came with its texture inside the `.glb`;
that texture was moved out to `Quaternius/Spaceship/Textures/atlas.png`, since the importer only reads textures next to
the model. The other models are unchanged.

The tank is generated: change `Tools/Art/build_usa_tank_blender.py` and run it with Blender
(`blender -b --python Tools/Art/build_usa_tank_blender.py`), which rewrites the `.glb` and `.blend` and renders previews
to `Saved/ArtPreviews`.

To swap a model, put the new `.glb` (with its textures next to it) in this folder, point the `make_art` call in
`setup_testmap.py` at it, and run `ue.ps1 -Task setup`.
