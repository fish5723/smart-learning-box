# -*- coding: utf-8 -*-
"""Build game_database.json from rom_inventory.csv. Never opens ROM contents."""
import os, csv, json, re
from collections import defaultdict, Counter

BASE = r"e:\IOT_competition\smart-learning-box"
INV = os.path.join(BASE, "rom_inventory.csv")
CLASS_CSV = os.path.join(BASE, "game_classification.csv")  # optional enrichment
OUT = os.path.join(BASE, "assets", "game_database.json")

EXCLUDE_RE = re.compile(r"\(Hack\)|\(Proto\)|\(Demo\)|\(Unl\)|\(Beta\)|\(Pirate\)|\(Bootleg\)", re.I)

def base_name(fn):
    n = os.path.splitext(fn)[0]
    n = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", n)
    return n.strip()

def tags(fn):
    return re.findall(r"[\(\[]([^\)\]]*)[\)\]]", os.path.splitext(fn)[0])

# --- load optional classification csv: 编号,名称,类型,tags,年份 ---
classmap = {}
if os.path.exists(CLASS_CSV):
    with open(CLASS_CSV, encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            vals = list(row.values())
            folder = str(vals[0]).strip().zfill(4)
            classmap[folder] = {
                "name": (vals[1] or "").strip() if len(vals) > 1 else "",
                "type": (vals[2] or "").strip() if len(vals) > 2 else "",
                "tags": (vals[3] or "").strip() if len(vals) > 3 else "",
                "year": (vals[4] or "").strip() if len(vals) > 4 else "",
            }

# --- group inventory by folder ---
folders = defaultdict(list)
with open(INV, encoding="utf-8-sig") as f:
    for r in csv.DictReader(f):
        parts = r["path"].split("/")
        folder = parts[1] if len(parts) > 2 else "ROOT"
        folders[folder].append(r)

def rank(fn):
    """default_rom priority: (U)[!] > (E)[!] > (J)[!] > plain > FDS. Lower = better."""
    is_fds = fn.lower().endswith(".fds")
    verified = "[!]" in fn
    region = 9
    if re.search(r"\((U|UE|W)\)", fn):  region = 0
    elif re.search(r"\(E\)", fn):        region = 1
    elif re.search(r"\(J\)", fn):        region = 2
    else:                                region = 3
    # verified beats unverified within region; FDS last
    return (1 if is_fds else 0, region, 0 if verified else 1, fn)

games, multi_count, missing_class = [], 0, 0
type_counter = Counter()

for i, folder in enumerate(sorted(folders), 1):
    files = folders[folder]
    non_excluded = [x for x in files if not EXCLUDE_RE.search(x["filename"])]
    pool = non_excluded if non_excluded else files  # fall back if all excluded

    best = sorted(pool, key=lambda x: rank(x["filename"]))[0]

    # name: prefer classification csv, else most common derived base name
    meta = classmap.get(folder, {})
    if meta.get("name"):
        name = meta["name"]
    else:
        name = Counter(base_name(x["filename"]) for x in files).most_common(1)[0][0]

    variants = []
    for x in files:
        variants.append({
            "filename": x["filename"],
            "path": x["path"],
            "extension": x["extension"],
            "size": int(x["size"]),
            "tags": tags(x["filename"]),
            "excluded": bool(EXCLUDE_RE.search(x["filename"])),
        })

    gtype = meta.get("type", "")
    year = meta.get("year", "")
    ctags = [t.strip() for t in meta.get("tags", "").split(",") if t.strip()]

    if not gtype:
        missing_class += 1
    else:
        type_counter[gtype] += 1
    if len(files) > 1:
        multi_count += 1

    games.append({
        "id": i,
        "folder": folder,
        "name": name,
        "type": gtype,
        "year": year,
        "tags": ctags,
        "default_rom": best["filename"],
        "default_path": best["path"],
        "variant_count": len(files),
        "variants": variants,
    })

db = {
    "meta": {
        "source": "rom_inventory.csv",
        "note": "Metadata only; ROM contents never read. For ESP32-P4 LVGL list UI.",
        "total_games": len(games),
        "total_rom_files": sum(g["variant_count"] for g in games),
        "multi_version_games": multi_count,
        "missing_classification": missing_class,
        "categories": dict(type_counter),
        "category_count": len(type_counter),
        "classification_csv_found": bool(classmap),
    },
    "games": games,
}

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(db, f, ensure_ascii=False, indent=2)

print("GAMES=", len(games))
print("ROM_FILES=", db["meta"]["total_rom_files"])
print("MULTI=", multi_count)
print("CATEGORIES=", db["meta"]["category_count"], dict(type_counter))
print("MISSING_CLASS=", missing_class)
print("CLASS_CSV_FOUND=", bool(classmap))
print("OUT=", OUT)
