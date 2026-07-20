# -*- coding: utf-8 -*-
"""Full ROM rescan ROM/0001..ROM/1252. Metadata only (os.walk + getsize). Never opens/moves/modifies ROMs.
Outputs game_database.json (no classification), rom_inventory.csv, and a stats report."""
import os, csv, json, re
from collections import defaultdict, Counter

ROM_ROOT = r"F:\ROM"
OUT = r"e:\IOT_competition\smart-learning-box"
ALLOWED_EXT = {".nes", ".fds", ".bin"}
EXCLUDE_RE = re.compile(r"\(Hack\)|\(Proto\)|\(Demo\)|\(Beta\)|\(Pirate\)|\(Bootleg\)", re.I)

def base_name(fn):
    n = os.path.splitext(fn)[0]
    return re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", n).strip()

def tags(fn):
    return re.findall(r"[\(\[]([^\)\]]*)[\)\]]", os.path.splitext(fn)[0])

def rank(fn):
    """default_rom priority: (W)[!] > (U)[!] > (E)[!] > (J)[!] > plain > FDS. Lower=better."""
    is_fds = fn.lower().endswith(".fds")
    verified = 0 if "[!]" in fn else 1
    if   re.search(r"\((W)\)", fn):      region = 0
    elif re.search(r"\((U|UE)\)", fn):   region = 1
    elif re.search(r"\((E)\)", fn):      region = 2
    elif re.search(r"\((J|JU)\)", fn):   region = 3
    else:                                region = 4
    return (1 if is_fds else 0, region, verified, fn)

# ---- scan (metadata only) ----
rows = []                       # inventory rows
folders = defaultdict(list)     # folder -> [file dicts]
for dirpath, dirnames, filenames in os.walk(ROM_ROOT):
    for fn in filenames:
        ext = os.path.splitext(fn)[1].lower()
        if ext not in ALLOWED_EXT:
            continue
        full = os.path.join(dirpath, fn)
        size = os.path.getsize(full)               # metadata only
        rel  = os.path.relpath(full, os.path.dirname(ROM_ROOT)).replace("\\", "/")
        folder = os.path.basename(dirpath)
        rec = {"folder": folder, "path": rel, "filename": fn,
               "extension": ext.lstrip("."), "size": size}
        rows.append(rec)
        folders[folder].append(rec)

rows.sort(key=lambda r: r["path"])

# ---- rom_inventory.csv ----
csv_path = os.path.join(OUT, "rom_inventory.csv")
with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f)
    w.writerow(["id", "path", "filename", "extension", "size"])
    for i, r in enumerate(rows, 1):
        w.writerow([i, r["path"], r["filename"], r["extension"], r["size"]])

# ---- game_database.json (one folder = one game, no classification) ----
games, multi = [], 0
for i, folder in enumerate(sorted(folders), 1):
    files = folders[folder]
    non_excl = [x for x in files if not EXCLUDE_RE.search(x["filename"])]
    pool = non_excl if non_excl else files          # keep a runnable version if all excluded
    best = sorted(pool, key=lambda x: rank(x["filename"]))[0]
    name = Counter(base_name(x["filename"]) for x in files).most_common(1)[0][0]
    variants = [{"filename": x["filename"], "path": x["path"], "extension": x["extension"],
                 "size": x["size"], "tags": tags(x["filename"]),
                 "excluded": bool(EXCLUDE_RE.search(x["filename"]))} for x in files]
    if len(files) > 1:
        multi += 1
    games.append({
        "id": i, "folder": folder, "name": name,
        "default_rom": best["filename"], "default_path": best["path"],
        "variant_count": len(files), "variants": variants,
    })

# ---- missing folder numbers ----
nums = sorted(int(f) for f in folders if f.isdigit())
maxnum = max(nums) if nums else 0
present = set(nums)
missing = [n for n in range(1, maxnum + 1) if n not in present]
ext_counter = Counter(r["extension"] for r in rows)

db = {
    "meta": {
        "source": "F:/ROM full rescan (ROM/0001..ROM/%04d)" % maxnum,
        "note": "Metadata only; ROM contents never read/modified/moved.",
        "total_games": len(games),
        "total_rom_files": len(rows),
        "multi_version_games": multi,
        "extensions": dict(ext_counter),
        "max_folder": "%04d" % maxnum,
        "missing_folder_count": len(missing),
    },
    "games": games,
}
json.dump(db, open(os.path.join(OUT, "assets", "game_database.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=2)

# ---- stats report ----
def fmt_ranges(ns):
    """collapse [1,2,3,7,8] -> ['0001-0003','0007-0008']"""
    out, i = [], 0
    while i < len(ns):
        j = i
        while j + 1 < len(ns) and ns[j + 1] == ns[j] + 1:
            j += 1
        out.append("%04d" % ns[i] if i == j else "%04d-%04d" % (ns[i], ns[j]))
        i = j + 1
    return out

rep = os.path.join(OUT, "rom_scan_report.md")
with open(rep, "w", encoding="utf-8") as f:
    f.write("# ROM 完整扫描报告 (ROM/0001 .. ROM/%04d)\n\n" % maxnum)
    f.write("> 仅读取元数据(文件名/路径/大小/扩展名)，未打开/修改/移动任何 ROM。\n\n")
    f.write("## 统计\n\n")
    f.write(f"- 游戏数量(编号目录): **{len(games)}**\n")
    f.write(f"- ROM 文件数量: **{len(rows)}**\n")
    f.write(f"- 多版本游戏: **{multi}**\n")
    f.write(f"- 最大目录编号: **%04d**\n" % maxnum)
    f.write(f"- 缺失编号数量: **{len(missing)}**\n\n")
    total_bytes = sum(r["size"] for r in rows)
    f.write(f"- 总大小: {total_bytes:,} bytes (~{total_bytes/1024/1024:.2f} MB)\n\n")
    f.write("### 扩展名统计\n\n| 扩展名 | 数量 |\n|---|---:|\n")
    for e, c in sorted(ext_counter.items(), key=lambda x: -x[1]):
        f.write(f"| .{e} | {c} |\n")
    f.write("\n### 缺失编号\n\n")
    if missing:
        f.write(f"共 {len(missing)} 个缺号（区间表示）:\n\n")
        f.write(", ".join(fmt_ranges(missing)) + "\n")
    else:
        f.write("无缺号，1..%04d 连续。\n" % maxnum)

print("GAMES=", len(games))
print("ROM_FILES=", len(rows))
print("MULTI=", multi)
print("EXT=", dict(ext_counter))
print("MAX_FOLDER=%04d" % maxnum)
print("MISSING_COUNT=", len(missing))
print("MISSING_SAMPLE=", fmt_ranges(missing)[:15])
