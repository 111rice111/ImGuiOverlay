# build_fingerprints.py — 从日志自动生成增强指纹数据库
# 用法: python build_fingerprints.py
# 输出: enhanced_fingerprints.json (供 C++ LoadFingerprintDB 加载)
#       fingerprint_summary.txt  (人类可读)

import os, json, math
from collections import defaultdict

LOGDIR = 'E:/ImGuiOverlay/log_analysis/log'
OUTJSON = 'E:/ImGuiOverlay/log_analysis/enhanced_fingerprints.json'
OUTTXT = 'E:/ImGuiOverlay/log_analysis/fingerprint_summary.txt'

# 物体匹配模式
PATTERNS = {
    'piano': 'piano01.gim',
    'pianochair': 'pianochair',
    'musicbox': 'musicbox',
    'core_door': 'core_l_01_door',
    'outdoor_door': 'outdoor_door',
    'prop_door': 'prop_door04',
    'woodplane': 'woodplane01',
}

def parse_log(filepath, logid):
    """解析单个日志，返回 {counts, piano_xyz, musicbox_xyz, chair_coords}"""
    counts = {k: 0 for k in PATTERNS}
    piano_xyz = None
    musicbox_xyz = None
    chair_coords = []

    with open(filepath, errors='ignore') as f:
        for line in f:
            for key, pat in PATTERNS.items():
                if pat in line:
                    counts[key] += 1
                    parts = line.strip().split(',')
                    if len(parts) >= 3:
                        try:
                            x = float(parts[-3])
                            y = float(parts[-2])
                            z = float(parts[-1])
                            if key == 'piano' and piano_xyz is None:
                                piano_xyz = (round(x), round(y), round(z))
                            elif key == 'musicbox' and musicbox_xyz is None:
                                musicbox_xyz = (round(x), round(y), round(z))
                            elif key == 'pianochair':
                                chair_coords.append((round(x), round(y), round(z)))
                        except ValueError:
                            pass
    return {
        'logid': logid,
        'counts': counts,
        'piano_xyz': piano_xyz,
        'musicbox_xyz': musicbox_xyz,
        'chair_coords': chair_coords,
    }

# ── 解析所有日志 ──
print("解析日志...")
all_logs = []
for fname in os.listdir(LOGDIR):
    if not fname.endswith('.log'):
        continue
    logid = fname.replace('.log', '')
    data = parse_log(os.path.join(LOGDIR, fname), logid)
    all_logs.append(data)
print(f"  共 {len(all_logs)} 个日志")

# ── 去重：按钢琴坐标(有钢琴)或音乐盒坐标(无钢琴)分组 ──
print("去重分组...")
groups = defaultdict(list)
for log in all_logs:
    c = log['counts']
    if log['piano_xyz']:
        key = ('piano', log['piano_xyz'])
    elif log['musicbox_xyz']:
        key = ('musicbox', log['musicbox_xyz'])
    else:
        # 无钢琴无音乐盒: 用椅子和门的组合做key
        key = ('chair_door', (c['pianochair'], c['core_door'], c['outdoor_door'], c['prop_door'], c['woodplane']))
    groups[key].append(log)

# ── 合并每组：取最多物体计数的日志作为代表 ──
print(f"  合并为 {len(groups)} 张独立地图\n")

map_fingerprints = []
unique_id = 0
summary_lines = []
all_piano_coords_seen = set()  # 用于检测钢琴重复

for grp_key, logs in groups.items():
    unique_id += 1
    # 选择物体最多的日志作为代表（扫描最完整）
    def log_weight(log):
        c = log['counts']
        return c['pianochair'] * 10 + c['prop_door'] + c['core_door'] * 5 + c['woodplane'] * 5

    best = max(logs, key=log_weight)
    c = best['counts']

    # 判断是否是重复钢琴（同一张图多次扫描）
    is_dup_piano = False
    if best['piano_xyz']:
        px, py, pz = best['piano_xyz']
        piano_round = (px, py, pz)
        is_dup_piano = piano_round in all_piano_coords_seen
        all_piano_coords_seen.add(piano_round)

    fp = {
        'id': unique_id,
        'piano_x': best['piano_xyz'][0] if best['piano_xyz'] else 0.0,
        'piano_y': best['piano_xyz'][1] if best['piano_xyz'] else 0.0,
        'piano_z': best['piano_xyz'][2] if best['piano_xyz'] else 0.0,
        'musicbox_x': best['musicbox_xyz'][0] if best['musicbox_xyz'] else 0.0,
        'musicbox_y': best['musicbox_xyz'][1] if best['musicbox_xyz'] else 0.0,
        'musicbox_z': best['musicbox_xyz'][2] if best['musicbox_xyz'] else 0.0,
        'chair_count': c['pianochair'],
        'core_door_count': c['core_door'],
        'outdoor_door_count': c['outdoor_door'],
        'prop_door_count': c['prop_door'],
        'woodplane_count': c['woodplane'],
        'chairs': [{'x': x, 'y': y, 'z': z} for x, y, z in best['chair_coords']],
        'source_logs': [l['logid'] for l in logs],
        'is_dup': is_dup_piano,
    }
    map_fingerprints.append(fp)

    status = '  重复!' if is_dup_piano else ''
    line = (f"  地图{unique_id}: P=({fp['piano_x']:.0f},{fp['piano_y']:.0f},{fp['piano_z']:.0f}) "
            f"M=({fp['musicbox_x']:.0f},{fp['musicbox_y']:.0f},{fp['musicbox_z']:.0f}) "
            f"C={c['pianochair']} coreD={c['core_door']} outD={c['outdoor_door']} "
            f"propD={c['prop_door']} W={c['woodplane']} "
            f"logs={fp['source_logs']}{status}")
    summary_lines.append(line)
    print(line)

# ── 生成 JSON ──
output = {
    'version': '2.0',
    'total_fingerprints': len(map_fingerprints),
    'description': '从日志自动生成的地图指纹数据库 — 用于杂交匹配识别',
    'fingerprints': map_fingerprints,
}
with open(OUTJSON, 'w', encoding='utf-8') as f:
    json.dump(output, f, indent=2, ensure_ascii=False)
print(f'\n已生成: {OUTJSON}')

# ── 生成摘要 ──
with open(OUTTXT, 'w', encoding='utf-8') as f:
    f.write(f'增强指纹数据库摘要\n')
    f.write(f'日志总数: {len(all_logs)} | 去重后地图: {len(map_fingerprints)}\n')
    f.write(f'{"="*70}\n')
    for line in summary_lines:
        f.write(line + '\n')
    # 对比分析
    f.write(f'\n{"="*70}\n')
    f.write('独特性分析:\n')
    for fp in map_fingerprints:
        c = fp['chair_count']
        p = fp['prop_door_count']
        core = fp['core_door_count']
        w = fp['woodplane_count']
        # 找相同指纹的其他地图
        same = [x for x in map_fingerprints if x['id'] != fp['id']
                and x['chair_count'] == c and x['prop_door_count'] == p
                and x['core_door_count'] == core and x['woodplane_count'] == w]
        marker = '✅唯一' if not same else f'⚠️与地图{",".join(str(s["id"]) for s in same)}冲突'
        f.write(f"  地图{fp['id']}: C={c} pD={p} cD={core} W={w} → {marker}\n")
print(f'已生成: {OUTTXT}')

# ── 统计 ──
piano_maps = sum(1 for fp in map_fingerprints if fp['piano_x'] != 0)
unique_combos = set()
conflicts = 0
for fp in map_fingerprints:
    combo = (fp['chair_count'], fp['prop_door_count'], fp['core_door_count'], fp['woodplane_count'])
    if combo in unique_combos:
        conflicts += 1
    unique_combos.add(combo)

print(f'\n统计: {len(map_fingerprints)}地图, {piano_maps}有钢琴')
print(f'  指纹冲突: {conflicts}/{len(map_fingerprints)} (越低越好)')
