// ============================================================
// draw_MapOverlay.cpp — draw_Gui 拆分文件 (v2.47)
// 从 draw_Gui.cpp 中拆分出地图覆盖相关函数：
//   - RDPRecursive / SimplifyPathRDP (路径简化)
//   - LineSegmentsIntersect2D (线段相交检测)
//   - PathGraph::buildFromSavedPaths / PathGraph::dijkstra (路径图)
//   - Draw_MapOverlay (主绘制入口)
// ============================================================

#include "draw_Gui_internal.h"

void RDPRecursive(const std::vector<ImVec2>& points, int start, int end,
                  float epsilonSq, std::vector<bool>& keep) {
    if (end <= start + 1) return;
    float maxDistSq = 0;
    int maxIdx = start;
    for (int i = start + 1; i < end; ++i) {
        float d = PointToSegmentDistanceSq(points[i], points[start], points[end]);
        if (d > maxDistSq) {
            maxDistSq = d;
            maxIdx = i;
        }
    }
    if (maxDistSq > epsilonSq) {
        keep[maxIdx] = true;
        RDPRecursive(points, start, maxIdx, epsilonSq, keep);
        RDPRecursive(points, maxIdx, end, epsilonSq, keep);
    }
}

std::vector<ImVec2> SimplifyPathRDP(const std::vector<ImVec2>& points, float epsilon) {
    if (points.size() <= 2) return points;
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    float epsilonSq = epsilon * epsilon;
    RDPRecursive(points, 0, (int)points.size() - 1, epsilonSq, keep);
    std::vector<ImVec2> result;
    for (size_t i = 0; i < points.size(); ++i)
        if (keep[i]) result.push_back(points[i]);
    return result;
}

bool LineSegmentsIntersect2D(const Vector3A& p1, const Vector3A& p2,
                             const Vector3A& q1, const Vector3A& q2,
                             Vector3A& intersection) {
    float d1x = p2.X - p1.X, d1y = p2.Y - p1.Y;
    float d2x = q2.X - q1.X, d2y = q2.Y - q1.Y;
    float cross = d1x * d2y - d1y * d2x;
    if (fabsf(cross) < 1e-6f) return false;
    float t = ((q1.X - p1.X) * d2y - (q1.Y - p1.Y) * d2x) / cross;
    float u = ((q1.X - p1.X) * d1y - (q1.Y - p1.Y) * d1x) / cross;

    // 修复：排除线段共享端点导致的无限循环
    if (t > 1e-4f && t < 1.0f - 1e-4f && u > 1e-4f && u < 1.0f - 1e-4f) {
        intersection.X = p1.X + t * d1x;
        intersection.Y = p1.Y + t * d1y;
        intersection.Z = p1.Z;
        return true;
    }
    return false;
}

void PathGraph::buildFromSavedPaths(const std::vector<std::vector<Vector3A>>& paths,
                                    const std::vector<Vector3A>& exits) {
    nodes.clear();
    edges.clear();

    struct Segment {
        Vector3A start, end;
        std::vector<Vector3A> intermediatePoints;
    };
    std::vector<Segment> segments;

    auto addNode = [&](const Vector3A& pos) -> int {
        for (size_t i = 0; i < nodes.size(); ++i) {
            float dx = nodes[i].pos.X - pos.X;
            float dy = nodes[i].pos.Y - pos.Y;
            float dz = nodes[i].pos.Z - pos.Z;
            if (dx*dx + dy*dy + dz*dz < 1.0f) return (int)i;
        }
        GraphNode node;
        node.pos = pos;
        node.id = (int)nodes.size();
        nodes.push_back(node);
        return node.id;
    };

    for (const auto& path : paths) {
        if (path.size() < 2) continue;
        for (size_t i = 0; i < path.size() - 1; ++i) {
            Segment seg;
            seg.start = path[i];
            seg.end = path[i+1];
            seg.intermediatePoints.push_back(path[i]);
            seg.intermediatePoints.push_back(path[i+1]);
            segments.push_back(seg);
        }
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < segments.size(); ++i) {
            for (size_t j = i + 1; j < segments.size(); ++j) {
                Vector3A inter;
                if (LineSegmentsIntersect2D(segments[i].start, segments[i].end,
                                            segments[j].start, segments[j].end, inter)) {
                    Segment segI1 = segments[i]; segI1.end = inter;
                    segI1.intermediatePoints = {segments[i].start, inter};
                    Segment segI2 = segments[i]; segI2.start = inter;
                    segI2.intermediatePoints = {inter, segments[i].end};
                    Segment segJ1 = segments[j]; segJ1.end = inter;
                    segJ1.intermediatePoints = {segments[j].start, inter};
                    Segment segJ2 = segments[j]; segJ2.start = inter;
                    segJ2.intermediatePoints = {inter, segments[j].end};

                    segments.erase(segments.begin() + std::max(i, j));
                    segments.erase(segments.begin() + std::min(i, j));
                    segments.push_back(segI1);
                    segments.push_back(segI2);
                    segments.push_back(segJ1);
                    segments.push_back(segJ2);
                    changed = true;
                    break;
                }
            }
            if (changed) break;
        }
    }

    for (auto& seg : segments) {
        int from = addNode(seg.start);
        int to   = addNode(seg.end);
        if (from == to) continue;
        GraphEdge edge;
        edge.from = from;
        edge.to = to;
        edge.weight = sqrtf((seg.end.X-seg.start.X)*(seg.end.X-seg.start.X) +
                            (seg.end.Y-seg.start.Y)*(seg.end.Y-seg.start.Y) +
                            (seg.end.Z-seg.start.Z)*(seg.end.Z-seg.start.Z));
        edge.pathPoints = seg.intermediatePoints;
        edges.push_back(edge);
    }

    for (auto& e : exits) {
        int idx = addNode(e);
        nodes[idx].isExit = true;
    }

    adj.assign(nodes.size(), std::vector<int>());
    for (size_t i = 0; i < edges.size(); ++i) {
        adj[edges[i].from].push_back((int)i);
        adj[edges[i].to].push_back((int)i);
    }

    dirty = false;
}

std::vector<int> PathGraph::dijkstra(int startNode) const {
    const int n = nodes.size();
    std::vector<float> dist(n, 1e30f);
    std::vector<int> prevEdge(n, -1);
    using P = std::pair<float, int>;
    std::priority_queue<P, std::vector<P>, std::greater<P>> pq;
    dist[startNode] = 0;
    pq.push({0, startNode});

    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d != dist[u]) continue;
        for (int ei : adj[u]) {
            int v = (edges[ei].from == u) ? edges[ei].to : edges[ei].from;
            float nd = d + edges[ei].weight;
            if (nd < dist[v]) {
                dist[v] = nd;
                prevEdge[v] = ei;
                pq.push({nd, v});
            }
        }
    }
    return prevEdge;
}


void Draw_MapOverlay(ImDrawList* Draw, const std::vector<DataStruct>& data) {
    if (!g_map_enabled) return;
    if (g_current_map_index < 0 || g_current_map_index >= (int)g_all_maps.size()) return;
    if (g_all_maps[g_current_map_index].empty()) return;

    // 安全钳制层数索引，防止越界读取导致显示错误地图
    g_current_floor_index = SafeClampFloorIdx(g_current_map_index, g_current_floor_index);

    // 检测地图或楼层变化 → 需要重新加载纹理
    if (g_last_rendered_map_index != g_current_map_index ||
        g_last_rendered_floor_index != g_current_floor_index) {
        g_last_rendered_map_index = g_current_map_index;
        g_last_rendered_floor_index = g_current_floor_index;
        // 强制重新加载纹理（不管之前是否有缓存）
        LoadMapTexture(g_current_map_index, g_current_floor_index);
    }

    UpdateCurrentFloor();

    MapConfig cfg = g_all_maps[g_current_map_index][g_current_floor_index];

    // 地图已校准 → 自动关闭校准模式（避免坐标标记遮挡视野）
    if (cfg.calibrated && g_use_calib) {
        g_use_calib = false;
    }

    float map_h = g_map_display_size;
    float worldW = cfg.maxX - cfg.minX;
    float worldH = cfg.maxY - cfg.minY;
    float map_w = cfg.isVerticalMap ? (map_h * worldH / worldW) : (map_h * worldW / worldH);
    ImVec2 map_pos(g_map_pos_x, g_map_pos_y);
    ImVec2 map_end(map_pos.x + map_w, map_pos.y + map_h);
    Draw->AddRect(map_pos, map_end, ImColor(255, 0, 0, 255), 0, 0, 3.0f);

    // 绘制底图
    GLuint tex = g_map_textures[g_current_map_index][g_current_floor_index];
    if (tex) {
        Draw->AddImage((void*)(intptr_t)tex, map_pos, map_end, ImVec2(0,0), ImVec2(1,1),
                       IM_COL32(255, 255, 255, (int)(255 * g_map_opacity)));
    } else {
        Draw->AddRectFilled(map_pos, map_end, IM_COL32(30, 30, 30, 250), 5.0f);
        Draw->AddText(ImVec2(map_pos.x + 10, map_pos.y + 10), ImColor(255, 200, 200, 255), "纹理未加载:");
        Draw->AddText(ImVec2(map_pos.x + 10, map_pos.y + 30), ImColor(255, 150, 150, 255), g_texture_status);
    }
    Draw->AddRect(map_pos, map_end, ImColor(255, 255, 255, 100), 5.0f, 0, 1.5f);

    // ========== 地图/楼层标识（固定在左上角，半透明背景）==========
    {
        char map_label[128];
        snprintf(map_label, sizeof(map_label), "第 %d 地图 - 第 %d 层",
                 g_current_map_index + 1, g_current_floor_index + 1);
        if (cfg.calibrated) {
            strcat(map_label, " [已校准]");
        } else {
            strcat(map_label, " [未校准]");
        }
        ImVec2 label_sz = ImGui::CalcTextSize(map_label);
        float label_pad = 6.0f;
        ImVec2 label_pos(map_pos.x + label_pad, map_pos.y + label_pad);
        Draw->AddRectFilled(
            ImVec2(label_pos.x - 4, label_pos.y - 2),
            ImVec2(label_pos.x + label_sz.x + 8, label_pos.y + label_sz.y + 4),
            IM_COL32(0, 0, 0, 160), 4.0f);
        Draw->AddText(g_font_ui, 14.0f, label_pos,
                      cfg.calibrated ? IM_COL32(100, 255, 100, 240) : IM_COL32(255, 200, 100, 240),
                      map_label);
    }

    auto ToMap = [&](const Vector3A& pos) -> ImVec2 {
        float sx, sy, ou, ov;
        bool fx, fy;
        if (cfg.calibrated) {
            sx = cfg.scaleX; sy = cfg.scaleY;
            ou = cfg.offsetU; ov = cfg.offsetV;
            fx = cfg.flipX; fy = cfg.flipY;
        } else {
            sx = g_map_scale_x; sy = g_map_scale_y;
            ou = g_map_offset_u; ov = g_map_offset_v;
            fx = g_map_flip_x; fy = g_map_flip_y;
        }
        float u = pos.X * sx + ou;
        float v = pos.Y * sy + ov;
        if (fx) u = 1.0f - u;
        if (fy) v = 1.0f - v;
        return ImVec2(map_pos.x + u * map_w, map_pos.y + v * map_h);
    };

    // ========== 绘制网格参考线 ==========
    if (g_show_grid && cfg.calibrated) {
        float worldLeft = cfg.minX, worldRight = cfg.maxX;
        float worldTop = cfg.minY, worldBottom = cfg.maxY;
        float spacing = g_grid_spacing;
        int startX = (int)floorf(worldLeft / spacing);
        int endX   = (int)ceilf(worldRight / spacing);
        int startY = (int)floorf(worldTop / spacing);
        int endY   = (int)ceilf(worldBottom / spacing);

        ImU32 gridColor = IM_COL32(255, 255, 255, (int)(g_grid_alpha * 255));
        for (int i = startX; i <= endX; ++i) {
            float wx = i * spacing;
            ImVec2 p1 = ToMap(Vector3A(wx, worldTop, 0));
            ImVec2 p2 = ToMap(Vector3A(wx, worldBottom, 0));
            Draw->AddLine(p1, p2, gridColor, 1.0f);
        }
        for (int j = startY; j <= endY; ++j) {
            float wy = j * spacing;
            ImVec2 p1 = ToMap(Vector3A(worldLeft, wy, 0));
            ImVec2 p2 = ToMap(Vector3A(worldRight, wy, 0));
            Draw->AddLine(p1, p2, gridColor, 1.0f);
        }
    }

    // ========== 按地图/楼层同步当前路径视图 ==========
    if (g_current_map_index != g_last_paths_map_idx || g_current_floor_index != g_last_paths_floor_idx) {
        g_saved_paths.clear();
        if (g_current_map_index >= 0 && g_current_map_index < (int)g_saved_paths_by_map.size() &&
            g_current_floor_index >= 0 && g_current_floor_index < (int)g_saved_paths_by_map[g_current_map_index].size()) {
            g_saved_paths = g_saved_paths_by_map[g_current_map_index][g_current_floor_index];
            g_path_visible.clear();
            g_path_colors.clear();
            for (size_t v = 0; v < g_saved_paths.size(); v++) { g_path_visible.push_back(true); g_path_colors.push_back(0); }
        }
        g_last_paths_map_idx = g_current_map_index;
        g_last_paths_floor_idx = g_current_floor_index;
        g_path_cache_dirty = true;  // ★ P2: 地图/楼层切换时重建顶点缓存
    }

    // 绘制自身位置 + 手电筒光锥朝向
    if (Z.X != 0 || Z.Y != 0) {
        ImVec2 self = ToMap(Z);
        float angle = atan2f(matrix[10], matrix[8]) + (cfg.isVerticalMap ? 1.57f : 3.14f);

        // 扇形光锥（半透明黄色三角扇）
        const float cone_radius = 28.0f;
        const float half_fov = 28.0f * (3.14159265f / 180.0f); // ±28度, 总56度视野
        const int segs = 7; // 弧线分段
        ImVec2 pts[2 + segs]; // 顶点 + segs+1个弧点
        pts[0] = self;
        for (int i = 0; i <= segs; i++) {
            float a = angle - half_fov + (2.0f * half_fov) * (float)i / (float)segs;
            pts[1 + i] = ImVec2(self.x + cosf(a) * cone_radius, self.y + sinf(a) * cone_radius);
        }
        Draw->AddConvexPolyFilled(pts, segs + 2, IM_COL32(255, 220, 60, 70));
        // 光锥边框
        Draw->AddLine(self, pts[1], IM_COL32(255, 220, 60, 120), 1.5f);
        Draw->AddLine(self, pts[segs + 1], IM_COL32(255, 220, 60, 120), 1.5f);
        Draw->AddPolyline(&pts[1], segs + 1, IM_COL32(255, 220, 60, 120), ImDrawFlags_Closed, 1.5f);

        // 玩家圆点（亮黄色实心）
        Draw->AddCircleFilled(self, 6.5f, IM_COL32(255, 240, 40, 240));
    }

    auto GetOneCharLabel = [](const char* prop_name) -> const char* {
        if (strstr(prop_name, "紫宝箱"))   return "紫";
        if (strstr(prop_name, "金宝箱"))   return "金";
        if (strstr(prop_name, "隐藏宝箱")) return "藏";
        if (strstr(prop_name, "小箱子"))   return "小";
        if (strstr(prop_name, "陷阱") || strstr(prop_name, "夹子") || strstr(prop_name, "碎石")) return "阱";
        if (strstr(prop_name, "穿梭门"))   return "门";
        if (strstr(prop_name, "钢琴"))     return "琴";
        if (strstr(prop_name, "凳子"))     return "凳";
        if (strstr(prop_name, "板"))       return "板";
        if (strstr(prop_name, "花瓶"))     return "瓶";
        return "·";
    };

    // ========== 物品循环 ==========
    for (const auto& item : data) {
        if (item.阵营 != 6 && item.阵营 != 4) continue;
        bool isChest = (strstr(item.prop_name, "[紫宝箱]") || strstr(item.prop_name, "[金宝箱]") ||
                        strstr(item.prop_name, "[隐藏宝箱]"));
        int price = ExtractPrice(item.prop_name);
        bool isMonster = (strstr(item.类名, "monster") != nullptr);
        bool isHiddenDoor = (strcmp(item.prop_name, "[隐藏开关门]") == 0);

        if (strcmp(item.prop_name, "[被遗忘的信仰 20000]") == 0 ||
            strcmp(item.prop_name, "[庇护者之战 15000]") == 0) {
            continue;
        }

        if (!isHiddenDoor) {
            if (!isChest && !isMonster && price < g_treasure_threshold) continue;
            if (isMonster && !MjSubsystem::show_monsters) continue;
        }

        Vector3A pos = getObjectCoordinates(item.objcoor, true);
        if (!isValidCoordinate(pos)) continue;

        if (g_current_floor_index == 0) { if (pos.Z > 190.0f) continue; }
        else if (g_current_floor_index == 1) { if (pos.Z <= 190.0f) continue; }

        if (isMonster) {
            float dist = FastMath::fastDistance(pos, Z) / 距离比例;
            if (dist > MjSubsystem::max_dist_monsters) continue;
        }

        ImVec2 p = ToMap(pos);

        ImColor color;
        if (isMonster) color = ImColor(255, 0, 0, 255);
        else if (isHiddenDoor) color = ImColor(0, 255, 255, 255);
        else if (strstr(item.prop_name, "[紫宝箱]")) color = ImColor(180, 0, 255, 255);
        else if (isChest) color = ImColor(255, 215, 0, 255);
        else color = ImColor(255, 0, 255, 255);

        Draw->AddCircleFilled(p, isMonster ? 4.0f : 5.0f, color);

        float label_font = ImGui::GetFontSize() * g_map_label_scale;
        if (isMonster) {
            Draw->AddText(ImGui::GetFont(), label_font * 0.85f,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(255, 200, 200, (int)(255 * g_label_opacity)), "怪");
        } else if (isHiddenDoor) {
            Draw->AddText(ImGui::GetFont(), label_font * 0.85f,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(0, 255, 255, (int)(255 * g_label_opacity)), "隐");
        } else {
            const char* onechar = GetOneCharLabel(item.prop_name);
            Draw->AddText(ImGui::GetFont(), label_font,
                          ImVec2(p.x - 6, p.y - 6), IM_COL32(255, 255, 255, (int)(255 * g_label_opacity)), onechar);
        }
    }

    // ========== 已保存路径绘制（带开关控制 + 选中高亮 + 顶点缓存P2优化） ==========
    if (g_show_saved_paths) {
        // ★ P2+P5: 顶点缓存 — 地图拖动/缩放/路径变化时自动重建
        if (g_path_cache_dirty || g_saved_paths.size() != g_path_vertex_cache.size()
            || g_map_pos_x != g_last_cache_map_pos_x || g_map_pos_y != g_last_cache_map_pos_y
            || g_map_display_size != g_last_cache_map_size) {
            g_path_vertex_cache.resize(g_saved_paths.size());
            for (size_t pi = 0; pi < g_saved_paths.size(); pi++) {
                auto& src = g_saved_paths[pi];
                auto& dst = g_path_vertex_cache[pi];
                dst.clear(); dst.reserve(src.size());
                ImVec2 last(-99999, -99999);
                for (size_t i = 0; i < src.size(); i++) {
                    ImVec2 p = ToMap(src[i]);
                    float dx = p.x - last.x, dy = p.y - last.y;
                    // ★ P5: 5px 内合并，保留最后一个点
                    if (dst.empty() || dx*dx + dy*dy > 25.0f || i == src.size() - 1) {
                        dst.push_back(p); last = p;
                    } else { dst.back() = p; }
                }
            }
            g_path_cache_dirty = false;
            g_last_cache_map_pos_x = g_map_pos_x;
            g_last_cache_map_pos_y = g_map_pos_y;
            g_last_cache_map_size = g_map_display_size;
        }
        for (size_t pi = 0; pi < g_saved_paths.size(); pi++) {
            if (pi < g_path_visible.size() && !g_path_visible[pi]) continue;
            if (g_show_nav_line && !g_nav_render_path.empty() && g_dest_world_x != 0) continue;
            auto& path = g_path_vertex_cache[pi];
            if (path.size() < 2) continue;

            bool isSelected = (pi == g_selected_path_index);

            ImU32 color_outer = isSelected
                ? IM_COL32(255, 255, 0, (int)(120 * g_saved_path_opacity))
                : IM_COL32(0, 180, 255, (int)(60 * g_saved_path_opacity));
            ImU32 color_inner = isSelected
                ? IM_COL32(255, 255, 100, (int)(255 * g_saved_path_opacity))
                : IM_COL32(0, 220, 255, (int)(200 * g_saved_path_opacity));
            float outer_width = isSelected ? 18.0f : 12.0f;
            float inner_width = isSelected ? 5.0f : 3.0f;

            for (size_t i = 1; i < path.size(); i++) {
                ImVec2 p1 = path[i-1];
                ImVec2 p2 = path[i];
                Draw->AddLine(p1, p2, color_outer, outer_width);
                Draw->AddLine(p1, p2, color_inner, inner_width);
            }
            ImVec2 start = path.front();
            ImVec2 end = path.back();
            Draw->AddCircleFilled(start, isSelected ? 7.0f : 5.0f,
                isSelected ? IM_COL32(255, 255, 0, 255) : IM_COL32(0, 255, 0, 200));
            Draw->AddCircleFilled(end, isSelected ? 7.0f : 5.0f,
                isSelected ? IM_COL32(255, 200, 0, 255) : IM_COL32(255, 0, 0, 200));
        }
    }

    // ========== 导入预览路径（黄色虚线） ==========
    if (g_has_import_preview &&
        g_import_preview_map_idx == g_current_map_index &&
        g_import_preview_floor_idx == g_current_floor_index) {
        for (size_t pi = 0; pi < g_import_preview_paths.size(); pi++) {
            auto& path = g_import_preview_paths[pi];
            if (path.size() < 2) continue;
            // 黄色虚线效果：每段 8px 实线 + 6px 间隔
            for (size_t i = 1; i < path.size(); i++) {
                ImVec2 p1 = ToMap(path[i-1]);
                ImVec2 p2 = ToMap(path[i]);
                float dx = p2.x - p1.x, dy = p2.y - p1.y;
                float len = sqrtf(dx*dx + dy*dy);
                if (len < 1.0f) continue;
                float ux = dx / len, uy = dy / len;
                float drawn = 0.0f;
                bool solid = true;
                while (drawn < len) {
                    float seg = solid ? 8.0f : 6.0f;
                    if (drawn + seg > len) seg = len - drawn;
                    ImVec2 s(p1.x + ux * drawn, p1.y + uy * drawn);
                    ImVec2 e(p1.x + ux * (drawn + seg), p1.y + uy * (drawn + seg));
                    if (solid) {
                        Draw->AddLine(s, e, IM_COL32(255, 255, 0, 180), 4.0f);
                    }
                    drawn += seg;
                    solid = !solid;
                }
            }
            // 起点绿色圆点，终点红色圆点
            ImVec2 start = ToMap(path.front());
            ImVec2 end = ToMap(path.back());
            Draw->AddCircleFilled(start, 5.0f, IM_COL32(0, 255, 0, 200));
            Draw->AddCircleFilled(end, 5.0f, IM_COL32(255, 0, 0, 200));
        }
        // 预览标签
        ImVec2 label_pos(map_pos.x + map_w - 80, map_pos.y + 4);
        Draw->AddRectFilled(ImVec2(label_pos.x - 4, label_pos.y - 2),
                            ImVec2(label_pos.x + 80, label_pos.y + 20),
                            IM_COL32(0, 0, 0, 160), 4.0f);
        Draw->AddText(g_font_ui, 14.0f, label_pos,
                      IM_COL32(255, 255, 0, 240), "预览 [黄色虚线]");
    }

    // ========== 正在绘制的路径 ==========
    if (!g_current_drawing_path.empty()) {
        for (size_t i = 0; i < g_current_drawing_path.size(); i++) {
            ImVec2 p = ToMap(g_current_drawing_path[i]);
            Draw->AddCircleFilled(p, 5.0f, IM_COL32(255, 255, 0, (int)(200 * g_route_opacity)));
            if (i > 0) {
                ImVec2 prev_p = ToMap(g_current_drawing_path[i-1]);
                Draw->AddLine(prev_p, p, IM_COL32(255, 255, 0, (int)(180 * g_route_opacity)), 4.0f);
            }
        }
        // ★ 吸附辅助：绘制模式下高亮附近可吸附端点（白色虚线环）
        if (g_path_edit_mode == 1) {
            for (auto& pth : g_saved_paths) {
                if (pth.size() < 2) continue;
                for (int ep : {0, (int)pth.size()-1}) {
                    ImVec2 ep_s = ToMap(pth[ep]);
                    Draw->AddCircle(ep_s, 22.0f, IM_COL32(255,255,255,100), 16, 2.0f);
                    Draw->AddCircleFilled(ep_s, 4.0f, IM_COL32(255,255,255,160));
                }
            }
        }
    }

    // 出口调试十字准星：点击位置 vs 实际渲染位置（使用UV直接渲染，验证一致性）
    if (g_show_exit_debug &&
        g_current_map_index >= 0 && g_current_map_index < (int)g_exits.size()) {
        auto& floors = g_exits[g_current_map_index];
        auto& uv_floors = g_exit_uvs[g_current_map_index];
        if (g_current_floor_index >= 0 && g_current_floor_index < (int)floors.size() && !floors.empty()
            && g_current_floor_index < (int)uv_floors.size() && !uv_floors.empty()) {
        auto& uv_e = uv_floors[g_current_floor_index].back();
        ImVec2 rendered = ImVec2(map_pos.x + uv_e.x * map_w, map_pos.y + uv_e.y * map_h);
        g_last_exit_rendered_pos = rendered;
        // 点击位置 - 黄色十字
        Draw->AddLine(ImVec2(g_last_exit_screen_pos.x - 15, g_last_exit_screen_pos.y),
                      ImVec2(g_last_exit_screen_pos.x + 15, g_last_exit_screen_pos.y),
                      IM_COL32(255, 255, 0, 200), 2.0f);
        Draw->AddLine(ImVec2(g_last_exit_screen_pos.x, g_last_exit_screen_pos.y - 15),
                      ImVec2(g_last_exit_screen_pos.x, g_last_exit_screen_pos.y + 15),
                      IM_COL32(255, 255, 0, 200), 2.0f);
        // 渲染位置 - 红色十字
        Draw->AddLine(ImVec2(rendered.x - 15, rendered.y),
                      ImVec2(rendered.x + 15, rendered.y),
                      IM_COL32(255, 0, 0, 200), 2.0f);
        Draw->AddLine(ImVec2(rendered.x, rendered.y - 15),
                      ImVec2(rendered.x, rendered.y + 15),
                      IM_COL32(255, 0, 0, 200), 2.0f);
            }
        }

    // ========== 出口标记（UV空间渲染，与点击坐标完全一致） ==========
    if (g_current_map_index < g_exits.size() && g_current_floor_index < g_exits[g_current_map_index].size()) {
        auto& exits = g_exits[g_current_map_index][g_current_floor_index];
        auto& uv_exits = g_exit_uvs[g_current_map_index][g_current_floor_index];
        // 确保 uv_exits 与 exits 数量一致
        while (uv_exits.size() < exits.size()) uv_exits.push_back(ImVec2(0,0));
        for (size_t ei = 0; ei < exits.size(); ei++) {
            auto& e = exits[ei];
            ImVec2 uv = (ei < uv_exits.size()) ? uv_exits[ei] : ImVec2(0,0);
            // 直接使用UV坐标计算屏幕位置，不经过ToMap（避免任何变换误差）
            ImVec2 p = ImVec2(map_pos.x + uv.x * map_w, map_pos.y + uv.y * map_h);
            // 出口方块加编号
            Draw->AddRectFilled(ImVec2(p.x-6, p.y-6), ImVec2(p.x+6, p.y+6), IM_COL32(0, 255, 80, (int)(220 * g_label_opacity)), 2.0f);
            char eLabel[16];
            snprintf(eLabel, sizeof(eLabel), "目的地%zu", ei+1);
            Draw->AddText(ImGui::GetFont(), 12.0f, ImVec2(p.x + 8, p.y - 8), IM_COL32(0, 255, 80, (int)(200 * g_label_opacity)), eLabel);

            // 点击出口开始拖动
            ImVec2 ms = ImGui::GetMousePos();
            float d = sqrtf((ms.x - p.x)*(ms.x - p.x) + (ms.y - p.y)*(ms.y - p.y));
            if (d < 15.0f) {
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && g_drag_exit_idx < 0) {
                    g_drag_exit_idx = (int)ei;
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    g_del_exit_idx = (int)ei;
                    ImGui::OpenPopup("ExitDeletePopup");
                }
            }
            // 拖拽更新UV（直接即时更新屏幕位置，无任何转换）
            if ((int)ei == g_drag_exit_idx && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                float u_drag = std::clamp((ms.x - map_pos.x) / map_w, 0.0f, 1.0f);
                float v_drag = std::clamp((ms.y - map_pos.y) / map_h, 0.0f, 1.0f);
                uv_exits[ei] = ImVec2(u_drag, v_drag);
                // 同步更新世界坐标（使用 CoordTransform 统一管道）
                const auto& drag_cfg = GetActiveMapConfig();
                e.X = CoordTransform::UVToX(u_drag, drag_cfg);
                e.Y = CoordTransform::UVToY(v_drag, drag_cfg);
                // 拖动时显示高亮边框
                Draw->AddRect(ImVec2(p.x-9, p.y-9), ImVec2(p.x+9, p.y+9), IM_COL32(255, 255, 255, 255), 2.0f, 0, 2.0f);
            }
            // 松开鼠标保存
            if ((int)ei == g_drag_exit_idx && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                g_drag_exit_idx = -1;
                MarkExitsDirty();
                AddNotification("目的地位置已更新", 1.5f, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
            }
        }
    }

    // 出口删除确认弹窗
    if (ImGui::BeginPopup("ExitDeletePopup")) {
        ImGui::Text("删除此目的地?");
        if (ImGui::Button("确定删除")) {
            g_exits[g_current_map_index][g_current_floor_index].erase(
                g_exits[g_current_map_index][g_current_floor_index].begin() + g_del_exit_idx);
            if (g_del_exit_idx < (int)g_exit_uvs[g_current_map_index][g_current_floor_index].size()) {
                g_exit_uvs[g_current_map_index][g_current_floor_index].erase(
                    g_exit_uvs[g_current_map_index][g_current_floor_index].begin() + g_del_exit_idx);
            }
            SaveExitsToJSON(g_current_map_index, g_current_floor_index);
            AddNotification("目的地已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ========== 校准标记绘制与交互 ==========
    if (g_use_calib) {
        auto TexToScreen = [&](float u, float v) -> ImVec2 {
            return ImVec2(map_pos.x + u * map_w, map_pos.y + v * map_h);
        };

        ImVec2 p1 = TexToScreen(g_pt1_tu, g_pt1_tv);
        Draw->AddLine(ImVec2(p1.x - 15, p1.y), ImVec2(p1.x + 15, p1.y), ImColor(255, 165, 0, 220), 2.0f);
        Draw->AddLine(ImVec2(p1.x, p1.y - 15), ImVec2(p1.x, p1.y + 15), ImColor(255, 165, 0, 220), 2.0f);
        Draw->AddCircle(p1, 8.0f, ImColor(255, 165, 0, 255), 0, 2.0f);
        float cal_font = ImGui::GetFontSize() * g_map_label_scale;
        Draw->AddText(ImGui::GetFont(), cal_font, ImVec2(p1.x + 12, p1.y - 12), ImColor(255, 200, 100, 255), "音乐盒");

        ImVec2 p2 = TexToScreen(g_pt2_tu, g_pt2_tv);
        Draw->AddLine(ImVec2(p2.x - 15, p2.y), ImVec2(p2.x + 15, p2.y), ImColor(0, 150, 255, 220), 2.0f);
        Draw->AddLine(ImVec2(p2.x, p2.y - 15), ImVec2(p2.x, p2.y + 15), ImColor(0, 150, 255, 220), 2.0f);
        Draw->AddCircle(p2, 8.0f, ImColor(0, 150, 255, 255), 0, 2.0f);
        Draw->AddText(ImGui::GetFont(), cal_font, ImVec2(p2.x + 12, p2.y - 12), ImColor(100, 200, 255, 255), "大门");

        ImVec2 mouse_pos = ImGui::GetMousePos();
        bool mouse_in_map = (mouse_pos.x >= map_pos.x && mouse_pos.x <= map_end.x &&
                             mouse_pos.y >= map_pos.y && mouse_pos.y <= map_end.y);

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse_in_map) {
            float dist1 = sqrtf((mouse_pos.x - p1.x) * (mouse_pos.x - p1.x) + (mouse_pos.y - p1.y) * (mouse_pos.y - p1.y));
            float dist2 = sqrtf((mouse_pos.x - p2.x) * (mouse_pos.x - p2.x) + (mouse_pos.y - p2.y) * (mouse_pos.y - p2.y));
            if (dist1 < 20.0f && dist1 <= dist2) g_drag_point = 1;
            else if (dist2 < 20.0f) g_drag_point = 2;
            else g_drag_point = 0;
        }

        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && g_drag_point != 0 && mouse_in_map) {
            float u = (mouse_pos.x - map_pos.x) / map_w;
            float v = (mouse_pos.y - map_pos.y) / map_h;
            u = std::clamp(u, 0.0f, 1.0f);
            v = std::clamp(v, 0.0f, 1.0f);
            if (g_drag_point == 1) {
                if (g_pt1_history.empty() || g_pt1_history.back().first != g_pt1_tu || g_pt1_history.back().second != g_pt1_tv) {
                    g_pt1_history.push_back({g_pt1_tu, g_pt1_tv});
                    if (g_pt1_history.size() > MAX_HISTORY) g_pt1_history.erase(g_pt1_history.begin());
                }
                g_pt1_tu = u; g_pt1_tv = v;
            } else if (g_drag_point == 2) {
                if (g_pt2_history.empty() || g_pt2_history.back().first != g_pt2_tu || g_pt2_history.back().second != g_pt2_tv) {
                    g_pt2_history.push_back({g_pt2_tu, g_pt2_tv});
                    if (g_pt2_history.size() > MAX_HISTORY) g_pt2_history.erase(g_pt2_history.begin());
                }
                g_pt2_tu = u; g_pt2_tv = v;
            }
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) g_drag_point = 0;

        if (mouse_in_map) {
            float hint_font = ImGui::GetFontSize() * g_map_label_scale * 0.9f;
            Draw->AddText(ImGui::GetFont(), hint_font,
                          ImVec2(map_pos.x + 5, map_end.y - 22), ImColor(255, 255, 255, 200),
                          g_drag_point == 0 ? "拖拽标记点定位" : (g_drag_point == 1 ? "移动音乐盒..." : "移动大门..."));
        }
    }

    // ========== 长按菜单（替代右键） ==========
    ImVec2 mouse = ImGui::GetMousePos();
    bool hover_map = (mouse.x >= map_pos.x && mouse.x <= map_end.x && mouse.y >= map_pos.y && mouse.y <= map_end.y);

    if (hover_map && cfg.calibrated && g_path_edit_mode == 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (g_press_timer == 0) g_press_pos = mouse;
            g_press_timer += ImGui::GetIO().DeltaTime;
            if (fabsf(mouse.x - g_press_pos.x) > 5 || fabsf(mouse.y - g_press_pos.y) > 5) {
                g_press_timer = 0;
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (g_press_timer > 0.5f) {
                ImGui::OpenPopup("MapLongPressPopup");
            }
            g_press_timer = 0;
        }
    }

    if (ImGui::BeginPopup("MapLongPressPopup")) {
        ImGui::Text("选择操作:");
        ImGui::Separator();

        // ★ 途经点功能已移除

        int nearby_path = -1;
        for (size_t i = 0; i < g_saved_paths.size(); i++) {
            for (size_t j = 0; j < g_saved_paths[i].size(); j++) {
                ImVec2 pp = ToMap(g_saved_paths[i][j]);
                float dist = sqrtf((mouse.x - pp.x)*(mouse.x - pp.x) + (mouse.y - pp.y)*(mouse.y - pp.y));
                if (dist < 10.0f) { nearby_path = i; break; }
            }
            if (nearby_path >= 0) break;
        }
        if (nearby_path >= 0) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                // 左键点击路径 → 选中该路径（高亮）
                g_selected_path_index = nearby_path;
            }
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "选中: 路径 #%d", nearby_path);
            if (ImGui::Button("删除此路径") || ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                g_saved_paths.erase(g_saved_paths.begin() + nearby_path);
                if (g_selected_path_index == nearby_path) g_selected_path_index = -1;
                else if (g_selected_path_index > nearby_path) g_selected_path_index--;
                MarkPathsDirty();
                AddNotification("路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }

    // ========== 全局路径操作：Delete键删除 + 点击空白取消选中 ==========
    if (g_selected_path_index >= 0 && g_selected_path_index < (int)g_saved_paths.size()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            g_saved_paths.erase(g_saved_paths.begin() + g_selected_path_index);
            g_selected_path_index = -1;
            SavePlayerPathsToJSON(g_current_map_index, g_current_floor_index);
            AddNotification("路径已删除", 2.0f, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        }
        // 点击地图空白区域取消选中
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_map) {
            // 检查是否点击在路径上
            bool clicked_on_path = false;
            for (size_t i = 0; i < g_saved_paths.size() && !clicked_on_path; i++) {
                for (size_t j = 0; j < g_saved_paths[i].size() && !clicked_on_path; j++) {
                    ImVec2 pp = ToMap(g_saved_paths[i][j]);
                    float dist = sqrtf((mouse.x - pp.x)*(mouse.x - pp.x) + (mouse.y - pp.y)*(mouse.y - pp.y));
                    if (dist < 10.0f) clicked_on_path = true;
                }
            }
            if (!clicked_on_path) g_selected_path_index = -1;
        }
    }

    // ========== 触摸拖动绘制路径（正交模式：水平/垂直 + 直角转角） ==========
    if (g_path_edit_mode == 1) {
        // 辅助：获取鼠标对应的世界坐标（不吸附——移除网格吸附功能）
        // 用户点击的精确位置即是路径点，不再自动吸附到网格交叉点
        auto GetMouseWorldWithSnap = [&](const ImVec2& mpos) -> Vector3A {
            float u = (mpos.x - map_pos.x) / map_w;
            float v = (mpos.y - map_pos.y) / map_h;
            float worldX = (u - cfg.offsetU) / cfg.scaleX;
            float worldY = (v - cfg.offsetV) / cfg.scaleY;
            return Vector3A(worldX, worldY, Z.Z);
        };

        // 辅助：将正交约束应用到目标点（相对于参考点强制水平/垂直）
        auto ApplyOrthoConstraint = [&](const Vector3A& target, const Vector3A& ref) -> Vector3A {
            if (!g_ortho_draw) return target;
            float dx = fabsf(target.X - ref.X);
            float dy = fabsf(target.Y - ref.Y);
            Vector3A constrained = target;
            // 偏置：水平方向乘1.5，让水平拖动更容易触发水平线段
            if (dx * 1.5f > dy)
                constrained.Y = ref.Y;  // 水平线段：Y锁定
            else
                constrained.X = ref.X;  // 垂直线段：X锁定
            return constrained;
        };

        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && hover_map) {
            if (g_press_timer == 0) g_press_pos = mouse;
            g_press_timer += ImGui::GetIO().DeltaTime;

            // 长按0.3秒开始绘制
            if (g_press_timer > 0.3f && !g_path_drawing_active) {
                g_path_drawing_active = true;
                g_current_drawing_path.clear();
                g_path_start_pos = mouse;
                Vector3A firstPt = GetMouseWorldWithSnap(g_press_pos);
                g_current_drawing_path.push_back(firstPt);
            }

            if (g_path_drawing_active) {
                Vector3A rawPt = GetMouseWorldWithSnap(mouse);
                Vector3A lastPt = g_current_drawing_path.back();
                Vector3A constrained;

                if (g_current_drawing_path.size() == 1) {
                    // 只有起点，根据移动方向确定先走水平还是垂直
                    constrained = ApplyOrthoConstraint(rawPt, lastPt);
                } else {
                    // 每段独立：始终根据鼠标相对于上一点的方向决定横/竖
                    // 这样用户可以自由切换方向，不被上一段锁定
                    if (g_ortho_draw) {
                        constrained = ApplyOrthoConstraint(rawPt, lastPt);
                    } else {
                        constrained = rawPt;
                    }
                }

                float dist = sqrtf((constrained.X - lastPt.X)*(constrained.X - lastPt.X) +
                                   (constrained.Y - lastPt.Y)*(constrained.Y - lastPt.Y));
                if (dist > g_path_draw_threshold) {
                    // 正交模式下检测方向变化，插入直角拐角点
                    if (g_ortho_draw && g_current_drawing_path.size() >= 2) {
                        Vector3A prevPrevPt = g_current_drawing_path[g_current_drawing_path.size() - 2];
                        bool oldHorizontal = (fabsf(lastPt.Y - prevPrevPt.Y) < 0.01f);
                        bool newHorizontal = (fabsf(constrained.Y - lastPt.Y) < 0.01f);
                        if (oldHorizontal != newHorizontal) {
                            // 方向变化 → 在转折处插入直角拐角
                            Vector3A corner;
                            if (oldHorizontal) {
                                // 旧段水平、新段垂直 → 拐角在 (constrained.X, lastPt.Y)
                                corner = {constrained.X, lastPt.Y, lastPt.Z};
                            } else {
                                // 旧段垂直、新段水平 → 拐角在 (lastPt.X, constrained.Y)
                                corner = {lastPt.X, constrained.Y, lastPt.Z};
                            }
                            g_current_drawing_path.push_back(corner);
                        }
                    }
                    g_current_drawing_path.push_back(constrained);
                    g_path_start_pos = mouse;
                }
            }
        } else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (g_path_drawing_active && !g_current_drawing_path.empty()) {
                // 使用RDP简化（保留端点，仅去除共线中间点）
                std::vector<ImVec2> screenPts;
                for (auto& wp : g_current_drawing_path)
                    screenPts.push_back(ToMap(wp));

                float tolerance = g_smooth_strength;
                auto simplifiedScreen = SimplifyPathRDP(screenPts, tolerance);
                std::vector<Vector3A> simplifiedWorld;
                for (auto& sp : simplifiedScreen) {
                    float u = (sp.x - map_pos.x) / map_w;
                    float v = (sp.y - map_pos.y) / map_h;
                    float worldX = (u - cfg.offsetU) / cfg.scaleX;
                    float worldY = (v - cfg.offsetV) / cfg.scaleY;
                    simplifiedWorld.push_back(Vector3A(worldX, worldY, Z.Z));
                }
                g_current_drawing_path = simplifiedWorld;

                if (g_current_drawing_path.size() >= 2) {
                    g_pending_path = g_current_drawing_path;
                    g_pending_save_confirm = true;
                    g_pending_save_timeout = 30.0f;
                    AddNotification("路径绘制完成，请确认保存", 3.0f, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));
                } else {
                    AddNotification("路径太短，已丢弃", 2.0f, ImVec4(1.0f, 0.5f, 0.3f, 1.0f));
                }
                g_current_drawing_path.clear();
                g_path_edit_mode = 0;
            }
            g_path_drawing_active = false;
            g_press_timer = 0;
        }
    }

    // ========== 地图拖动（出口拖拽时禁用）==========
    static bool dragging_map = false;
    static ImVec2 drag_offset;
    // 出口拖拽优先：如果在拖拽出口，强制禁止地图拖动
    if (g_drag_exit_idx >= 0) {
        dragging_map = false;
    }
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hover_map && g_drag_point == 0 && g_path_edit_mode == 0
        && g_drag_exit_idx < 0) {
        dragging_map = true;
        drag_offset = ImVec2(mouse.x - map_pos.x, mouse.y - map_pos.y);
    }
    // 如果出口正在被拖拽，阻止地图拖动
    if (g_drag_exit_idx >= 0) dragging_map = false;
    if (dragging_map && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_map_pos_x = mouse.x - drag_offset.x;
        g_map_pos_y = mouse.y - drag_offset.y;
        // 地图拖动边界限制
        g_map_pos_x = std::clamp(g_map_pos_x, -map_w * 0.5f, displayInfo.width - map_w * 0.5f);
        g_map_pos_y = std::clamp(g_map_pos_y, -map_h * 0.5f, displayInfo.height - map_h * 0.5f);
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) dragging_map = false;

    // ========== 双指缩放 + 鼠标滚轮缩放（仅地图悬停区域生效）==========
    {
        static float g_pinch_last_dist = 0.0f;
        static bool  g_pinch_active  = false;

        auto ClampMapPos = [&]() {
            g_map_pos_x = std::clamp(g_map_pos_x, -map_w * 0.5f, displayInfo.width - map_w * 0.5f);
            g_map_pos_y = std::clamp(g_map_pos_y, -map_h * 0.5f, displayInfo.height - map_h * 0.5f);
        };

        if (hover_map) {
            int fc = Touch::GetFingerCount();
            if (fc >= 2) {
                float x1, y1, x2, y2;
                if (Touch::GetFinger(0, x1, y1) && Touch::GetFinger(1, x2, y2)) {
                    float dx = x1 - x2, dy = y1 - y2;
                    float dist = sqrtf(dx*dx + dy*dy);
                    if (!g_pinch_active) {
                        g_pinch_last_dist = dist;
                        g_pinch_active = true;
                    } else if (g_pinch_last_dist > 1.0f) {
                        float scale = dist / g_pinch_last_dist;
                        float newSz = g_map_display_size * scale;
                        newSz = std::clamp(newSz, 100.0f, 2500.0f);
                        float ratio = newSz / g_map_display_size;
                        float cx = (x1 + x2) * 0.5f, cy = (y1 + y2) * 0.5f;
                        g_map_pos_x = cx - (cx - g_map_pos_x) * ratio;
                        g_map_pos_y = cy - (cy - g_map_pos_y) * ratio;
                        g_map_display_size = newSz;
                        g_pinch_last_dist = dist;
                        ClampMapPos();
                    }
                }
            } else {
                g_pinch_active = false;
            }

            if (hover_map && ImGui::GetIO().MouseWheel != 0.0f) {
                float oldSz = g_map_display_size;
                float newSz = g_map_display_size * (1.0f + ImGui::GetIO().MouseWheel * 0.1f);
                newSz = std::clamp(newSz, 100.0f, 2500.0f);
                float ratio = newSz / oldSz;
                ImVec2 ms = ImGui::GetMousePos();
                g_map_pos_x = ms.x - (ms.x - g_map_pos_x) * ratio;
                g_map_pos_y = ms.y - (ms.y - g_map_pos_y) * ratio;
                g_map_display_size = newSz;
                ClampMapPos();
                ImGui::GetIO().MouseWheel = 0.0f;
            }
        } else {
            g_pinch_active = false;
        }
    }

    // ========== 缩放按钮 ==========
    ImVec2 zoom_btn_pos(map_end.x - 35.0f, map_pos.y + 10.0f);
    ImVec2 zoom_btn_size(35.0f, 35.0f);
    ImU32 zoom_btn_color = IM_COL32(255, 255, 255, 80);
    ImU32 zoom_text_color = IM_COL32(255, 255, 255, 200);

    Draw->AddRectFilled(zoom_btn_pos, ImVec2(zoom_btn_pos.x + zoom_btn_size.x, zoom_btn_pos.y + zoom_btn_size.y), zoom_btn_color, 5.0f);
    Draw->AddText(ImVec2(zoom_btn_pos.x + 8, zoom_btn_pos.y + 2), zoom_text_color, "+");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetMousePos().x >= zoom_btn_pos.x && ImGui::GetMousePos().x <= zoom_btn_pos.x + zoom_btn_size.x &&
        ImGui::GetMousePos().y >= zoom_btn_pos.y && ImGui::GetMousePos().y <= zoom_btn_pos.y + zoom_btn_size.y) {
        g_map_display_size = std::min(g_map_display_size + 20.0f, 1800.0f);
    }

    ImVec2 zoom_btn2_pos(map_end.x - 35.0f, map_pos.y + 55.0f);
    Draw->AddRectFilled(zoom_btn2_pos, ImVec2(zoom_btn2_pos.x + zoom_btn_size.x, zoom_btn2_pos.y + zoom_btn_size.y), zoom_btn_color, 5.0f);
    Draw->AddText(ImVec2(zoom_btn2_pos.x + 11, zoom_btn2_pos.y + 2), zoom_text_color, "-");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        ImGui::GetMousePos().x >= zoom_btn2_pos.x && ImGui::GetMousePos().x <= zoom_btn2_pos.x + zoom_btn_size.x &&
        ImGui::GetMousePos().y >= zoom_btn2_pos.y && ImGui::GetMousePos().y <= zoom_btn2_pos.y + zoom_btn_size.y) {
        g_map_display_size = std::max(g_map_display_size - 20.0f, 100.0f);
    }

    // ========== 坐标调试显示（实时显示鼠标位置和UV坐标） ==========
    if (hover_map && cfg.calibrated) {
        ImVec2 ms = ImGui::GetMousePos();
        float u_debug = (ms.x - map_pos.x) / map_w;
        float v_debug = (ms.y - map_pos.y) / map_h;
        float sx_d, sy_d, ou_d, ov_d;
        bool fx_d, fy_d;
        if (cfg.calibrated) {
            sx_d = cfg.scaleX; sy_d = cfg.scaleY;
            ou_d = cfg.offsetU; ov_d = cfg.offsetV;
            fx_d = cfg.flipX; fy_d = cfg.flipY;
        } else {
            sx_d = g_map_scale_x; sy_d = g_map_scale_y;
            ou_d = g_map_offset_u; ov_d = g_map_offset_v;
            fx_d = g_map_flip_x; fy_d = g_map_flip_y;
        }
        float u_raw = u_debug, v_raw = v_debug;
        if (fx_d) u_debug = 1.0f - u_debug;
        if (fy_d) v_debug = 1.0f - v_debug;
        float wx = (u_debug - ou_d) / sx_d;
        float wy = (v_debug - ov_d) / sy_d;
        char coord_buf[256];
        // 倒推出实际 UV（经过 ToMap round-trip）用于验证
        float u_check = wx * sx_d + ou_d;
        float v_check = wy * sy_d + ov_d;
        if (fx_d) u_check = 1.0f - u_check;
        if (fy_d) v_check = 1.0f - v_check;
        snprintf(coord_buf, sizeof(coord_buf),
            "鼠标: (%.0f, %.0f)  UV: (%.3f, %.3f)  世界: (%.1f, %.1f)",
            ms.x, ms.y, u_raw, v_raw, wx, wy);
        ImVec2 coordSize = ImGui::CalcTextSize(coord_buf);
        Draw->AddRectFilled(
            ImVec2(map_pos.x + 3, map_end.y - coordSize.y - 10),
            ImVec2(map_pos.x + coordSize.x + 12, map_end.y - 2),
            IM_COL32(0, 0, 0, 180), 4.0f);
        Draw->AddText(ImGui::GetFont(), 18.0f,
            ImVec2(map_pos.x + 6, map_end.y - coordSize.y - 7),
            IM_COL32(255, 255, 100, 240), coord_buf);
    }
}
