# World Divide — Design Foundation

**Status:** Working design source for Claude Code issue planning  
**Last consolidated:** 2026-10-14  
**Owner:** Studio Lek River  
**Purpose:** Capture design direction from the game director discussion before creating implementation and content-design GitHub issues. This file is **not** a production specification or permission to implement every feature immediately.

> **Decision labels:** **CONFIRMED** = user approved; **WORKING** = a suggested direction not fully approved; **OPEN** = requires another design decision; **RECONCILE** = conflicts with an existing repository plan or needs technical validation.
>
> **Implementation rule:** Claude Code should translate this into scoped issues and dependencies; do not quietly treat working examples as final names or content. Keep feature scope and release milestones realistic.

## 1. Game identity and priorities

- **CONFIRMED:** A spiritual successor to *Command & Conquer: Generals — Zero Hour*, also influenced by the visual presentation and character animation of the cancelled *Generals 2* and *Red Alert 3*.
- **CONFIRMED:** Alternate, non-COVID 2020s military setting. Mix recognizable equipment from roughly 2000–2020 with plausible near-future systems; fictional variants and memorable names. Historical accuracy is subordinate to enjoyable RTS gameplay.
- **CONFIRMED:** Actual United States and China, with a fictional insurgent/GLA-inspired third faction. Do **not** merely rename or reproduce C&C assets and characters. Original models, dialogue, designs, music, UI and code.
- **CONFIRMED:** Stylized military realism: readable silhouettes from the RTS camera, exaggerated yet plausible military buildings/units, dramatic combat effects, functional animations, recognizable faction architecture and lively base activity.
- **CONFIRMED:** Military-focused and characterful, not a politics simulator. Generals are exaggerated, charismatic, funny and distinctive; cinematics can mix drama and humor.
- **CONFIRMED:** Maintain user direction: critical game-director review, design questions, no arbitrary balancing/roster quotas, avoid uncritical cloning of Zero Hour.

## 2. Factions and generals

**CONFIRMED: 3 factions × 4 commanders each = 12 playable doctrines** (one Standard and three specialists per faction). The Standard General's advantage is broad flexibility — *jack of all trades, master of none* — not an artificial combined-arms bonus. Specialists are strongly asymmetric across units, production, buildings, economy, defenses, upgrades and abilities; each must have practical counters even without identical unit categories.

| Faction | Standard | Specialist 1 | Specialist 2 | Specialist 3 |
|---|---|---|---|---|
| USA | Conventional combined arms | Armor | Air Force | Drone/Cyber |
| China | Broad conventional army | Nuclear | Mass Mobilization | **Incendiary/Napalm** (replaces earlier Electronics direction) |
| Insurgent organization | Adaptable baseline | Chemical | Black Market/Arms Trafficking | Guerrilla Warfare |

- **CONFIRMED:** Chinese Incendiary General focuses on **fire, flame and napalm**, not a general high-explosive/thermobaric doctrine. Distinguish persistent flame hazards from chemical contamination.
- **CONFIRMED:** USA Drone/Cyber includes drones, unmanned ground systems, human infantry and escalating cybernetic augmentation; advanced cyborg infantry is allowed. Its basic drones must be available early, not arbitrarily locked because they're futuristic.
- **CONFIRMED:** All standard factions/generals use recognizable real-world equipment classes; specialists can replace most combat units, retain a few shared units, and use custom variants of a shared chassis. Shared core units can have distinct doctrine-specific kits/upgrades. Roster lengths need not match.
- **CONFIRMED:** Basic naval warfare belongs to all factions/generals on suitable maps. Naval-focused general may be future expansion only (**WORKING**).
- **CONFIRMED:** Every general has its own signature destructive superweapon, personality, portrait, distinctive architecture/insignia and specialist defensive options.

### Character concepts and story roles

- **CONFIRMED:** USA Standard General is **Gen. Nathaniel "Atlas" Ward**, the calm, pragmatic combined-arms commander and face of the USA faction; profile in [`generals/USA_Standard_Ward.md`](generals/USA_Standard_Ward.md).
- **CONFIRMED:** USA and China Standard generals are long-time acquaintances who previously operated together. Their histories remain personal and independently developed; subtle references to old RTS history are okay, not direct character copies.
- **CONFIRMED:** USA Air Force General is a **female, hotheaded, very capable** aviation commander; aggressive belief in bombing and air superiority, notably counters Chinese ground masses. Distinctive confidence, not incompetence.
- **CONFIRMED:** China Mass Mobilization General is impulsive, patriotic and aggressive; initiates an unauthorized retaliatory operation in the campaign.
- **WORKING:** USA Armor General is an older armored-warfare veteran. Drone/Cyber commander is a younger prodigy. Chinese commanders trend older; Nuclear should capture some of Tao's memorable eccentric *energy*, without being a copy.
- **CONFIRMED:** At least one or two USA generals may be women; other genders, ages, names, identities and personal histories remain to be settled.
- **CONFIRMED:** Each general should have an original, expressive 3D character model for communication scenes/cinematics. Static portraits remain useful for lobby/UI, not preferred for main communications.
- **CONFIRMED:** AI-generated voice performances are the practical production path; create original voice profiles, not cloned performances of existing C&C actors.

## 3. Narrative and campaigns

- **CONFIRMED:** Approximately **10 primary missions per faction** (~30), plus optional/branch missions. Player chooses which faction's campaign to begin; no forced campaign order.
- **CONFIRMED:** Three distinct campaigns share a major conflict and some intersecting events but **must not retell identical missions three times**. Each campaign has an alternate, satisfying victory ending for USA, China, or Insurgents (like Red Alert 3); no singular canonical victor required.
- **CONFIRMED:** Predominantly base construction, expansion and capture of old/outlying military installations; occasional specialist/limited-force missions for variety. Primary theater Middle East, other world regions possible.
- **CONFIRMED:** Cinematic inter-mission character scenes and a separate tactical mission briefing; afterward debriefing/strategic consequence feedback is a **WORKING** presentation format.
- **CONFIRMED:** Each campaign controls **its own Standard General for the whole campaign**. Specialists appear as prominent supporting/adversarial characters and shine in Challenge modes. Players act as the commander; don't have the selected general endlessly narrate their own actions. Silent protagonist or minor dialogue choices in cinematics remains **OPEN**.
- **CONFIRMED:** The insurgent campaign begins roughly **two missions before** the central chemical incident, showing preparation of the plot.

### Core war premise — agreed sequence, refine details later

1. **CONFIRMED:** USA and China set disagreements aside to prevent insurgents acquiring dangerous chemical weapons. American and Chinese standard generals have a pre-existing relationship and cooperate in military operations.
2. **WORKING:** An under-resourced US base temporarily guards weapons under a disarmament/security operation, explaining the otherwise implausible poor security.
3. **CONFIRMED:** Insurgents assault it as one part of a larger deception and seize **some** chemical material (not an established chemical arsenal at the start of their campaign).
4. **CONFIRMED:** Insurgents also capture an American facility and **authentic US vehicles/equipment**, then use these to attack Chinese forces, making the US look responsible.
5. **CONFIRMED:** Beijing wants investigation/de-escalation. The Chinese Mass Mobilization General disobeys and attacks an American installation; America responds. The crisis escalates through accumulated incidents and pre-existing mistrust, not a single implausible instant declaration of global war.
6. **CONFIRMED:** The USA Air Force General plays an aggressive retaliatory role. Standard US/Chinese commanders' friendship becomes stressed. Its ultimate fate remains **OPEN**.
7. **CONFIRMED:** The third faction exploits superpower fighting to develop its Chemical General's arsenal. Its eventual victory means forcing foreign forces out of the region and asserting decentralized dominance, **not** creating a conventional new nation-state.
- **CONFIRMED:** Third faction should resemble the organizational role of the GLA: decentralized underground militant movement with regional commanders, diverse ambitions and unconventional methods. It is **the organization** that is the principal adversary, rather than a single ultimate villain. Chemical General should be memorable like Thrax, but original and not above the whole organization.
- **CONFIRMED:** Mostly fictional officer/political characters, minimal overt politics.

### Mission choices and strategic assets

- **CONFIRMED:** Missions start fresh: no persistent physical army, credits or inherited base state.
- **CONFIRMED:** Capturing/freeing strategic assets in missions unlocks *capabilities* in later missions — e.g. sea outpost unlocks naval forces or shore bombardment, airfield opens air units, logistics site grants additional supplies. Once unlocked, an asset stays unlocked for that campaign playthrough; no later recapture/loss system.
- **CONFIRMED:** **Easy, Normal, Hard** only. Easy can unlock all strategic opportunities. Higher difficulties can force mutually exclusive choices/mission routes. Avoid blocking critical story comprehension behind difficulty.
- **CONFIRMED:** Players may replay missions either with the unlocks/resources at original completion or with current campaign unlocks, chosen through an option.
- **CONFIRMED:** Campaign progress resets can be permitted, but using reset or changing difficulty can invalidate certain completion/achievement eligibility; exact rules **OPEN**. No traditional RPG passive skill-tree stat bonuses: 'campaign traits' really mean earned **strategic unlocks** through operations.
- **OPEN:** Precise branching mission maps, character dialogue options, achievement integrity, save-state details and difficulty switch policy.

## 4. Modes and commander unlocks

- **CONFIRMED:** Campaign (three factions), Classic Generals Challenge (Zero Hour style), World/Commander's Challenge (Red Alert 3: Uprising style), and standard Skirmish/Multiplayer.
- **CONFIRMED:** Standard generals available from start. Defeating specialist generals in Challenge unlocks them for Skirmish/Multiplayer. General selection in campaign is **not** an unlock mechanism.
- **WORKING:** Classic and World Challenges share general unlocks rather than making players defeat the same opponent twice.
- **RECONCILE/OPEN:** Competitive fairness: locked specialists in multiplayer versus player preference for challenge-earned unlocks. Decide whether custom-game hosts can opt into all-unlocked or whether competitive modes have a fair universal roster.
- **CONFIRMED:** Special generals are characterized particularly strongly in Challenge, with dramatic presentation and recognizable strongholds.

## 5. General powers and ranks

- **CONFIRMED:** Keep Zero Hour-like **five-star battlefield promotion/general powers**. Rank names can evoke authentic military ranks, while character's formal rank and in-match progression should remain distinct.
- **CONFIRMED:** General powers earned via combat during a match, selected through the familiar general powers interface, and reset for each new match. Powers and bonuses should be individual to each general plus underlying faction identity.
- **CONFIRMED:** Factions have inherent doctrine-wide characteristics (e.g. USA intelligence, China production, insurgent scavenging), but Standard General gets no special combined-arms synergy buff.
- **CONFIRMED:** Generals may unlock their doctrine units/upgrades earlier; advanced tech and superweapons can require research buildings and rank. Do **not** delay core signature units simply because they are 'advanced'.

## 6. Bases, construction, architecture and environmental visuals

- **CONFIRMED:** Free-form Zero Hour-style building placement from builder units anywhere legally reachable; no territory/proximity restriction. Free rotation or snapped rotation increments, at player's choice. Smart alignment and placement preview useful. Production exits must remain clear; terrain slope handling required.
- **CONFIRMED:** USA/China dozers and Insurgent workers form construction baseline; general-specific builders/skins/mechanics may differ.
- **CONFIRMED:** Recognizable faction architecture with specialized general variants. Aim for **2–3 cosmetic building models/variants per general/building**, if feasible; player can manually choose or randomize variation. Shared gameplay footprint/stats for purely cosmetic variants.
- **CONFIRMED:** Do **not** build manual desert/arctic camouflage scheme selection. Instead automatically apply light biome/weather wear: snow on suitable surfaces, desert sand/dust, temperate dirt/moss, urban grime, subtly on vehicles too.
- **CONFIRMED:** Progressive construction animations, scaffolding/machinery, animated production structures, doors/radars/hangars, ambient personnel/vehicles where feasible. Roads between buildings are an **advanced optional idea**, not a committed MVP requirement.
- **CONFIRMED:** Destruction leaves visually persistent ruins; automatic cleanup and immediate rebuild eligibility avoid snowballing. Cosmetic ruins should not obstruct movement/placement after the collapse phase. Vehicle wrecks are a separate, temporary gameplay resource.
- **CONFIRMED:** Animated faction/national flags on buildings, optional **per building** and via global visual toggle.

## 7. Economy, resources, salvage and recovery

- **CONFIRMED:** **One resource: Credits**. No fuel, ammo or materials currencies. Map supply piles are finite. Capturable/contested secondary economic map objects include oil derricks, mines and military stockpiles.
- **CONFIRMED:** Supply collectors are purposeful faction/doctrine-specific units. Standard USA conventional collector (exact type TBD), Armor armored supply truck, Air Force helicopter, Drone/Cyber automated supply drones. China supply trucks; Insurgent workers; more general-specific variants possible.
- **CONFIRMED:** Supply Depot can toggle automatic/manual collector handling. Auto mode needs a sensible player-defined cap and shouldn't waste resources.
- **CONFIRMED:** Each general has a viable secondary income system, with distinctive mechanisms/variants suited to doctrine; no player becomes economically dead after map supply exhaustion. Examples are **WORKING**, not locked final production buildings.
- **CONFIRMED:** Loot from enemy units/bases/convoys can create supply crates, salvage and abandoned stockpiles. **Only dedicated supply units collect economic loot**, not just any combat unit walking over it. Avoid excessive snowball rewards.
- **CONFIRMED:** Insurgents specialize in time-sensitive wreck salvage, potentially equipment/resources/upgrades, with Black Market deepening captured-gear/arms-trade identity. USA and China may recover and repair their own/eligible wrecks using appropriate recovery vehicles. Wrecks expire automatically after a limited time; recovery is slower/costlier than routine repairs.

## 8. Capture, foreign technologies and garrisons

- **CONFIRMED:** **All infantry can capture buildings. Period.** Human, cyborg and robotic infantry all qualify; no special research requirement or type exclusions. Capture time/interrupt rules may still be tuned, without negating universal capture.
- **CONFIRMED:** Players can capture and sell enemy buildings, produce foreign units from captured factories and acquire more of an enemy tech tree by capturing appropriate prerequisite buildings/research.
- **CONFIRMED:** Capturing foreign HQ permits constructing that foreign faction's builder and establishing its tech tree. Captured advanced research can bypass some rank requirements; other building/unit prerequisites still apply. Precise ownership of enemy general-specific content is **OPEN**; don't make capture automatically unlock every foreign doctrine.
- **CONFIRMED:** Compatible civilian buildings can be garrisoned; capacity/firing slots depend on building. Appropriate counter-garrison weapons can clear occupants; severe building damage may kill those inside.

## 9. Infantry, squads, behavior and veterancy

- **CONFIRMED:** Mostly individual infantry units; select generals (especially China Mass Mobilization) can produce squads. Squad survivors fight on; refill at Barracks, or more slowly at HQ, preserving squad experience.
- **CONFIRMED:** Roles, not fixed categories: general-purpose, anti-infantry, anti-armor, anti-air, anti-building, long-range, recon/detection, support and specialist; one unit can cover several roles. Generals may omit roles in infantry while countering them with other arms.
- **CONFIRMED:** Unit-dependent equipment kits and active abilities. Shared USA **basic rifleman** is the same initial unit across all four USA generals, but can receive different doctrine kits/upgrades/abilities.
- **CONFIRMED:** Four *working* baseline USA infantry slots: general-purpose rifleman, missile specialist, heavy gunner, reconnaissance specialist. Size/role allocation is not final; no universal cap. Suggested labels **Vanguard, Lancer, Sentinel, Specter** are **NOT APPROVED FINAL NAMES**.
- **CONFIRMED:** Every unit, including basic troops, gets a distinctive, memorable **original military codename**, not recycled C&C names. Recognizable US uniforms and 3D silhouettes.
- **CONFIRMED:** USA Drone/Cyber trains human soldiers at Barracks, researched augmentations at Cybernetics Lab; allows eventually heavily modified cyborgs. Distinguish humans, augmented humans and autonomous robots; every infantry classification retains capture ability.
- **CONFIRMED:** Light automatic use of cover if feasible, but must never override movement orders or slow the game into a cover tactics simulator.
- **CONFIRMED:** Unit stances/commands are simple: Hold Position, Stand Guard, Pursuit with sensible maximum leash; no elaborate infantry formation-management interface.
- **CONFIRMED:** Three classic veterancy grades: Veteran, Elite, Heroic. Squad veterancy applies to squad as whole. **Heroic units regenerate health**; lower tiers need conventional healing/repairs.

## 10. Vehicles and upgrades

- **CONFIRMED:** Fire on the move where appropriate; accuracy/stabilization penalties may be mitigated by applicable upgrades.
- **CONFIRMED:** Simple directional armor (front strongest, side weaker, rear weakest), not an overly detailed penetration simulator.
- **CONFIRMED:** Avoid general-purpose component-disable mechanics; vehicles otherwise function until destroyed, beyond deliberately authored status effects.
- **CONFIRMED:** Progressive cosmetic damage: dents/scratches, smoke, sparks, flames, readable from camera distance.
- **CONFIRMED:** Global research upgrades and limited **individual permanent kits** (armor, mobility, weapons, sensors, etc.) on appropriate units. Kits are mutually exclusive where their design specifies. Upgrade visuals change when sensible. No requirement every vehicle has kits.
- **CONFIRMED:** Older military equipment remains tactically useful; specialists use meaningful doctrine-specific modifications to shared platforms, not simply statistically superior copies. Major upgrades may shift unit role with meaningful trade-offs.

## 11. Aircraft, smart guard and air defense

- **CONFIRMED:** Separate fighters, ground-attack aircraft, strategic bombers, helicopters and drones. Fixed-wing aircraft generally operate from airfields, return to rearm; helicopters and some drones may loiter. Aircraft carry finite ordnance.
- **CONFIRMED:** Airfields have limited pads/slots; hangar upgrades may increase capacity. Strategic bombers like a B-2-inspired platform are **provisionally General Powers**; ordinary producible strategic bombers remain **OPEN**.
- **CONFIRMED:** Smart Guard — player designates a circular zone; airborne units orbit/patrol, ground units hold position and respond as necessary. Units cooperate on target allocation (e.g., four fighters spread missiles among six supply trucks rather than all selecting one), considering expected damage, allocated salvos and legal weapon targets. Cross-type coordination is supported. Uses only known/detected contacts; does not override direct targeting orders or reposition an army on its own.
- **CONFIRMED:** Aircraft flares, ECM and stealth where appropriate; countermeasures have meaningful counters.
- **CONFIRMED:** Anti-air missile installations have visible burst magazines and reload sequences (e.g. Patriot-inspired battery has four missiles, launches up to four, then reloads). Automatic ammunition replenishment, not ammo as a separate economic currency.
- **CONFIRMED:** Radar installations form an **integrated faction-wide detection network**, improve AA coverage and accuracy, contribute to minimap intel; destroying nodes reduces geographic coverage, not necessarily the global tier of research.

## 12. Stealth and detection

- **CONFIRMED:** Tiered classifications, working labels **Basic / Advanced / Supreme**, opposed by progressively capable radar/sensors. Stealth is not perfectly invisible in all circumstances.
- **CONFIRMED:** Appropriate stealth aircraft, ships, submarines and selected vehicles may improve stealth through upgrades; not every platform qualifies.
- **CONFIRMED:** **Faction-wide detection research** upgrades network capability; not each individual radar manually. Network structures provide coverage and can be destroyed. Some independent local detection units/countermeasures remain possible.
- **WORKING safeguards:** Close-range specialists can reveal even high stealth; firing or active emissions may temporarily increase detectability. Tune so Supreme stealth isn't an automatic no-counter win.

## 13. Naval warfare

- **CONFIRMED:** Integrated but secondary to land RTS. Only suitable maps support building **coastline-only shipyards** (not lakes/inland water placements).
- **CONFIRMED:** Basic navy accessible to all generals; relevant classes include patrol boats, frigates/destroyers, cruisers, submarines, amphibious transports. Shore bombardment, fleet AA, amphibious deployment and coastal objectives matter.
- **CONFIRMED:** **Aircraft carriers are mission-only strategic assets for now**, not ordinary constructible skirmish units. A carrier can act as mobile HQ/airbase in a mission.
- **OPEN:** Exact ship roster and scale compromise; preserve readable world scale, don't make normal maps absurdly large.

## 14. Transports

- **CONFIRMED:** Infantry and suitable light vehicles carried by appropriate helicopter, aircraft or amphibious transport; heavy vehicles require dedicated heavy lift or landing craft. Larger passengers occupy more capacity.
- **CONFIRMED:** Standard helicopter must land to load/unload. Special units/transports may fast-rope deploy without landing.
- **CONFIRMED:** Transport destruction generally kills passengers; particular upgrades/systems can improve survival. Final values and cases TBD.

## 15. Superweapons

- **CONFIRMED:** **One unique destructive superweapon per general (12 total)**, nuke-scale or comparably devastating, not merely utility/support abilities. Distinguish launch methods, area shapes, lingering fire/radiation/chemical effects and identity.
- **CONFIRMED:** Configurable match limits: disabled / one per player / unlimited.
- **CONFIRMED:** Counterplay primarily destroy the emplacement before launch or evacuate. Superweapon projectiles generally **not interceptable**.
- **CONFIRMED:** Construction detection/notifications, countdown and attack warning. Target anywhere including fogged territory. Full friendly fire including own and allied troops/buildings.
- **CONFIRMED:** Research upgrades may increase **damage or add effects**, **not shorten recharge**. Provisional blast radius preview may be useful but shouldn't reveal hidden enemy information.
- **CONFIRMED:** Superweapons gated behind tech and appropriate commander rank; not rushable at rank one. Specialists may have doctrine-appropriate earlier access to supporting technologies.

## 16. Defensive construction and power

- **CONFIRMED:** Shared faction baseline defenses plus specialist structures and doctrine kits. Defensive kits are **permanent on that individual building**; one cannot freely swap them later; sell/rebuild is allowed. Watchtower concepts: surveillance/stealth detection, fire support, AA or command support, subject to clear capacity limits.
- **CONFIRMED:** Player draws freely connected, destructible walls. Gates/towers can snap to wall segments; gates open for friendlies. Garrison infantry in explicitly supported towers/bunkers/other structures. Wall breaches and artillery counters essential.
- **CONFIRMED:** Players can save/reuse defense/base blueprints containing walls, gates, defenses **and ordinary buildings**. Templates must consume normal credits, construction workers and build times — no instant fortified bases.
- **CONFIRMED:** **All factions, including Insurgents, require electricity.** Global faction grid; no local cables or per-building proximity networks.
- **CONFIRMED:** Low power disables radar and powered defenses; slows production and research. No manually assigned power-priority system.
- **CONFIRMED:** Buildable emergency backup generators automatically activate when grid supply becomes insufficient, cover only limited power and don't eliminate the tactical impact of powerplant destruction.
- **CONFIRMED:** Destruction side effects vary: China nuclear/radiation, Insurgent oil generator leak/burning oil, conventional plant explosions. Make hazard footprint/time readable and balanceable.

## 17. Technology and production

- **CONFIRMED:** Standard tiered technology buildings reminiscent of Strategy Center, Propaganda Center and Palace, plus doctrine variants. Research both at production sites and at dedicated tech buildings as appropriate.
- **CONFIRMED:** General rank thresholds vary by doctrine. Advanced units may require research and tech structures; core doctrine units must be available early where necessary. Captured advanced tech may bypass some rank restrictions but not all other prerequisites.
- **CONFIRMED:** Barracks, factories, airfields and shipyards have **independent queues and rally points**; extra buildings add production lanes, not a production-speed bonus to existing ones.
- **CONFIRMED:** Normal click queues one; **Shift-click** toggles continuous repeat for one unit at that building (exact input UX may need conflicts review). Cost charged **when construction starts**, not at order time. Repeat waits for sufficient credits. Player can cancel queues; individual factories can be upgraded with specialized production infrastructure.
- **CONFIRMED:** Default UI is per-building Zero Hour-style production. Optional **large production overview overlay**, opened via HUD/hotkey, displays all production buildings and their queues, progress and idle states, supports selection of individual/multiple same-type producers but does **not** introduce pooled queues. No small expandable panel as the main alternative.
- **OPEN:** Exact rules for cash reservation and queues; optional credit reserve is only a working idea, not confirmed.

## 18. Dialogue and presentation

- **CONFIRMED:** Challenge mode is the richest place for taunts and rivalries; ordinary skirmish/multiplayer can toggle general chatter **Off / Minimal / Full** with events actually visible/known to that side.
- **CONFIRMED:** Character speech reacts to attacks, loss of buildings, power outages, units, opponent tactics, superweapons, victory/defeat and key gameplay events. No omniscient scouting via taunts.
- **CONFIRMED:** Avoid full pair-specific dialogue for every possible matchup. Use general-specific reusable taunts, doctrine/event lines and a **small number of bespoke rivalry exchanges**. Voice variation and memorable humor matter more than raw line counts.
- **CONFIRMED:** Enemy/AI commanders taunt player; selected player general normally does not narrate the user's tactical decisions. Friendly officers provide battlefield reports. Player-general's scripted cinematic speech or cosmetic dialogue choices remain **OPEN**.
- **CONFIRMED:** 3D talking-head communications when feasible; 2D static portraits remain useful in UI and as production fallback.
- **REFERENCE:** `taunts.txt` supplied by user is a style/tone study of Zero Hour generals (arrogance, eccentricity, situational remarks); **do not directly reuse lines or performances**.

## 19. Known repo alignment work for Claude Code

Current repository is an Unreal Engine 5.8 C++/Blueprint/data-driven RTS with economy, combat, aircraft, buildings, multiplayer lobby and prototype AI; design is ahead of implementation. Key existing issues referenced in earlier review:

- **Issue #14:** Describes fictional Insurgents as `bUsesPower = false`; **RECONCILE** with current confirmed 'all factions need electricity'.
- **Issue #73:** Commander data prototype for 3 generals per faction; **RECONCILE** with four per faction, one standard + three specialist, and unique doctrine support.
- **Issue #75:** Existing *nine generals* list and **2012** setting. It proposed China Electronics, US Air Force/Tank/Drone, China Mobilization/Nuclear and Insurgent Chemical/Black Market/Guerrilla. **RECONCILE** to 12 generals, alternate 2020s and **China Incendiary instead of Electronics**, and audit all linked tasks.
- **Issue #102:** General portrait art for existing nine/2012 theme; **RECONCILE** count, roster, cinematic 3D direction, character assignments and references before commissioning assets.
- **Issue #105:** JSON content authoring workflow; use for data-first rosters and balancing once design specs are approved.
- Cross-check existing HUD, naval, upgrades, faction data and visual design issues against new mechanics before raising duplicates.

### Suggested issue decomposition (planning, not authorization to change GitHub)

1. **Design governance:** Confirm this document as design source; establish decision records and milestone priorities; update contradictory existing design issues.
2. **Story and characters:** One world/lore brief, three campaign outline issues, standard-general biographies, specialist-general profiles, cinematic/taunt style guide.
3. **Faction/core rosters:** USA Standard + Armor + Air Force + Drone/Cyber **designed side by side**; then China four; then Insurgents four. Each unit to record codename, inspiration, role/counters, tier/rank, prerequisites, cost target, weapons, active abilities, kits, cosmetics.
4. **Gameplay foundations:** Universal capture; faction power; smart guard; independent queues/repeat/overlay; upgrade kits; radar and stealth tiers; construction and wall templates; salvage and wreck expiration; squad refill; naval interoperability.
5. **Meta/campaign:** Challenge unlock persistence & competitive rules, strategic asset progression, mission replay settings, achievement/difficulty integrity.
6. **Presentation:** Faction/general visual language, building variations, environmental overlays, animated flags, ambient bases, 3D dialogue interface and AI voice pipeline.
7. **Test and balancing:** Acceptance tests for no-snowball ruin cleanup, foreign-tech capture, overkill allocation, multi-factory economics, superweapon configurations, out-of-power behavior and counterplay for every doctrine.

> **Scope warning:** 12 highly asymmetric generals, 12 unique superweapons, ~30 campaign missions, branching objectives, two challenges, 3D cinematics, naval gameplay and modular defenses are a **large multi-year content ambition**. Recommend phased prototypes and milestones while preserving the eventual vision.

## 20. Next design work: USA four-way roster

**Next session focus:** Flesh out the **USA Standard General's basic roster**, while simultaneously tracking overlap and differentiation against Armor, Air Force, Drone/Cyber. Do **not** design a carbon copy of Zero Hour's Ranger/Missile Defender/Burton roster.

**Confirmed starting ground rules:**
- Standard USA is broad, straightforward combined arms without bespoke combined-arms buffs.
- Four *working* basic infantry roles (rifleman / versatile missile specialist / heavy gunner / recon) are a starting point, not a requirement.
- Basic rifleman is shared across all four USA generals; doctrine-specific upgrades/kits/abilities may differ.
- Standard can field familiar equipment derived from Abrams, Bradley, Humvee, Apache etc., with original memorable unit codenames; technology era does **not** imply rank tier.
- No fixed roster count; every unit must have a distinct tactical function and practical weaknesses.

**Open for user input next:** Names and identities of the four basic infantry; whether any two roles should merge; early-game anti-air and anti-building coverage; which vehicles/aircraft support Standard and which are reserved for specialists; distinctive standard-general identity beyond flexibility.
