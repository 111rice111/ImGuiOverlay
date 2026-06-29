#!/usr/bin/env python3
"""
AI 自动提取地图可走路径
输入: 地图纹理图 (jpg/png)
输出: player_paths JSON
"""

import cv2
import numpy as np
import json
import sys
from pathlib import Path

def extract_walkable_paths(image_path, map_name="地图", sample_interval=20):
    """
    从地图纹理图提取可走路径
    sample_interval: 采样间隔(像素)，越小路径点越多越精细
    """
    img = cv2.imread(str(image_path))
    if img is None:
        print(f"Error: 无法读取图片 {image_path}")
        return None
    
    h, w = img.shape[:2]
    print(f"图片尺寸: {w}x{h}")
    
    # 转换到 HSV 颜色空间，更容易分离颜色
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    
    # 提取可走区域 (棕色房间 + 灰色走廊)
    # 棕色地板: HSV 范围
    lower_brown = np.array([10, 40, 40])
    upper_brown = np.array([30, 180, 180])
    mask_brown = cv2.inRange(hsv, lower_brown, upper_brown)
    
    # 灰色走廊: HSV 范围
    lower_gray = np.array([0, 0, 60])
    upper_gray = np.array([180, 50, 150])
    mask_gray = cv2.inRange(hsv, lower_gray, upper_gray)
    
    # 合并可走区域
    walkable_mask = cv2.bitwise_or(mask_brown, mask_gray)
    
    # 形态学操作：去噪 + 连接断裂
    kernel = np.ones((5,5), np.uint8)
    walkable_mask = cv2.morphologyEx(walkable_mask, cv2.MORPH_CLOSE, kernel)
    walkable_mask = cv2.morphologyEx(walkable_mask, cv2.MORPH_OPEN, kernel)
    
    # 提取骨架 (中心线)
    skeleton = cv2.ximgproc.thinning(walkable_mask)
    
    # 查找连通域
    num_labels, labels, stats, centroids = cv2.connectedComponentsWithStats(skeleton, connectivity=8)
    
    paths = []
    for i in range(1, num_labels):  # 跳过背景(0)
        area = stats[i, cv2.CC_STAT_AREA]
        if area < 50:  # 过滤小噪点
            continue
        
        # 提取该连通域的所有点
        y_coords, x_coords = np.where(labels == i)
        points = list(zip(x_coords.tolist(), y_coords.tolist()))
        
        if len(points) < 3:
            continue
        
        # 按顺序采样 (简化：按 x 坐标排序后采样)
        points.sort(key=lambda p: (p[1], p[0]))  # 先按 y，再按 x
        
        sampled = []
        for idx, (px, py) in enumerate(points):
            if idx % sample_interval == 0 or idx == len(points) - 1:
                # 像素坐标 → 归一化 UV (0-1)
                u = px / w
                v = py / h
                sampled.append({"u": round(u, 4), "v": round(v, 4)})
        
        if len(sampled) >= 2:
            paths.append({"points": sampled})
    
    print(f"提取到 {len(paths)} 条路径，共 {sum(len(p['points']) for p in paths)} 个点")
    
    # 可视化保存
    vis = img.copy()
    # 标记可走区域
    vis[walkable_mask > 0] = [0, 255, 0]  # 绿色
    # 标记骨架
    vis[skeleton > 0] = [0, 0, 255]  # 红色
    
    output_vis = Path(image_path).stem + "_paths_vis.jpg"
    cv2.imwrite(output_vis, vis)
    print(f"可视化结果已保存: {output_vis}")
    
    return {
        "name": map_name,
        "floor": 0,
        "player_paths": paths
    }

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("用法: python extract_paths.py <地图图片路径> [采样间隔]")
        sys.exit(1)
    
    image_path = sys.argv[1]
    interval = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    
    result = extract_walkable_paths(image_path, sample_interval=interval)
    
    if result:
        output_json = Path(image_path).stem + "_paths.json"
        with open(output_json, 'w', encoding='utf-8') as f:
            json.dump(result, f, ensure_ascii=False, indent=2)
        print(f"路径数据已保存: {output_json}")
