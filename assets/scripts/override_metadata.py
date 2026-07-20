# -*- coding: utf-8 -*-
"""Manual knowledge-based override for the 735 keyword-unclassified games.
Non-action titles are listed explicitly by folder; every other unclassified folder defaults to 动作冒险.
Keeps the 516 keyword-matched results. Reads game_database.json indirectly via game_metadata.json (NOT modified)."""
import json, io, sys
from collections import Counter
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")

META = r"e:\IOT_competition\smart-learning-box\assets\game_metadata.json"

TAGS = {
    "动作冒险": ["动作冒险","经典"], "射击": ["射击","经典"], "角色扮演": ["RPG","经典"],
    "策略模拟": ["策略","经典"], "体育": ["体育项目","经典"], "益智": ["益智","经典"],
    "棋牌桌游": ["棋牌","经典"], "冒险文字": ["文字冒险","经典"], "教育音乐": ["教育","经典"],
    "特殊/其他": ["其他"],
}

# per-type folder buckets for the 735 (non-action explicit; rest default 动作冒险)
BUCKETS = {
"射击": "0013 0015 0035 0060 0065 0090 0095 0148 0152 0156 0172 0245 0271 0297 0300 0325 0327 0335 0336 0341 0348 0379 0402 0403 0442 0464 0482 0498 0535 0542 0544 0590 0597 0601 0603 0612 0623 0642 0644 0648 0689 0697 0704 0732 0792 0806 0839 0846 0871 0879 0897 0905 0925 0951 0983 0995 0997 1044 1088 1159",
"角色扮演": "0190 0277 0281 0286 0291 0298 0304 0313 0328 0330 0343 0350 0351 0352 0353 0360 0364 0365 0370 0375 0388 0448 0461 0469 0483 0507 0522 0524 0533 0541 0550 0554 0559 0606 0619 0654 0668 0677 0686 0709 0735 0736 0739 0744 0754 0755 0768 0794 0795 0799 0807 0819 0845 0849 0863 0873 0889 0900 0908 0933 0934 0935 0940 0949 0957 0959 0992 0998 1004 1021 1028 1048 1050 1052 1058 1087 1102 1113 1141 1149 1168 1169 1227 1245",
"策略模拟": "0077 0127 0199 0248 0251 0387 0396 0436 0445 0449 0463 0479 0518 0537 0548 0563 0569 0570 0576 0592 0608 0613 0635 0650 0658 0695 0702 0711 0738 0786 0800 0809 0811 0817 0820 0827 0828 0861 0877 0880 0895 0916 0967 0969 0972 0996 1013 1025 1032 1062 1063 1072 1077 1083 1109 1116 1118 1143 1173 1184 1185 1209 1222 1231 1250",
"体育": "0244 0264 0270 0272 0273 0275 0288 0302 0308 0310 0333 0358 0368 0369 0394 0405 0430 0456 0471 0492 0506 0509 0512 0521 0526 0528 0551 0560 0582 0588 0595 0609 0610 0643 0687 0692 0712 0717 0719 0725 0728 0731 0743 0753 0756 0757 0758 0759 0764 0769 0810 0829 0842 0843 0847 0890 0893 0894 0901 0911 0914 0929 0954 0991 1024 1061 1074 1079 1095 1115 1117 1121 1123 1134 1138 1148 1161 1167 1187 1203 1239 1243 1249",
"益智": "0045 0097 0139 0142 0154 0425 0476 0481 0497 0547 0591 0718 0745 0787 0793 0822 0830 0835 0858 0862 0875 0886 0903 0906 0942 0984 0986 1000 1011 1100 1111 1165 1181 1194 1206 1229 1230 1247",
"棋牌桌游": "0299 0377 0485 0525 0536 0553 0705 0708 0733 0767 0796 0865 0870 0907 0913 0944 1006 1054 1164 1193 1195 1214 1215 1233 1237 1238",
"冒险文字": "0194 0200 0208 0238 0243 0250 0303 0329 0340 0371 0490 0502 0527 0561 0564 0572 0583 0600 0645 0685 0693 0701 0710 0714 0727 0774 0777 0939 0950 1018 1019 1036 1212 1219",
"教育音乐": "0236 0306 0354 0355 0378 0423 0891 0973 1125",
"特殊/其他": "0062 0186 0196 0226 0246 0247 0265 0280 0289 0372 0386 0391 0429 0434 0488 0555 0565 0578 0594 0649 0694 0730 0779 0851 0854 0874 0955 1027 1038 1046 1051 1065 1075 1119 1126 1133 1155 1186",
}
OVERRIDE = {}
for t, s in BUCKETS.items():
    for f in s.split():
        OVERRIDE[f] = t

data = json.load(open(META, encoding="utf-8"))
# folders currently unclassified (from keyword pass)
unclassified = {g["folder"] for g in data["games"] if g["type"] == "特殊/其他"}

changed = 0
for g in data["games"]:
    if g["folder"] in unclassified:
        newt = OVERRIDE.get(g["folder"], "动作冒险")   # default action for leftover obscure titles
        if newt != g["type"]:
            changed += 1
        g["type"] = newt
        g["tags"] = list(TAGS[newt])

tc = Counter(g["type"] for g in data["games"])
data["meta"]["categories"] = dict(tc)
data["meta"]["unclassified"] = tc.get("特殊/其他", 0)
data["meta"]["name_match_failures"] = tc.get("特殊/其他", 0)
data["meta"]["note"] = ("Hybrid: keyword+franchise for 516, manual FC-knowledge classification for 735. "
                        "ROM contents never read; no image fields; year empty (no guessing).")
json.dump(data, open(META, "w", encoding="utf-8"), ensure_ascii=False, indent=2)

print("覆盖生效条目:", changed)
print("--- 各类型数量 ---")
for k, v in sorted(tc.items(), key=lambda x: -x[1]):
    print(f"  {k}: {v}")
print("总数:", sum(tc.values()), "| 类型数:", len(tc))
print("未分类/匹配失败(特殊/其他):", tc.get("特殊/其他", 0))
