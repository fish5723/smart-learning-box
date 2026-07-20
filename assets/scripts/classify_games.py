# -*- coding: utf-8 -*-
"""Classify games in game_database.json by explicit title map. Never reads ROM contents.
Only sets type/tags/year; folder/default_rom/default_path/variants untouched. year stays empty (no guessing)."""
import json, os
from collections import Counter

DB = r"e:\IOT_competition\smart-learning-box\assets\game_database.json"

# type -> tag template (rule 3)
TAGS = {
    "动作冒险": ["动作冒险", "经典"],
    "射击":     ["射击", "经典"],
    "角色扮演": ["RPG", "经典"],
    "策略模拟": ["策略", "经典"],
    "体育":     ["体育项目", "经典"],
    "益智":     ["益智", "经典"],
    "棋牌桌游": ["棋牌", "经典"],
    "冒险文字": ["文字冒险", "经典"],
    "教育音乐": ["教育", "经典"],
    "特殊/其他": ["其他"],
}

CLASS = {
    # ---- 射击 ----
    "1942":"射击","Alpha Mission":"射击","Argus":"射击","Astro Robo Sasa":"射击","B-Wings":"射击",
    "Baltron":"射击","Battle City":"射击","Choplifter":"射击","Commando":"射击","Cosmo Genesis":"射击",
    "Exed Exes":"射击","Exerion":"射击","Field Combat":"射击","Formation Z":"射击","Front Line":"射击",
    "Galaga":"射击","Galaxian":"射击","Galg":"射击","Gall Force":"射击","Geimos":"射击",
    "Ginga Denshou - Galaxy Odyssey":"射击","Gradius":"射击","Gyrodine":"射击","Ikari Warriors":"射击",
    "King's Knight":"射击","Layla":"射击","Macross":"射击","Magmax":"射击","Mobile Suit Z Gundam - Hot Scramble":"射击",
    "Moero TwinBee":"射击","Raid on Bungeling Bay":"射击","Seicross":"射击","Sky Destroyer":"射击",
    "Sky Kid":"射击","Space Invaders":"射击","Sqoon":"射击","Star Force":"射击","Star Luster":"射击",
    "Star Soldier":"射击","Super Star Force":"射击","Super Xevious":"射击","Terra Cresta":"射击",
    "Thexder":"射击","Tiger-Heli":"射击","TwinBee":"射击","Volguard II":"射击","Warpman":"射击",
    "Xevious":"射击","Zanac":"射击","Duck Hunt":"射击","Hogan's Alley":"射击","Wild Gunman":"射击",
    # ---- 动作冒险 ----
    "Adventure Island":"动作冒险","Adventures of Dino Riki":"动作冒险","Aigina no Yogen":"动作冒险",
    "Akumajou Dracula":"动作冒险","Antarctic Adventure":"动作冒险","Atlantis no Nazo":"动作冒险",
    "Balloon Fight":"动作冒险","Bird Week":"动作冒险","Bomberman":"动作冒险","Buggy Popper":"动作冒险",
    "BurgerTime":"动作冒险","Chack'n Pop":"动作冒险","Challenger":"动作冒险","Chubby Cherub":"动作冒险",
    "Circus Charlie":"动作冒险","City Adventure Touch":"动作冒险","City Connection":"动作冒险",
    "Crazy Climber":"动作冒险","Deadly Towers":"动作冒险","Devil World":"动作冒险","Dig Dug":"动作冒险",
    "Dig Dug II":"动作冒险","Donkey Kong":"动作冒险","Donkey Kong 3":"动作冒险","Donkey Kong Jr.":"动作冒险",
    "Door Door":"动作冒险","Doraemon":"动作冒险","Dough Boy":"动作冒险","Dragon Buster":"动作冒险",
    "Electrician":"动作冒险","Elevator Action":"动作冒险","Flying Dragon":"动作冒险","Ganbare Goemon!":"动作冒险",
    "Ganso Saiyuuki":"动作冒险","Gegege no Kitarou":"动作冒险","Ghostbusters":"动作冒险","Ghosts 'n Goblins":"动作冒险",
    "Hana no Star Kaidou":"动作冒险","Hikari Shinwa":"动作冒险","Hokuto no Ken":"动作冒险",
    "Hottaman no Chitei Tanken":"动作冒险","Ice Climber":"动作冒险","Ikki":"动作冒险","Jajamaru no Daibouken":"动作冒险",
    "Juu Ouken no Nazo":"动作冒险","Kage no Densetsu":"动作冒险","Karateka":"动作冒险","King Kong 2":"动作冒险",
    "Kinnikuman - Muscle Tag Match":"动作冒险","Knight Lore":"动作冒险","Koneko Monogatari":"动作冒险",
    "Kung Fu":"动作冒险","Kung-Fu Heroes":"动作冒险","Labyrinth":"动作冒险","Mach Rider":"动作冒险",
    "Mappy":"动作冒险","Mappy-Land":"动作冒险","Marchen Veil":"动作冒险","Mario Bros.":"动作冒险",
    "Meikyuu Kumikyoku":"动作冒险","Metro-Cross":"动作冒险","Metroid":"动作冒险","Mickey Mouse":"动作冒险",
    "Mighty Bomb Jack":"动作冒险","Musashi no Ken":"动作冒险","Nagagutsu wo Haita Neko":"动作冒险",
    "Nazo no Murasamejou":"动作冒险","Ninja Hattori-kun":"动作冒险","Ninja Jajamaru-kun":"动作冒险",
    "Ninja-kun":"动作冒险","Nuts & Milk":"动作冒险","Onyanko Town":"动作冒险","Pac-Land":"动作冒险",
    "Pac-Man":"动作冒险","Penguin-kun Wars":"动作冒险","Pooyan":"动作冒险","Popeye":"动作冒险",
    "Route-16 Turbo":"动作冒险","Son Son":"动作冒险","Space Hunter":"动作冒险","Spelunker":"动作冒险",
    "Super Arabian":"动作冒险","Super Mario Bros.":"动作冒险","Super Mario Bros. 2J":"动作冒险",
    "Super Pitfall":"动作冒险","Tatakai no Banka":"动作冒险","Terra... ":"动作冒险",
    "The 3-D Battles of World Runner":"动作冒险","The Goonies":"动作冒险","The Goonies II":"动作冒险",
    "The Tower of Druaga":"动作冒险","The Wing of Madoola":"动作冒险","Toki no Tabibito":"动作冒险",
    "Transformers - Convoy no Nazo":"动作冒险","Ultraman - Kaijuu Teikoku no Gyakushuu":"动作冒险",
    "Urban Champion":"动作冒险","Urusei Yatsura":"动作冒险","Valkyrie no Bouken":"动作冒险","Warpman ":"动作冒险",
    "Wrecking Crew":"动作冒险","Yie Ar Kung-Fu":"动作冒险","Bird Week ":"动作冒险","Banana":"益智",
    "The Legend of Zelda":"动作冒险","Hi no Tori Hououhen":"动作冒险",
    # ---- 角色扮演 ----
    "Breeder":"角色扮演","Daiva":"角色扮演","Deep Dungeon - Madou Senki":"角色扮演",
    "Dragon Ball - Le Secret Du Dragon":"角色扮演","Dragon Quest II":"角色扮演","Dragon Warrior":"角色扮演",
    "Esper Dream":"角色扮演","Hydlide":"角色扮演","Seikima II":"角色扮演","Suishou no Dragon":"角色扮演",
    "Zelda II - The Adventure of Link":"角色扮演",
    # ---- 益智 ----
    "2-in-1":"益智","Arkanoid":"益智","Babel":"益智","Binary Land":"益智","Castle Excellent":"益智",
    "Championship Lode Runner":"益智","Clu Clu Land":"益智","Eggerland":"益智","Flappy":"益智",
    "Lode Runner":"益智","Lot Lot":"益智","Namida no Soukoban Special":"益智","Nazo no Kabe":"益智",
    "Pinball":"益智","Robot Block":"益智","Solomon no Kagi":"益智","Super Lode Runner":"益智",
    # ---- 体育 ----
    "10-Yard Fight":"体育","Athletic World":"体育","Baseball":"体育","Excitebike":"体育",
    "F-1 Race":"体育","Family Computer Golf - Japan Course":"体育","Golf":"体育","Hyper Olympic":"体育",
    "Hyper Sports":"体育","Lunar Pool":"体育","Pro Wrestling":"体育","R.B.I. Baseball":"体育",
    "Road Fighter":"体育","Soccer":"体育","Stadium Events":"体育","Tag Team Pro-Wrestling":"体育",
    "Tennis":"体育","The Black Bass":"体育","Volleyball":"体育","Zippy Race":"体育",
    # ---- 棋牌桌游 ----
    "4 Nin Uchi Mahjong":"棋牌桌游","Family Computer Othello":"棋牌桌游","Gomoku Narabe":"棋牌桌游",
    "Mahjong":"棋牌桌游","Naitou Kudan - Shougi Hiden":"棋牌桌游","Professional Mahjong Gokuu":"棋牌桌游",
    # ---- 冒险文字 ----
    "Dead Zone":"冒险文字","Kieta Princess":"冒险文字","Law of the West":"冒险文字",
    "Mississippi Satsujin Jiken":"冒险文字","Portopia Renzoku Satsujin Jiken":"冒险文字",
    "Sherlock Holmes - Hakushaku Reijou Yuukai Jiken":"冒险文字","Takeshi no Chousenjou":"冒险文字",
    "Toukaidou Gojuusan Tsugi":"冒险文字",
    # ---- 教育音乐 ----
    "Dance Aerobics":"教育音乐","Donkey Kong Jr. Math":"教育音乐","Family Basic":"教育音乐",
    "I am a Teacher - Super Mario no Sweater":"教育音乐","I am a Teacher - Teami no Kiso":"教育音乐",
    "Ikinari Musician":"教育音乐","NS-Hu Basic":"教育音乐","Popeye no Eigo Asobi":"教育音乐",
    "Sansuu 1 Nen":"教育音乐","Sansuu 2 Nen":"教育音乐","Sansuu 3 Nen":"教育音乐","Sansuu 4 Nen":"教育音乐",
    "Sansuu 5 & 6 Nen":"教育音乐",
    # ---- 策略模拟 ----
    "Bokosuka Wars":"策略模拟","Pachicom":"策略模拟","Spy vs Spy":"策略模拟",
    # ---- 特殊/其他 ----
    "Kineko Vol. I":"特殊/其他","Nazoler Land Soukan Gou":"特殊/其他","Robot Gyro":"特殊/其他",
}

db = json.load(open(DB, encoding="utf-8"))
tc = Counter(); missing = 0

for g in db["games"]:
    t = CLASS.get(g["name"], "特殊/其他")
    if g["name"] not in CLASS:
        missing += 1
    g["type"] = t
    g["tags"] = list(TAGS[t])
    g["year"] = ""            # rule 4: never guess
    tc[t] += 1

multi = sum(1 for g in db["games"] if g["variant_count"] > 1)
db["meta"]["categories"] = dict(tc)
db["meta"]["category_count"] = len([k for k in tc if k != "特殊/其他"]) + (1 if "特殊/其他" in tc else 0)
db["meta"]["unclassified_fallback"] = tc.get("特殊/其他", 0)
db["meta"]["names_not_in_map"] = missing
db["meta"]["multi_version_games"] = multi

json.dump(db, open(DB, "w", encoding="utf-8"), ensure_ascii=False, indent=2)

print("=== 各类型数量 ===")
for k, v in tc.most_common():
    print(f"{k}: {v}")
print("总类型数:", len(tc))
print("特殊/其他(含未匹配):", tc.get("特殊/其他", 0))
print("未在映射表命中:", missing)
print("多版本游戏:", multi)
print("游戏总数:", len(db["games"]))
