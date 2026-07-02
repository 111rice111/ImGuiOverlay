"""
ImGuiOverlay 服务器健康监控与诊断系统
========================================
功能:
  1. 实时指标采集 (CPU/内存/磁盘/网络/进程)
  2. 故障自动检测与分类
  3. 异常阈值告警
  4. 历史数据记录与趋势分析
  5. 结构化诊断报告生成

架构: 后台线程独立运行, 通过 shared state 与 server.py 通信
"""

import os, sys, time, json, sqlite3, threading, traceback
from datetime import datetime, timedelta
from collections import deque
from dataclasses import dataclass, field, asdict
from typing import Optional

# ─── 配置 ───────────────────────────────────────────
CHECK_INTERVAL   = 15          # 健康检查间隔(秒)
ALERT_COOLDOWN   = 300         # 同类告警冷却(秒)
HISTORY_RETENTION = 7200       # 历史数据保留(条), ~30小时
LOW_DISK_GB      = 1           # 磁盘低于此值告警(GB)
HIGH_CPU_PCT     = 85          # CPU 高于此值告警(%)
HIGH_MEM_PCT     = 90          # 内存高于此值告警(%)
RESP_TIMEOUT_SEC = 5           # API 响应超时(秒)
DB_PATH          = os.path.join(os.path.dirname(__file__), "health.db")

# ─── 数据结构 ───────────────────────────────────────
@dataclass
class SystemMetrics:
    """单次采集的系统指标"""
    timestamp:    str  = ""
    cpu_pct:      float = 0.0
    mem_pct:      float = 0.0
    mem_avail_mb: float = 0.0
    disk_free_gb: float = 0.0
    disk_pct:     float = 0.0
    net_rx_mb:    float = 0.0    # 累计接收
    net_tx_mb:    float = 0.0    # 累计发送
    api_latency_ms: float = 0.0
    api_ok:       bool  = True
    bore_alive:   bool  = True
    server_pid:   int   = 0

@dataclass
class Alert:
    """告警记录"""
    id:          int    = 0
    level:       str    = ""      # critical / warning / info
    category:    str    = ""      # cpu / memory / disk / network / api / process
    message:     str    = ""
    timestamp:   str    = ""
    resolved:    bool   = False
    resolved_at: str    = ""

@dataclass
class DiagnosticReport:
    """诊断报告"""
    ok:           bool   = True
    timestamp:    str    = ""
    summary:      str    = ""
    checks:       list   = field(default_factory=list)
    alerts:       list   = field(default_factory=list)
    suggestions:  list   = field(default_factory=list)
    metrics:      dict   = field(default_factory=dict)

# ─── 全局状态 (线程安全) ─────────────────────────────
_state_lock      = threading.Lock()
_current_metrics = SystemMetrics()
_alert_history   = deque(maxlen=200)
_diag_history    = deque(maxlen=500)
_last_alert_time = {}             # category → timestamp, 防刷屏
_announcements   = []             # [{id, content, created_at, active}]
_force_version   = "0.0.0"       # 强制最低版本, 0.0.0=不强制
_db_conn         = None


def _get_db():
    """获取/初始化 SQLite 连接"""
    global _db_conn
    if _db_conn is None:
        _db_conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        _db_conn.execute("""
            CREATE TABLE IF NOT EXISTS metrics_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                ts TEXT, cpu REAL, mem REAL, disk_free REAL, disk_pct REAL,
                api_ms REAL, api_ok INTEGER, bore_ok INTEGER, server_pid INTEGER
            )
        """)
        _db_conn.execute("""
            CREATE TABLE IF NOT EXISTS alerts_log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                level TEXT, category TEXT, message TEXT,
                ts TEXT, resolved INTEGER DEFAULT 0, resolved_at TEXT
            )
        """)
        _db_conn.execute("""
            CREATE TABLE IF NOT EXISTS announcements (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                content TEXT NOT NULL,
                created_at TEXT DEFAULT (datetime('now','localtime')),
                active INTEGER DEFAULT 1
            )
        """)
        _db_conn.execute("""
            CREATE TABLE IF NOT EXISTS config (
                key TEXT PRIMARY KEY, value TEXT
            )
        """)
        _db_conn.execute("INSERT OR IGNORE INTO config VALUES ('force_version', '0.0.0')")
        _db_conn.commit()
    return _db_conn


# ─── 指标采集 ───────────────────────────────────────
def _collect_system_metrics(port: int = 8080) -> SystemMetrics:
    """采集系统实时指标 (Windows 兼容)"""
    m = SystemMetrics(timestamp=datetime.now().isoformat())

    # CPU (近似: 通过 psutil 或 WMIC)
    try:
        import psutil
        m.cpu_pct = psutil.cpu_percent(interval=1)
        mem = psutil.virtual_memory()
        m.mem_pct = mem.percent
        m.mem_avail_mb = mem.available / 1048576
        disk = psutil.disk_usage(os.path.dirname(__file__))
        m.disk_free_gb = disk.free / 1073741824
        m.disk_pct = disk.percent
        net = psutil.net_io_counters()
        m.net_rx_mb = net.bytes_recv / 1048576
        m.net_tx_mb = net.bytes_sent / 1048576
    except ImportError:
        # fallback: 无 psutil, 用 0 填充
        pass

    # API 响应检测 (本地)
    import urllib.request
    try:
        t0 = time.time()
        req = urllib.request.Request(f"http://127.0.0.1:{port}/api/status", method="GET")
        resp = urllib.request.urlopen(req, timeout=RESP_TIMEOUT_SEC)
        m.api_latency_ms = (time.time() - t0) * 1000
        m.api_ok = (resp.status == 200)
    except Exception:
        m.api_ok = False
        m.api_latency_ms = 9999

    # Bore 隧道检测
    try:
        t0 = time.time()
        req = urllib.request.Request("http://bore.pub:3699/api/status", method="GET")
        urllib.request.urlopen(req, timeout=RESP_TIMEOUT_SEC)
        m.bore_alive = True
    except Exception:
        m.bore_alive = False

    return m


# ─── 故障检测与诊断 ────────────────────────────────
def diagnose(m: SystemMetrics, port: int = 8080) -> DiagnosticReport:
    """根据指标生成诊断报告"""
    report = DiagnosticReport(timestamp=m.timestamp, metrics=asdict(m))
    checks = []
    alerts = []
    suggestions = []

    # --- 1. API 服务检测 ---
    if not m.api_ok:
        report.ok = False
        checks.append({"name": "API 服务", "status": "FAIL", "detail": "本地 API 无响应"})
        alerts.append({"level": "critical", "category": "api",
                       "message": "后端服务崩溃或无响应"})
        suggestions.append("1. 检查 server.py 进程: tasklist | findstr python")
        suggestions.append("2. 尝试重启: python server.py")
        suggestions.append("3. 检查端口占用: netstat -ano | findstr :8080")
    else:
        latency = m.api_latency_ms
        status = "WARN" if latency > 2000 else ("SLOW" if latency > 1000 else "OK")
        checks.append({"name": "API 延迟", "status": status, "detail": f"{latency:.0f}ms"})
        if latency > 2000:
            alerts.append({"level": "warning", "category": "api",
                           "message": f"API 响应延迟过高: {latency:.0f}ms"})
            suggestions.append("检查服务器负载, 可能存在性能瓶颈")

    # --- 2. 公网隧道检测 ---
    if not m.bore_alive:
        report.ok = False
        checks.append({"name": "公网隧道", "status": "FAIL", "detail": "bore.pub:3699 不可达"})
        alerts.append({"level": "critical", "category": "network",
                       "message": "bore 隧道断开, 公网无法访问"})
        suggestions.append("1. 检查 bore.exe 进程")
        suggestions.append("2. 重启隧道: bore.exe local 8080 --to bore.pub --port 3699")
    else:
        checks.append({"name": "公网隧道", "status": "OK", "detail": "bore.pub:3699 可达"})

    # --- 3. 磁盘空间 ---
    if m.disk_free_gb > 0 and m.disk_free_gb < LOW_DISK_GB:
        report.ok = False
        checks.append({"name": "磁盘空间", "status": "FAIL",
                       "detail": f"仅剩 {m.disk_free_gb:.1f}GB"})
        alerts.append({"level": "critical", "category": "disk",
                       "message": f"磁盘空间不足: {m.disk_free_gb:.1f}GB"})
        suggestions.append("清理日志文件和临时文件")
        suggestions.append(f"检查 {os.path.dirname(__file__)} 目录空间占用")
    elif m.disk_free_gb > 0:
        checks.append({"name": "磁盘空间", "status": "OK",
                       "detail": f"{m.disk_free_gb:.1f}GB 可用"})

    # --- 4. CPU ---
    if m.cpu_pct > 0:
        if m.cpu_pct > HIGH_CPU_PCT:
            checks.append({"name": "CPU 使用率", "status": "WARN",
                           "detail": f"{m.cpu_pct:.0f}%"})
            alerts.append({"level": "warning", "category": "cpu",
                           "message": f"CPU 使用率过高: {m.cpu_pct:.0f}%"})
            suggestions.append("检查是否有异常进程占用 CPU")
        else:
            checks.append({"name": "CPU 使用率", "status": "OK",
                           "detail": f"{m.cpu_pct:.0f}%"})

    # --- 5. 内存 ---
    if m.mem_pct > HIGH_MEM_PCT:
        checks.append({"name": "内存使用率", "status": "WARN",
                       "detail": f"{m.mem_pct:.0f}%"})
        alerts.append({"level": "warning", "category": "memory",
                       "message": f"内存使用率过高: {m.mem_pct:.0f}%"})

    # --- 6. 数据库 ---
    try:
        db = _get_db()
        db.execute("SELECT 1").fetchone()
        checks.append({"name": "数据库连接", "status": "OK", "detail": "SQLite 正常"})
    except Exception as e:
        report.ok = False
        checks.append({"name": "数据库连接", "status": "FAIL", "detail": str(e)})
        alerts.append({"level": "critical", "category": "process",
                       "message": f"数据库异常: {e}"})
        suggestions.append("检查 overlay.db 文件权限和完整性")

    # --- 汇总 ---
    if report.ok:
        report.summary = "所有系统指标正常"
    else:
        fail_count = sum(1 for c in checks if c["status"] in ("FAIL", "WARN"))
        report.summary = f"发现 {fail_count} 个异常, 需立即处理"

    report.checks = checks
    report.alerts = alerts
    report.suggestions = suggestions
    return report


# ─── 告警管理 ───────────────────────────────────────
def _maybe_alert(level: str, category: str, message: str, force: bool = False):
    """去重告警: 同类告警 ALERT_COOLDOWN 秒内不重复"""
    now = time.time()
    key = f"{category}_{level}"
    last = _last_alert_time.get(key, 0)
    if not force and (now - last) < ALERT_COOLDOWN:
        return
    _last_alert_time[key] = now

    alert = Alert(level=level, category=category, message=message,
                  timestamp=datetime.now().isoformat())
    _alert_history.append(alert)
    _diag_history.append({"type": "alert", "data": asdict(alert)})

    # 持久化
    try:
        db = _get_db()
        db.execute("INSERT INTO alerts_log (level, category, message, ts) VALUES (?,?,?,?)",
                   (level, category, message, alert.timestamp))
        db.commit()
    except Exception:
        pass

    # 控制台输出
    icon = {"critical": "🔴", "warning": "🟡", "info": "🔵"}.get(level, "⚪")
    print(f"{icon} [{category.upper()}] {message}")


# ─── 后台监控线程 ───────────────────────────────────
def _monitor_loop(port: int):
    """持续监控循环"""
    print(f"[Monitor] 健康监控已启动 (间隔{CHECK_INTERVAL}s)")
    last_metrics_save = 0

    while True:
        try:
            m = _collect_system_metrics(port)
            with _state_lock:
                global _current_metrics
                _current_metrics = m

            report = diagnose(m, port)

            # 持久化指标 (每 60 秒)
            now = time.time()
            if now - last_metrics_save > 60:
                try:
                    db = _get_db()
                    db.execute("INSERT INTO metrics_log (ts,cpu,mem,disk_free,disk_pct,api_ms,api_ok,bore_ok,server_pid) "
                               "VALUES (?,?,?,?,?,?,?,?,?)",
                               (m.timestamp, m.cpu_pct, m.mem_pct, m.disk_free_gb,
                                m.disk_pct, m.api_latency_ms, int(m.api_ok), int(m.bore_alive), 0))
                    # 清理旧数据
                    db.execute("DELETE FROM metrics_log WHERE id NOT IN "
                               "(SELECT id FROM metrics_log ORDER BY id DESC LIMIT ?)",
                               (HISTORY_RETENTION,))
                    db.commit()
                    last_metrics_save = now
                except Exception:
                    pass

            # 触发告警
            for a in report.alerts:
                _maybe_alert(a["level"], a["category"], a["message"])

            _diag_history.append({"type": "report", "data": asdict(report)})

            # 自动恢复: bore 隧道丢了就尝试重连
            if not m.bore_alive:
                _maybe_alert("warning", "network", "bore 隧道断开, 尝试自动恢复...")
                # 不做自动重启（可能造成循环），只告警

        except Exception as e:
            print(f"[Monitor] 异常: {e}")

        time.sleep(CHECK_INTERVAL)


# ─── 公开 API ───────────────────────────────────────
def start_monitor(port: int = 8080):
    """启动后台监控线程"""
    t = threading.Thread(target=_monitor_loop, args=(port,), daemon=True, name="HealthMonitor")
    t.start()
    return t


def get_current_metrics() -> dict:
    """获取最新指标快照"""
    with _state_lock:
        return asdict(_current_metrics)


def get_recent_diagnostics(limit: int = 20) -> list:
    """获取最近诊断历史"""
    items = list(_diag_history)[-limit:]
    return items


def get_alert_history(limit: int = 50) -> list:
    """获取告警历史 (从数据库)"""
    try:
        db = _get_db()
        rows = db.execute(
            "SELECT id, level, category, message, ts, resolved, resolved_at "
            "FROM alerts_log ORDER BY id DESC LIMIT ?", (limit,)
        ).fetchall()
        return [{"id": r[0], "level": r[1], "category": r[2],
                 "message": r[3], "ts": r[4], "resolved": r[5], "resolved_at": r[6]}
                for r in rows]
    except Exception:
        return []


def get_metrics_history(hours: int = 1) -> list:
    """获取指标历史 (用于趋势图)"""
    try:
        db = _get_db()
        cutoff = (datetime.now() - timedelta(hours=hours)).isoformat()
        rows = db.execute(
            "SELECT ts, cpu, mem, disk_free, api_ms, api_ok, bore_ok "
            "FROM metrics_log WHERE ts > ? ORDER BY id ASC", (cutoff,)
        ).fetchall()
        return [{"ts": r[0], "cpu": r[1], "mem": r[2], "disk_free": r[3],
                 "api_ms": r[4], "api_ok": r[5], "bore_ok": r[6]} for r in rows]
    except Exception:
        return []


def get_announcements() -> list:
    """获取活跃公告"""
    try:
        db = _get_db()
        rows = db.execute(
            "SELECT id, content, created_at FROM announcements WHERE active=1 ORDER BY id DESC"
        ).fetchall()
        return [{"id": r[0], "content": r[1], "created_at": r[2]} for r in rows]
    except Exception:
        return []


def add_announcement(content: str) -> bool:
    """发布公告"""
    try:
        db = _get_db()
        db.execute("INSERT INTO announcements (content) VALUES (?)", (content,))
        db.commit()
        return True
    except Exception:
        return False


def del_announcement(ann_id: int) -> bool:
    """删除/停用公告"""
    try:
        db = _get_db()
        db.execute("UPDATE announcements SET active=0 WHERE id=?", (ann_id,))
        db.commit()
        return True
    except Exception:
        return False


def get_force_version() -> str:
    """获取强制最低版本"""
    try:
        db = _get_db()
        row = db.execute("SELECT value FROM config WHERE key='force_version'").fetchone()
        return row[0] if row else "0.0.0"
    except Exception:
        return "0.0.0"


def set_force_version(ver: str) -> bool:
    """设置强制最低版本"""
    try:
        db = _get_db()
        db.execute("INSERT OR REPLACE INTO config (key, value) VALUES ('force_version', ?)", (ver,))
        db.commit()
        with _state_lock:
            global _force_version
            _force_version = ver
        return True
    except Exception:
        return False


def run_full_diagnostic(port: int = 8080) -> dict:
    """运行完整诊断并返回结构化报告"""
    m = _collect_system_metrics(port)
    report = diagnose(m, port)
    return asdict(report)


# ─── 手动排查指引 ───────────────────────────────────
MANUAL_TROUBLESHOOTING = """
## 服务器手动排查指引

### 1. 服务是否在运行?
   tasklist | findstr python
   预期: 看到 python.exe 进程

### 2. 端口是否监听?
   netstat -ano | findstr :8080
   预期: LISTENING 状态

### 3. 本地 API 是否响应?
   curl http://127.0.0.1:8080/api/status
   预期: 返回 JSON

### 4. 公网隧道是否连通?
   curl http://bore.pub:3699/api/status
   预期: 返回 JSON (与第3步结果一致)

### 5. 数据库是否正常?
   检查 backend/overlay.db 文件存在且可读写

### 6. 防火墙是否拦截?
   - Windows 防火墙 → 允许 Python 入站
   - bore.pub 是否可达: ping bore.pub

### 7. 常见故障恢复:
   # 重启服务
   taskkill /F /IM python.exe
   cd backend && python server.py

   # 重启隧道
   taskkill /F /IM bore.exe
   bore.exe local 8080 --to bore.pub --port 3699
"""
