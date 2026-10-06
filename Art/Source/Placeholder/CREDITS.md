# Placeholder art

Stand-in models for testing. All of them are CC0 (public domain): free to use, change and ship, no attribution
required. They are credited here anyway. `Tools/Editor/setup_testmap.py` imports them through `placeholder_art.py`.

| File | Used for | Source | Author |
|---|---|---|---|
| `Quaternius/tank.glb` | Tank | [Tank](https://poly.pizza/m/jWS1CLA0RO) | Quaternius |
| `Kenney/MiniCharacters/character-male-a.glb` | Infantry | [Mini Characters](https://kenney.nl/assets/mini-characters) | Kenney |
| `Kenney/CarKit/delivery.glb` | Harvester | [Car Kit](https://kenney.nl/assets/car-kit) | Kenney |
| `Kenney/CityKitIndustrial/building-m.glb` | Power Plant | [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | Kenney |
| `Kenney/CityKitIndustrial/building-j.glb` | Supply Depot | [City Kit (Industrial)](https://kenney.nl/assets/city-kit-industrial) | Kenney |
| `Kenney/TowerDefenseKit/tower-round-build-c.glb` | Tower | [Tower Defense Kit](https://kenney.nl/assets/tower-defense-kit) | Kenney |
| `Kenney/CastleKit/wall.glb` | Test map walls | [Castle Kit](https://kenney.nl/assets/castle-kit) | Kenney |
| `Kenney/FactoryKit/box-wide.glb` | Supply source | [Factory Kit](https://kenney.nl/assets/factory-kit) | Kenney |

Each Kenney folder also holds that kit's `Textures/colormap.png`.

The tank comes skinned and animated. Unreal places skinned parts wrongly when it imports them as a static mesh, so it
was flattened into its rest pose with `Tools/Art/bake_gltf_static.py`. The other models are unchanged.

To swap a model, put the new `.glb` (with its textures next to it) in this folder, point the `make_art` call in
`setup_testmap.py` at it, and run `ue.ps1 -Task setup`.
