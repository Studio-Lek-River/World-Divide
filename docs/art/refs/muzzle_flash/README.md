# Tank gun firing: reference sheet (#294, #303)

What the M20's firing effect copies from real tank guns, layer by layer. Links only: no photo, frame or video is copied
into the repository, and nothing comes from games or films. All photos are U.S. government works marked public domain on
DVIDS; they are reference, not source art. The look itself is baked from our own Blender simulation
(`Tools/Art/muzzle_fx_blender.py`).

## Stills

| # | Still | Time after firing | What it shows |
|---|---|---|---|
| 1 | [M1A2, Karliki Range, 2017 (DVIDS 3799408)](https://www.dvidshub.net/image/3799408/tankfire) | ~0.01–0.03 s | 120 mm, no brake: a short white-yellow core at the muzzle and a flame tongue thrown forward along the bore |
| 2 | [M1A2, Grafenwöhr, 2020 (DVIDS 6439249)](https://www.dvidshub.net/image/6439249/boom) | ~0.03–0.08 s | the fireball at its largest, orange, ragged and turbulent, well in front of the muzzle |
| 3 | [M1128 MGS, Donnelly, 2013 (DVIDS 1043494)](https://www.dvidshub.net/image/1043494/mgs-gunnery) | ~0.02–0.05 s | 105 mm with a muzzle brake: the blast split, fire thrown out sideways from the brake as well as forward |
| 4 | [M1A2, Pabrade, 2015 (DVIDS 1864912)](https://www.dvidshub.net/image/1864912) | ~0.1–1 s | the pressure wave raising dust off the ground in front of and around the tank |
| 5 | [M1A2, Fort Hood, 2015 (DVIDS 2323515)](https://dvidshub.net/image/2323515/abrams-fire) | ~0.2–3 s | the grey-brown blast cloud rolling out of the fire and hanging, then drifting off |

Timing comes from the muzzle-blast literature, not from the photos: the precursor and the main flash last a few
milliseconds, the secondary flash (the fireball, unburnt propellant gas igniting in air) peaks around 10–50 ms and is gone
by about 0.15 s; the near field reaches 1–1.25 m from a 152 mm muzzle ([high-speed imaging of a 152 mm
howitzer](https://www.science.gov/topicpages/m/muzzle+blast+wave)); a more effective brake throws a stronger side blast
([DTIC ADA139766](https://apps.dtic.mil/sti/pdfs/ADA139766.pdf)).

## Layers

| Layer | Copies | From | Timing |
|---|---|---|---|
| Core | a tiny white-hot flare right at the muzzle, the brightest thing in the frame | 1 | full in 1–2 frames, gone by ~0.1 s |
| Blast cone | a narrow flame tongue stretched along the bore, longer than wide | 1 | 0–0.08 s |
| Brake jets | flame thrown sideways (left and right) from the brake; guns without a brake (M20) have none | 3 | 0–0.06 s |
| Fireball | a ragged, billowing ball in front of the muzzle, white-yellow when it lights, orange at its largest, dark red and smoky as it cools | 2 | 0.01–0.2 s |
| Ground blast | a low ring of dust kicked off the ground under and ahead of the muzzle, only when the gun is low | 4 | 0.05–0.6 s |
| Blast smoke (#303) | a dense grey-brown cloud billowing out of the fireball along the bore and sideways, lit by the sun | 5 | 0.1–0.8 s |
| Drifting smoke (#303) | the cloud grown large and thin, drifting and rising, gone in a few seconds | 5 | 0.8–3 s |
| Muzzle light | the flash lighting the hull and ground: a sharp peak, then a quick flicker down | 1, 2 | peak 0–0.015 s, out by 0.15 s |
