"""Measures Zero Hour's proportions straight from the user's own install and prints them in World Divide metres, for
docs/tech/scale/ZH_PROPORTIONS.md. Nothing is extracted to disk: only numbers come out.

  python -I Tools/Art/zh_scale_report.py ["<Zero Hour install dir>"]

Reads the object geometry, locomotor speeds and weapon ranges from INIZH.big, the camera from GameData.ini and the
official multiplayer maps (playable extent, start and supply positions) from Maps/MapCache.ini in MapsZH.big. ZH
distances are world units; ZH units are oversized next to its buildings, so everything is converted through an anchor:
A puts the ZH main battle tank at our MBT's 9.8 m, B puts ZH infantry at our 1.8 m soldier. Plain desktop Python.
"""

import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from extract_big import read_entries  # noqa: E402

DEFAULT_INSTALL = r"H:\Program Files\Steam\steamapps\common\Command & Conquer Generals - Zero Hour"

OUR_MBT_LENGTH_M = 9.8
OUR_INFANTRY_HEIGHT_M = 1.8
MBTS = ["AmericaTankCrusader", "ChinaTankBattleMaster", "GLATankScorpion"]
INFANTRY = ["AmericaInfantryRanger", "ChinaInfantryRedguard", "GLAInfantryRebel"]

# Our building: (standard footprint m, standard height m, game footprint m, ZH equivalents).
BUILDINGS = [
    ("Headquarters", (28, 28), "18-25", (38, 56), ["AmericaCommandCenter", "ChinaCommandCenter", "GLACommandCenter"]),
    ("Vehicle factory", (24, 24), "12-14", (24, 24), ["AmericaWarFactory", "ChinaWarFactory", "GLAArmsDealer"]),
    ("Power plant", (20, 20), "12-16", (20, 20), ["AmericaPowerPlant", "ChinaPowerPlant"]),
    ("Barracks", (16, 16), "6-8", (16, 16), ["AmericaBarracks", "ChinaBarracks", "GLABarracks"]),
    ("Supply depot", (16, 16), "8-10", (16, 16), ["AmericaSupplyCenter", "ChinaSupplyCenter", "GLASupplyStash"]),
    ("Superweapon", (24, 24), "30-35", (24, 24),
     ["AmericaParticleCannonUplink", "ChinaNuclearMissileLauncher", "GLAScudStorm"]),
    ("Defence tower", (6, 6), "10-12", (6, 6), ["AmericaPatriotBattery", "ChinaGattlingCannon", "GLAStingerSite"]),
    ("Airfield", (24, 48), "1-2", (24, 48), ["AmericaAirfield", "ChinaAirfield"]),
    ("Supply source", (12, 12), "2-4", (12, 12), ["SupplyDock", "SupplyWarehouse"]),
]

# Our unit class: (reference length m, ZH equivalents).
UNITS = [
    ("Main battle tank", 9.8, MBTS),
    ("Light vehicle / IFV", 7.5, ["AmericaVehicleHumvee", "ChinaVehicleTroopCrawler", "GLAVehicleTechnicalChassisOne"]),
    ("AA vehicle", 7.0, ["AmericaTankAvenger", "ChinaTankGattling", "GLAVehicleQuadCannon"]),
    ("Supply truck (harvester)", 10.0, ["ChinaVehicleSupplyTruck"]),
    ("Builder", 6.5, ["AmericaVehicleDozer", "ChinaVehicleDozer"]),
    ("Infantry", 0.5, INFANTRY),
    ("Jet (0.6x)", 10.0, ["AmericaJetRaptor", "ChinaJetMIG"]),
]


def big_files(path, prefix):
    with open(path, "rb") as file:
        data = file.read()
    return {name: data[offset:offset + size].decode("latin-1")
            for name, offset, size in read_entries(data) if name.startswith(prefix)}


def strip_comments(text):
    return "\n".join(line.split(";")[0].split("//")[0] for line in text.splitlines())


def top_blocks(text, keyword):
    """(name, body, base) for every top-level `keyword Name` block; `ObjectReskin New Old` gives base Old."""
    pattern = r"^(%s|%sReskin)[ \t]+(\S+)(?:[ \t]+(\S+))?(.*?)(?=^\w|\Z)" % (keyword, keyword)
    for match in re.finditer(pattern, text, re.M | re.S):
        base = match.group(3) if match.group(1).endswith("Reskin") else None
        yield match.group(2), match.group(4), base


def field(body, key):
    match = re.search(r"^\s*%s\s*=\s*(.+?)\s*$" % key, body, re.M)
    return match.group(1) if match else None


def number(body, key):
    """The leading number of the value, read the way the game's atof reads typos such as "40v.0" or "18.5.0"."""
    match = re.match(r"-?\d*\.?\d+", field(body, key) or "")
    return float(match.group(0)) if match else None


def read_objects(ini):
    objects = {}
    for path, text in ini.items():
        if not path.startswith("Data/INI/Object/"):
            continue
        for name, body, base in top_blocks(strip_comments(text), "Object"):
            entry = dict(objects.get(base, {})) if base else {}
            shape = field(body, "Geometry")
            if shape:
                entry.update(shape=shape.upper(), major=number(body, "GeometryMajorRadius") or 0.0,
                              minor=number(body, "GeometryMinorRadius") or 0.0,
                              height=number(body, "GeometryHeight") or 0.0)
            locomotor = re.search(r"^\s*Locomotor\s*=\s*SET_NORMAL\s+(\S+)", body, re.M)
            weapon = re.search(r"^\s*Weapon\s*=\s*PRIMARY\s+(\S+)", body, re.M)
            if locomotor:
                entry["locomotor"] = locomotor.group(1)
            if weapon:
                entry["weapon"] = weapon.group(1)
            for key in ("VisionRange", "BuildCost", "BuildTime"):
                if number(body, key) is not None:
                    entry[key] = number(body, key)
            objects.setdefault(name, {}).update(entry)
    return objects


def read_numbers(text, keyword, key):
    return {name: number(body, key) for name, body, _ in top_blocks(strip_comments(text), keyword)}


def footprint(entry):
    """Length (along X) x width x height in ZH units."""
    if entry["shape"] == "BOX":
        return 2 * entry["major"], 2 * entry["minor"], entry["height"]
    return 2 * entry["major"], 2 * entry["major"], entry["height"]


def read_maps(maps_big):
    maps = []
    for name, body, _ in top_blocks(strip_comments(maps_big["Maps/MapCache.ini"]), "MapCache"):
        if field(body, "isOfficial") != "yes" or field(body, "isMultiplayer") != "yes":
            continue
        decoded = re.sub(r"_([0-9A-F]{2})", lambda match: chr(int(match.group(1), 16)), name)
        extent = re.search(r"X:([\d.]+)\s+Y:([\d.]+)", field(body, "extentMax"))
        starts = [tuple(map(float, re.search(r"X:([-\d.]+)\s+Y:([-\d.]+)", value).groups()))
                  for value in re.findall(r"^\s*Player_\d+_Start\s*=\s*(.+)$", body, re.M)]
        distances = [math.dist(a, b) for i, a in enumerate(starts) for b in starts[i + 1:]]
        maps.append({"name": os.path.splitext(os.path.basename(decoded.replace("\\", "/")))[0].title(),
                     "players": int(field(body, "numPlayers")), "w": float(extent.group(1)),
                     "h": float(extent.group(2)), "nearest": min(distances) if distances else 0.0,
                     "supplies": len(re.findall(r"^\s*supplyPosition\s*=", body, re.M))})
    return sorted(maps, key=lambda entry: (entry["players"], entry["w"] * entry["h"]))


def average(values):
    values = [value for value in values if value]
    return sum(values) / len(values) if values else 0.0


def main():
    install = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_INSTALL
    ini = big_files(os.path.join(install, "INIZH.big"), "Data/INI/")
    objects = read_objects(ini)
    speeds = read_numbers(ini["Data/INI/Locomotor.ini"], "Locomotor", "Speed")
    ranges = read_numbers(ini["Data/INI/Weapon.ini"], "Weapon", "AttackRange")
    game_data = strip_comments(ini["Data/INI/GameData.ini"])
    maps = read_maps(big_files(os.path.join(install, "MapsZH.big"), "Maps/MapCache.ini"))

    missing = [name for _, *_, names in BUILDINGS for name in names if "shape" not in objects.get(name, {})]
    missing += [name for _, _, names in UNITS for name in names if "shape" not in objects.get(name, {})]
    zh_mbt = average([footprint(objects[name])[0] for name in MBTS])
    zh_infantry = average([footprint(objects[name])[2] for name in INFANTRY])
    scale_a = OUR_MBT_LENGTH_M / zh_mbt
    scale_b = OUR_INFANTRY_HEIGHT_M / zh_infantry

    print("Objects parsed: %d; maps: %d; missing: %s" % (len(objects), len(maps), ", ".join(missing) or "none"))
    print("ZH MBT geometry length %.1f u -> anchor A %.4f m/u; ZH infantry height %.1f u -> anchor B %.4f m/u"
          % (zh_mbt, scale_a, zh_infantry, scale_b))
    print("Infantry under anchor A: %.1f m tall\n" % (zh_infantry * scale_a))

    print("## Buildings (ZH geometry L x W x H in units; metres under anchor A)\n")
    print("| Our building | ZH object | ZH L x W x H (u) | L/MBT | A: L x W x H (m) | Standard | Game |")
    print("|---|---|---|---:|---|---|---|")
    for ours, standard, height, game, names in BUILDINGS:
        for name in names:
            if "shape" not in objects.get(name, {}):
                continue
            length, width, tall = footprint(objects[name])
            print("| %s | %s | %.0f x %.0f x %.0f %s | %.1f | %.1f x %.1f x %.1f | %d x %d, %s | %d x %d |"
                  % (ours, name, length, width, tall, objects[name]["shape"].lower(), length / zh_mbt,
                     length * scale_a, width * scale_a, tall * scale_a, standard[0], standard[1], height,
                     game[0], game[1]))

    print("\n## Units (ZH geometry; speed and range as multiples of the ZH MBT length)\n")
    print("| Our class | ZH object | L x W x H (u) | A: L x W (m) | Speed u/s | Speed MBT-len/s | Range u | "
          "Range MBT-len | Vision MBT-len |")
    print("|---|---|---|---|---:|---:|---:|---:|---:|")
    for ours, _, names in UNITS:
        for name in names:
            entry = objects.get(name, {})
            if "shape" not in entry:
                continue
            length, width, tall = footprint(entry)
            speed = speeds.get(entry.get("locomotor")) or 0.0
            reach = ranges.get(entry.get("weapon")) or 0.0
            print("| %s | %s | %.0f x %.0f x %.0f | %.1f x %.1f | %.0f | %.2f | %.0f | %.1f | %.1f |"
                  % (ours, name, length, width, tall, length * scale_a, width * scale_a, speed, speed / zh_mbt,
                     reach, reach / zh_mbt, entry.get("VisionRange", 0.0) / zh_mbt))

    print("\n## Camera\n")
    for key in ("CameraHeight", "MinCameraHeight", "MaxCameraHeight", "CameraPitch"):
        value = number(game_data, key)
        print("- %s = %.1f u (%.1f MBT lengths, %.0f m under anchor A)" % (key, value, value / zh_mbt, value * scale_a)
              if "Height" in key else "- %s = %.1f" % (key, value))

    print("\n## Official multiplayer maps (playable extent; metres under anchor A)\n")
    print("| Map | Players | Extent (u) | A: extent (m) | MBT lengths across | Nearest start-start (u / m) | Supplies |")
    print("|---|---:|---|---|---:|---|---:|")
    for entry in maps:
        print("| %s | %d | %.0f x %.0f | %.0f x %.0f | %.0f | %.0f / %.0f | %d |"
              % (entry["name"], entry["players"], entry["w"], entry["h"], entry["w"] * scale_a, entry["h"] * scale_a,
                 max(entry["w"], entry["h"]) / zh_mbt, entry["nearest"], entry["nearest"] * scale_a,
                 entry["supplies"]))
    for players in sorted({entry["players"] for entry in maps}):
        group = [entry for entry in maps if entry["players"] == players]
        print("\n%d players: %d maps, median longest side %.0f u (%.0f m), median nearest start-start %.0f m"
              % (players, len(group), median([max(e["w"], e["h"]) for e in group]),
                 median([max(e["w"], e["h"]) for e in group]) * scale_a,
                 median([e["nearest"] for e in group]) * scale_a), end="")
    print()


def median(values):
    values = sorted(values)
    middle = len(values) // 2
    return values[middle] if len(values) % 2 else (values[middle - 1] + values[middle]) / 2


if __name__ == "__main__":
    main()
