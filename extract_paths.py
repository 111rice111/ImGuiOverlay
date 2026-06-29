"""
extract_paths.py — 从游戏攻略图中提取红蓝路径像素坐标
=============================================================

功能:
  读取地图攻略图 → 移除宝箱图标(鹅黄) → 提取红/蓝路径 →
  细化骨架 → 生成有序坐标 → 输出验证图 + 坐标文件

已知颜色:
  鹅黄(宝箱):  RGB(250, 224, 77)   #FAE04D
  酡红(主路):  RGB(226,  54, 42)   #E2362A
  蔚蓝(辅路):  RGB(124, 251, 252)  #7CFBFC

用法:
  python extract_paths.py <图片文件夹>          → 完整输出(坐标+验证图)
  python extract_paths.py <图片文件夹> --preview-only → 仅生成验证图

依赖:
  pip install opencv-contrib-python numpy
"""

import cv2
import numpy as np
import os
import sys
import argparse
from collections import deque

# ═══════════════════════════════════════════════════════════
#  配置常量
# ═══════════════════════════════════════════════════════════

# ---- 目标颜色 (RGB) ----
CHEST_RGB  = np.array([250, 224,  77], dtype=np.uint8)  # 鹅黄 — 宝箱
MAIN_RGB   = np.array([226,  54,  42], dtype=np.uint8)  # 酡红 — 主路径
SUB_RGB    = np.array([124, 251, 252], dtype=np.uint8)  # 蔚蓝 — 辅路

# ---- 颜色容差 (每个通道 ±N 以内视为匹配) ----
RGB_TOLERANCE = 18

# ---- 形态学参数 ----
# 闭运算核大小: 小核连接线段断裂, 但必须是够小才能保留墙壁缺口
# 地图中墙壁缺口通常 ≥5px, 所以 3x3 的核只会填补线内细缝
CLOSE_KERNEL_SIZE = (3, 3)

# ---- 验证图参数 ----
VERIFY_LINE_WIDTH = 3  # 验证图上路径线条宽度(px)
VERIFY_ALPHA = 0.55    # 路径叠加透明度 (0=完全透明, 1=完全不透明)


# ═══════════════════════════════════════════════════════════
#  核心函数
# ═══════════════════════════════════════════════════════════

def mask_by_rgb_tolerance(img_bgr, target_rgb, tolerance):
    """
    通过 RGB 容差生成二值掩膜。

    对每个像素计算与目标颜色的欧氏距离。距离 ≤ tolerance 的视为匹配。
    相比逐通道比较, 欧氏距离更自然 —— 它同时考虑三通道偏差,
    避免了 "R 差很多但 GB 完美" 的误匹配。

    参数:
        img_bgr:   BGR 图像 (OpenCV 默认格式)
        target_rgb: 目标颜色 (R, G, B)
        tolerance:  容差半径

    返回:
        二值掩膜 (uint8, 0/255)
    """
    # OpenCV 图像是 BGR 顺序, 目标颜色数组需对应
    target_bgr = target_rgb[::-1]  # (R,G,B) → (B,G,R)
    # 计算每个像素与目标颜色的欧氏距离
    dist = np.linalg.norm(img_bgr.astype(np.float32) - target_bgr.astype(np.float32), axis=2)
    mask = (dist <= tolerance).astype(np.uint8) * 255
    return mask


def remove_chest_icons(img_bgr, mask_chest):
    """
    将鹅黄色宝箱区域替换为背景深灰色 (RGB≈60,60,60)。

    深灰色与地图墙壁颜色接近, 替换后不留视觉痕迹,
    避免宝箱图标干扰后续路径提取。

    参数:
        img_bgr:     原始 BGR 图像
        mask_chest:  宝箱区域二值掩膜

    返回:
        清理后的 BGR 图像 (不修改原图)
    """
    result = img_bgr.copy()
    # 深灰色 BGR = (60, 60, 60)
    result[mask_chest > 0] = (60, 60, 60)
    return result


def extract_line_mask(img_bgr, target_rgb, tolerance):
    """
    从图像中提取指定颜色线条的完整流程:
      1. RGB 容差掩膜
      2. 形态学闭运算 — 连接细小断裂
         (核大小 3x3: 足够填补线段内断裂, 但墙壁缺口 ≥5px 不会被填)
      3. 骨架细化 — 将线条减至单像素宽

    参数:
        img_bgr:     BGR 图像
        target_rgb:  目标 RGB 颜色
        tolerance:   容差值

    返回:
        (mask_raw, mask_thinned) — 原始掩膜 + 细化后的骨架掩膜
    """
    # Step 1: RGB 容差掩膜
    mask = mask_by_rgb_tolerance(img_bgr, target_rgb, tolerance)

    # Step 2: 形态学闭运算 (先膨胀后腐蚀, 填充线段内小孔/裂缝)
    #         使用椭圆形核以获得更自然的连接效果
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, CLOSE_KERNEL_SIZE)
    mask_closed = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel)

    # Step 3: 骨架细化 → 单像素宽线条
    mask_thinned = cv2.ximgproc.thinning(mask_closed, thinningType=cv2.ximgproc.THINNING_ZHANGSUEN)

    return mask, mask_thinned


def find_endpoints(skeleton):
    """
    查找骨架中的所有端点。

    在 8 邻域中, 端点只有 1 个邻居 (骨架像素)。
    端点像素用作 DFS 追踪的起点, 确保生成有序坐标链。

    参数:
        skeleton: 二值骨架掩膜 (0/255)

    返回:
        端点坐标列表 [(y, x), ...]
    """
    h, w = skeleton.shape
    endpoints = []
    # 8 邻域偏移
    neighbors = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)]

    for y in range(1, h-1):
        for x in range(1, w-1):
            if skeleton[y, x] == 0:
                continue
            # 统计 8 邻域中骨架像素数
            count = sum(1 for dy, dx in neighbors if skeleton[y+dy, x+dx] > 0)
            if count == 1:  # 仅 1 个邻居 → 端点
                endpoints.append((y, x))

    return endpoints


def dfs_trace(skeleton, start):
    """
    从端点出发, 深度优先遍历骨架, 生成有序像素坐标链。

    遍历到分叉点时选择未访问的邻居, 到尽端(0 或 1 个未访问邻居)时停止。
    返回的坐标按遍历顺序排列, 可用于路径绘制。

    参数:
        skeleton: 二值骨架掩膜 (该函数会修改副本)
        start:    起点坐标 (y, x)

    返回:
        有序坐标列表 [(x, y), ...]  — x 在前符合用户要求的 x,y 格式
    """
    h, w = skeleton.shape
    # 8 邻域及其逆方向索引 (用于回退时发现分叉)
    dirs = [(-1,-1),(-1,0),(-1,1),(0,-1),(0,1),(1,-1),(1,0),(1,1)]

    path = []
    stack = [(start[0], start[1])]
    visited = np.zeros_like(skeleton, dtype=bool)

    while stack:
        y, x = stack[-1]  # 查看栈顶
        if visited[y, x]:
            stack.pop()
            continue
        visited[y, x] = True
        path.append((x, y))  # 用户要求 x,y 格式 (列在前)

        # 收集所有未访问的骨架邻居
        neighbors = []
        for dy, dx in dirs:
            ny, nx = y + dy, x + dx
            if 0 <= ny < h and 0 <= nx < w and skeleton[ny, nx] > 0 and not visited[ny, nx]:
                neighbors.append((ny, nx))

        if len(neighbors) == 1:
            stack.append(neighbors[0])
        elif len(neighbors) > 1:
            # 分叉: 取第一个继续, 其余的后续处理
            for nb in neighbors[1:]:
                stack.append(nb)
            stack.append(neighbors[0])
        # else: 0 个邻居 → 到达终点, 自然出栈

    return path


def extract_all_paths(skeleton):
    """
    从骨架中提取所有连通域的有序路径。

    每个连通域产生一个或多个有序坐标链:
      从所有端点出发 DFS, 已访问的像素不会被重复追踪。

    参数:
        skeleton: 二值骨架掩膜

    返回:
        路径列表, 每条路径是 [(x,y), ...] 的列表
    """
    work = skeleton.copy()
    all_paths = []

    while True:
        endpoints = find_endpoints(work)
        if not endpoints:
            # 没有端点 → 可能是闭合环或只剩散点
            # 找到任意骨架像素继续
            ys, xs = np.where(work > 0)
            if len(ys) == 0:
                break
            endpoints = [(ys[0], xs[0])]

        path = dfs_trace(work, endpoints[0])
        if len(path) > 1:
            all_paths.append(path)

        # 从工作副本中擦除已追踪的像素
        for x, y in path:
            work[y, x] = 0

    return all_paths


def save_paths_to_file(paths, output_path):
    """
    将路径坐标列表写入文本文件。

    每行格式: x,y  (逗号分隔, 无空格)
    多条路径之间用空行隔开。

    参数:
        paths:       路径列表
        output_path: 输出文件路径
    """
    with open(output_path, 'w', encoding='utf-8') as f:
        for i, path in enumerate(paths):
            for x, y in path:
                f.write(f"{x},{y}\n")
            if i < len(paths) - 1:
                f.write("\n")  # 路径间空行分隔
    print(f"  [→] 坐标文件: {output_path}  ({len(paths)} 段)")


def draw_verify_image(img_bgr, main_paths, sub_paths, output_path):
    """
    生成验证预览图。

    底图: 清理宝箱后的灰度图
    叠加: 红色路径 → 半透明白色 3px 宽线条
          蓝色路径 → 半透明青色 3px 宽线条

    参数:
        img_bgr:     清理宝箱后的 BGR 图像
        main_paths:  主路径(酡红)坐标列表
        sub_paths:   辅路(蔚蓝)坐标列表
        output_path: 输出 PNG 路径
    """
    # 底图: 灰度
    gray = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2GRAY)
    canvas = cv2.cvtColor(gray, cv2.COLOR_GRAY2BGR)

    # 生成叠加层 (全黑底 + 彩色路径)
    overlay = np.zeros_like(canvas)

    # 画主路径: 半透明白色
    for path in main_paths:
        if len(path) < 2:
            continue
        pts = np.array([(x, y) for x, y in path], dtype=np.int32)
        cv2.polylines(overlay, [pts], isClosed=False, color=(255, 255, 255),
                      thickness=VERIFY_LINE_WIDTH, lineType=cv2.LINE_AA)

    # 画辅路: 半透明青色
    for path in sub_paths:
        if len(path) < 2:
            continue
        pts = np.array([(x, y) for x, y in path], dtype=np.int32)
        cv2.polylines(overlay, [pts], isClosed=False, color=(255, 255, 128),
                      thickness=VERIFY_LINE_WIDTH, lineType=cv2.LINE_AA)

    # Alpha 混合
    canvas = cv2.addWeighted(canvas, 1.0, overlay, VERIFY_ALPHA, 0)

    cv2.imwrite(output_path, canvas)
    print(f"  [→] 验证图: {output_path}")


def process_image(img_path, output_dir, preview_only=False, tolerance=18):
    """
    处理单张攻略图的主流程。

    参数:
        img_path:     输入图片路径
        output_dir:   输出目录
        preview_only: 是否仅输出验证图

    返回:
        bool: 是否成功
    """
    basename = os.path.splitext(os.path.basename(img_path))[0]
    print(f"\n{'─'*60}")
    print(f"处理: {os.path.basename(img_path)}")

    # ── 1. 读取图像 ──
    img_bgr = cv2.imdecode(np.fromfile(img_path, dtype=np.uint8), cv2.IMREAD_COLOR)
    if img_bgr is None:
        print(f"  [✗] 无法读取图片")
        return False
    print(f"  [√] 图像尺寸: {img_bgr.shape[1]}×{img_bgr.shape[0]}")

    # ── 2. 移除宝箱图标 (鹅黄色 → 深灰背景) ──
    mask_chest = mask_by_rgb_tolerance(img_bgr, CHEST_RGB, tolerance)
    chest_pixels = np.count_nonzero(mask_chest)
    img_clean = remove_chest_icons(img_bgr, mask_chest)
    print(f"  [√] 宝箱掩膜: {chest_pixels} px → 已替换为深灰(60,60,60)")

    # ── 3. 提取酡红色主路径 ──
    _, main_skeleton = extract_line_mask(img_clean, MAIN_RGB, tolerance)
    main_pixels = np.count_nonzero(main_skeleton)
    print(f"  [√] 主路径骨架: {main_pixels} px")

    # ── 4. 提取蔚蓝色辅路 ──
    _, sub_skeleton = extract_line_mask(img_clean, SUB_RGB, tolerance)
    sub_pixels = np.count_nonzero(sub_skeleton)
    print(f"  [√] 辅路骨架:   {sub_pixels} px")

    # ── 5. 骨架追踪 → 有序坐标链 ──
    main_paths = extract_all_paths(main_skeleton)
    sub_paths  = extract_all_paths(sub_skeleton)
    print(f"  [√] 主路径: {len(main_paths)} 段, 辅路: {len(sub_paths)} 段")

    # ── 6. 生成验证图 ──
    verify_path = os.path.join(output_dir, f"{basename}_verify.png")
    draw_verify_image(img_clean, main_paths, sub_paths, verify_path)

    # ── 7. 输出坐标文件 (非 preview-only 模式) ──
    if not preview_only:
        main_coord_path = os.path.join(output_dir, f"{basename}_main.txt")
        sub_coord_path  = os.path.join(output_dir, f"{basename}_sub.txt")
        save_paths_to_file(main_paths, main_coord_path)
        save_paths_to_file(sub_paths,  sub_coord_path)

    return True


# ═══════════════════════════════════════════════════════════
#  主入口
# ═══════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description="从游戏攻略图中提取红蓝路径像素坐标",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python extract_paths.py ./maps
  python extract_paths.py ./maps --preview-only
  python extract_paths.py ./maps -t 15     (调整容差)
        """
    )
    parser.add_argument("folder", help="存放攻略图(PNG/JPG)的文件夹路径")
    parser.add_argument("--preview-only", action="store_true",
                        help="仅生成验证图, 不输出坐标文件")
    parser.add_argument("-t", "--tolerance", type=int, default=RGB_TOLERANCE,
                        help=f"RGB 容差值 (默认 {RGB_TOLERANCE})")
    args = parser.parse_args()

    tol = args.tolerance

    input_dir = args.folder
    if not os.path.isdir(input_dir):
        print(f"[错误] 文件夹不存在: {input_dir}")
        sys.exit(1)

    # 收集所有图片文件
    exts = ('.png', '.jpg', '.jpeg', '.bmp', '.webp')
    images = sorted([
        os.path.join(input_dir, f) for f in os.listdir(input_dir)
        if f.lower().endswith(exts)
    ])

    if not images:
        print(f"[错误] 文件夹内未找到 PNG/JPG 图片: {input_dir}")
        sys.exit(1)

    print(f"{'='*60}")
    print(f"提取路径坐标 — {'仅预览' if args.preview_only else '完整输出'}")
    print(f"输入目录: {input_dir}")
    print(f"图片数量: {len(images)}")
    print(f"RGB 容差: ±{tol}")
    print(f"目标颜色:")
    print(f"  鹅黄(宝箱) RGB{tuple(CHEST_RGB)} → 替换为深灰")
    print(f"  酡红(主路) RGB{tuple(MAIN_RGB)}")
    print(f"  蔚蓝(辅路) RGB{tuple(SUB_RGB)}")
    print(f"{'='*60}")

    # 逐张处理
    success = 0
    for img_path in images:
        if process_image(img_path, input_dir, args.preview_only, tol):
            success += 1

    print(f"\n{'='*60}")
    print(f"完成: {success}/{len(images)} 张图片处理成功")
    print(f"输出目录: {input_dir}")
    if not args.preview_only:
        print("生成文件: *_main.txt (主路径坐标), *_sub.txt (辅路坐标)")
    print("生成文件: *_verify.png (验证预览图)")


if __name__ == "__main__":
    main()
