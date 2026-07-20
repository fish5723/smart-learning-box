# -*- coding: utf-8 -*-
"""Generate game_metadata.json for all 1251 games via keyword rules + explicit franchise map.
Reads game_database.json (NOT modified). Never opens ROMs. No image fields. year left empty (no guessing)."""
import json, os, re
from collections import Counter

DB  = r"e:\IOT_competition\smart-learning-box\assets\game_database.json"
OUT = r"e:\IOT_competition\smart-learning-box\assets\game_metadata.json"

TAGS = {
    "动作冒险": ["动作冒险","经典"], "射击": ["射击","经典"], "角色扮演": ["RPG","经典"],
    "策略模拟": ["策略","经典"], "体育": ["体育项目","经典"], "益智": ["益智","经典"],
    "棋牌桌游": ["棋牌","经典"], "冒险文字": ["文字冒险","经典"], "教育音乐": ["教育","经典"],
    "特殊/其他": ["其他"],
}

# --- high-confidence explicit franchise/title map (substring, case-insensitive) ---
EXPLICIT = [
    # 角色扮演
    (r"dragon quest|dragon warrior|final fantasy|wizardry|ultima|hydlide|megami tensei|deep dungeon|"
     r"esper dream|dragon slayer|sword of|mother\b|glory of heracles|herakles|ganbare goemon gaiden|"
     r"famicom jump|miracle ropit|dungeon|maou golvellius|faria|willow|magic of scheherazade|"
     r"destiny of an emperor|tenchi wo kurau|jesus|cocoron|dragon buster|valkyrie", "角色扮演"),
    # 棋牌桌游
    (r"mahjong|maajan|jangou|\bjan\b|shougi|shogi|othello|reversi|gomoku|renju|\bigo\b|\bgo\b|"
     r"hanafuda|trump|poker|casino|\bchess\b|\bcard\b|\bkcard\b|\bnin uchi\b|honshougi|"
     r"tsume shougi|gunjin shougi|billiard|bishoujo.*mahjong", "棋牌桌游"),
    # 冒险文字 (detective / mystery / visual-novel adventure)
    (r"satsujin jiken|renzoku satsujin|\bjiken\b|\btantei\b|detective|holmes|portopia|yuukai|"
     r"murder|jinguuji|misa no|yakusoku|ripple island|adventure of|sherlock|"
     r"ie naki ko|hokkaidou rensa|toukaidou", "冒险文字"),
    # 教育音乐
    (r"sansuu|\beigo\b|\bmath\b|teacher|family basic|ns-hu basic|aerobics|karaoke|"
     r"\bmusic\b|piano|\d ?nen\b|gakushuu|benkyou|kanji|study|donkey kong jr\. math|"
     r"popeye no eigo|\babc\b|typing", "教育音乐"),
    # 体育
    (r"baseball|\bsoccer\b|football|\bgolf\b|tennis|volleyball|basketball|olympic|athletic|"
     r"\bsports?\b|wrestling|boxing|sumo|fishing|\btsuri\b|black bass|\bpool\b|bowling|"
     r"\bhockey\b|\brace\b|racing|rally|circuit|grand prix|\bf-1\b|\bf1\b|\bgp\b|derby|"
     r"skate|\bski\b|swim|\bcup\b|\bleague\b|stadium|10-yard|hyper olympic|hyper sports|"
     r"exciting|nekketsu koushien|moero.*yakyuu|dodge|kunio.*undoukai|paris-dakar|"
     r"formula|world cup|super sky kid|excitebike|mach rider|zippy race|road fighter|"
     r"hole in one|top rider|famicom grand prix|rc pro", "体育"),
    # 射击
    (r"gradius|xevious|twinbee|salamander|zanac|star soldier|star force|star luster|galaga|"
     r"galaxian|gun\.?smoke|\bgun\b|gunman|commando|ikari|b-wing|contra|1942|1943|tiger-heli|"
     r"raiden|macross|thunder|missile|space (invaders|hunter|shadow)|guardian legend|"
     r"section z|life force|gyruss|zaxxon|terra cresta|argus|magmax|volguard|sqoon|"
     r"gyrodine|exed exes|exerion|field combat|front line|formation z|sky destroyer|"
     r"heavy barrel|jackal|silkworm|air fortress|zombie hunter|star ship|gall force|"
     r"cosmo|geimos|baltron|seicross|super star force|super xevious|choplifter|"
     r"aleste|recca|crisis force|over horizon|gun nac|gun-nac|hectic|abadox|"
     r"dead zone|gundam.*scramble", "射击"),
    # 益智
    (r"puzzle|tetris|lode runner|arkanoid|sokoban|soukoban|pinball|columns|puyo|eggerland|"
     r"flipull|clu clu|pipe|quarth|palamedes|adventures of lolo|money game|"
     r"block\b|dr\. mario|yoshi|klax|solitaire|nazo no kabe|binary land|"
     r"picture puzzle|hatris|magic johnson", "益智"),
    # 策略模拟
    (r"nobunaga|sangokushi|romance of the three|daisenryaku|\bwars\b|simulation|"
     r"\bsim\b|tactics|strategy|aerobiz|nekketsu.*monogatari(?!)|bokosuka|"
     r"gunshi|shushou|keiei|derby stallion|best play|first queen|"
     r"military|europe sensen|famicom senki|sd gundam.*gachapon", "策略模拟"),
    # 教育-ish already; 动作冒险 strong franchises
    (r"super mario|mario bros|zelda|akumajou|dracula|castlevania|\bninja\b|kung.?fu|"
     r"ghosts.?n.?goblins|makaimura|metroid|rockman|mega man|kunio|goemon|adventure island|"
     r"takahashi meijin|bomberman|ganbare goemon|kamen rider|ultraman|kinnikuman|"
     r"tetsuwan atom|doraemon|dragon ball|saint seiya|hokuto no ken|"
     r"donald|mickey|disney|wai wai world|superman|predator|batman|"
     r"ninja gaiden|shinobi|ganso|jajamaru|karateka|urban champion|"
     r"spartan|rush.?n.?attack|rygar|kage|magmax|"
     r"chack.?n.?pop|dig dug|mappy|pac.?man|pac.?land|pooyan|son son|"
     r"binary(?!)|circus charlie|city connection|elevator action|"
     r"ice climber|balloon fight|wrecking crew|donkey kong|popeye|"
     r"spelunker|pitfall|druaga|castlequest|castle excellent|"
     r"gremlins|goonies|ghostbuster|transformers|gundam.*hot scramble", "动作冒险"),
]
EXPLICIT = [(re.compile(p, re.I), t) for p, t in EXPLICIT]

# names that are genuinely misc / non-game / unknown
SPECIAL = re.compile(r"nazoler land|kineko|karaoke studio|family trainer|"
                     r"\b2-in-1\b|\bmulti\b|robot gyro|test cart|bikkuri|"
                     r"terebi|tv system|keyboard|datach|barcode|"
                     r"family computer othello", re.I)

def classify(name):
    if SPECIAL.search(name):
        # othello is board though
        if re.search(r"othello", name, re.I):
            return "棋牌桌游", False
        return "特殊/其他", False
    for rx, t in EXPLICIT:
        if rx.search(name):
            return t, False
    return "特殊/其他", True  # match failure

db = json.load(open(DB, encoding="utf-8"))
out, tc = [], Counter()
match_fail = 0
for g in db["games"]:
    t, failed = classify(g["name"])
    if failed:
        match_fail += 1
    tc[t] += 1
    out.append({
        "id": g["id"], "folder": g["folder"], "name": g["name"],
        "type": t, "year": "", "tags": list(TAGS[t]),
    })

meta = {
    "meta": {
        "source": "game_database.json (unmodified)",
        "note": "Keyword+franchise heuristic classification; ROM contents never read; no image fields; year empty (no guessing).",
        "total_games": len(out),
        "categories": dict(tc),
        "unclassified": tc.get("特殊/其他", 0),
        "name_match_failures": match_fail,
    },
    "games": out,
}
json.dump(meta, open(OUT, "w", encoding="utf-8"), ensure_ascii=False, indent=2)

import io, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8")
print("总数:", len(out))
print("--- 各类型数量 ---")
for k, v in sorted(tc.items(), key=lambda x: -x[1]):
    print(f"  {k}: {v}")
print("未分类(特殊/其他):", tc.get("特殊/其他", 0))
print("名称匹配失败:", match_fail)
