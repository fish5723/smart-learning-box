# -*- coding: utf-8 -*-
"""ROM inventory scanner. Reads ONLY metadata (name/path/size/ext). Never opens ROM contents."""
import os, csv, re
from collections import defaultdict, Counter

ROM_ROOT = r"F:\ROM"
OUT_DIR = r"e:\IOT_competition\smart-learning-box"
ALLOWED_EXT = {".nes", ".fds", ".bin"}

rows = []
for dirpath, dirnames, filenames in os.walk(ROM_ROOT):
    for fn in filenames:
        ext = os.path.splitext(fn)[1].lower()
        if ext not in ALLOWED_EXT:
            continue
        full = os.path.join(dirpath, fn)
        size = os.path.getsize(full)          # metadata only, no read()
        rel = os.path.relpath(full, os.path.dirname(ROM_ROOT))
        rows.append({"path": rel.replace("\\", "/"), "filename": fn,
                     "extension": ext.lstrip("."), "size": size})

rows.sort(key=lambda r: r["path"])

# --- write CSV ---
csv_path = os.path.join(OUT_DIR, "rom_inventory.csv")
with open(csv_path, "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f)
    w.writerow(["id", "path", "filename", "extension", "size"])
    for i, r in enumerate(rows, 1):
        w.writerow([i, r["path"], r["filename"], r["extension"], r["size"]])

# --- base-name grouping ---
def base_name(fn):
    n = os.path.splitext(fn)[0]
    n = re.sub(r"\s*[\(\[][^\)\]]*[\)\]]", "", n)  # strip (...) [...]
    return n.strip()

def tags(fn):
    return re.findall(r"[\(\[]([^\)\]]*)[\)\]]", os.path.splitext(fn)[0])

groups = defaultdict(list)
for r in rows:
    groups[base_name(r["filename"])].append(r)

ext_counter = Counter(r["extension"] for r in rows)
multi = {k: v for k, v in groups.items() if len(v) > 1}

# --- write report ---
rep = os.path.join(OUT_DIR, "rom_report.md")
with open(rep, "w", encoding="utf-8") as f:
    f.write("# ROM 扫描报告\n\n")
    f.write("> 仅读取元数据(文件名/路径/大小/扩展名)，未打开任何 ROM 内容。\n\n")
    f.write("## 1. 数量统计\n\n")
    f.write(f"- ROM 文件总数: **{len(rows)}**\n")
    f.write(f"- 游戏条目(去版本后基名): **{len(groups)}**\n")
    f.write(f"- ROM 子目录数: **{len(set(os.path.dirname(r['path']) for r in rows))}**\n\n")
    f.write("### 文件类型\n\n| 扩展名 | 数量 |\n|---|---|\n")
    for ext, c in sorted(ext_counter.items(), key=lambda x: -x[1]):
        f.write(f"| .{ext} | {c} |\n")
    total_bytes = sum(r["size"] for r in rows)
    f.write(f"\n- 总大小: {total_bytes:,} bytes (~{total_bytes/1024/1024:.2f} MB)\n\n")

    f.write("## 2. 多版本 / 重名游戏\n\n")
    f.write(f"共 **{len(multi)}** 个游戏存在多个版本文件。\n\n")
    for name in sorted(multi):
        items = multi[name]
        f.write(f"### {name}  ({len(items)} 个)\n\n")
        for it in items:
            tg = ", ".join(tags(it["filename"])) or "-"
            f.write(f"- `{it['filename']}`  — 版本标签: {tg}  ({it['size']:,} B)\n")
        f.write("\n")

    f.write("## 3. 缺少数据库匹配风险\n\n")
    risks = []
    for r in rows:
        fn = r["filename"]
        if re.search(r"\(Hack\)|\(Pirate\)|\(Unl\)|\(Proto\)|\(Beta\)|\(Bootleg\)", fn, re.I):
            risks.append((fn, "非官方版本(Hack/Proto/Unl/Beta)可能无标准数据库条目"))
        if not tags(fn):
            risks.append((fn, "无地区/版本标签，难以与 No-Intro/GoodNES 精确匹配"))
        if r["size"] == 0:
            risks.append((fn, "文件大小为 0，疑似损坏"))
    if risks:
        f.write(f"发现 **{len(risks)}** 项潜在匹配风险:\n\n")
        for fn, why in risks[:200]:
            f.write(f"- `{fn}` — {why}\n")
    else:
        f.write("未发现明显匹配风险。\n")

# console summary
print(f"TOTAL_FILES={len(rows)}")
print(f"GAMES={len(groups)}")
print("EXT:", dict(ext_counter))
print(f"MULTI_VERSION_GAMES={len(multi)}")
print(f"CSV={csv_path}")
print(f"REPORT={rep}")
