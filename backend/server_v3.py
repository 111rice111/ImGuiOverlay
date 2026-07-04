"""
ImGuiOverlay 卡密验证系统 v3.0 — 腾讯云部署版
=================================================
新功能: 多时长卡密 | 公用卡(不绑定设备) | 公告系统 | 强制更新 | 统计看板 | 管理登录

部署:
  python server_v3.py            # 监听 0.0.0.0:8080
  python server_v3.py --port 80  # 自定义端口

API 设计:
  客户端:   POST /api/verify (XOR-CBC)  /api/heartbeat /api/check
  管理后台: POST /api/admin/* (明文JSON) + Web UI
  公开:     GET  /api/status /api/ann/active /api/update/check
"""

import sqlite3, json, time, secrets, hashlib, struct, os, sys, argparse
from typing import Optional, Union
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime, timedelta
from urllib.parse import parse_qs, urlparse

# ========== 配置 ==========
SERVER_PORT   = 8080
DB_PATH       = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overlay_v3.db")
ADMIN_SESSION_TIMEOUT = 3600  # 管理后台会话 1 小时
APP_NAME      = "ImGuiOverlay"

# ========== XOR-CBC 加密 (与客户端 net_client.h crypto.h 同步) ==========
XOR_KEY = b'ImGuiOverlay2026'
XOR_IV  = b'InitVector123456'

def xor_encrypt(plain: bytes) -> str:
    key, iv = XOR_KEY, XOR_IV
    result = bytearray()
    prev = iv
    for i in range(0, len(plain), 16):
        block = plain[i:i+16]
        if len(block) < 16:
            block = block + bytes([16 - len(block)]) * (16 - len(block))
        enc = bytes(b ^ key[j % 16] ^ prev[j % 16] for j, b in enumerate(block))
        result.extend(enc)
        prev = enc
    return result.hex().upper()

def xor_decrypt(hex_str: str) -> bytes:
    data = bytes.fromhex(hex_str)
    key, iv = XOR_KEY, XOR_IV
    result = bytearray()
    prev = iv
    for i in range(0, len(data), 16):
        block = data[i:i+16]
        dec = bytes(b ^ key[j % 16] ^ prev[j % 16] for j, b in enumerate(block))
        result.extend(dec)
        prev = block
    pad = result[-1]
    if 1 <= pad <= 16:
        result = result[:-pad]
    return bytes(result)

# ========== 密码工具 ==========
def hash_password(pw: str) -> str:
    return hashlib.sha256(pw.encode()).hexdigest()

def admin_sessions():
    """线程不安全的简单会话管理 {token: (username, expire_ts)}"""
    return {}

# ========== 时长计算 ==========
DURATION_MAP = {
    "hour":     {"label": "小时", "unit": "hours"},
    "day":      {"label": "天",   "unit": "days"},
    "week":     {"label": "周",   "unit": "weeks"},
    "month":    {"label": "月",   "unit": "months"},
    "year":     {"label": "年",   "unit": "years"},
    "permanent":{"label": "永久", "unit": None},
}

PRESET_DURATIONS = [
    # (type, value, label)
    ("hour",      1,  "1小时"),
    ("hour",      6,  "6小时"),
    ("hour",      12, "12小时"),
    ("day",       1,  "1天"),
    ("day",       3,  "3天"),
    ("day",       7,  "7天"),
    ("week",      1,  "1周"),
    ("month",     1,  "1个月"),
    ("month",     3,  "3个月"),
    ("month",     6,  "6个月"),
    ("year",      1,  "1年"),
    ("permanent", 0,  "永久"),
]

def calc_expire(duration_type: str, duration_value: int) -> Optional[str]:
    """计算到期时间字符串, None 表示永久"""
    if duration_type == "permanent" or duration_value <= 0:
        return None
    now = datetime.now()
    kwargs = {DURATION_MAP[duration_type]["unit"]: duration_value}
    expire = now + timedelta(**kwargs)
    return expire.strftime("%Y-%m-%d %H:%M:%S")

def is_expired(expire_str: Optional[str]) -> bool:
    if not expire_str:
        return False
    try:
        return datetime.strptime(expire_str, "%Y-%m-%d %H:%M:%S") < datetime.now()
    except:
        return True

# ========== 数据库初始化 ==========
def init_db() -> sqlite3.Connection:
    db = sqlite3.connect(DB_PATH)
    db.row_factory = sqlite3.Row
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA foreign_keys=ON")

    # ===== 卡密表 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS cards (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            card_key        TEXT NOT NULL UNIQUE,
            prefix          TEXT DEFAULT '',
            card_type       TEXT DEFAULT 'device',
            device_id       TEXT DEFAULT NULL,
            hwid            TEXT DEFAULT NULL,
            token           TEXT DEFAULT NULL,
            banned          INTEGER DEFAULT 0,
            active          INTEGER DEFAULT 1,
            duration_type   TEXT DEFAULT 'permanent',
            duration_value  INTEGER DEFAULT 0,
            duration_label  TEXT DEFAULT '永久',
            expire_at       TEXT DEFAULT NULL,
            activated_at    TEXT DEFAULT NULL,
            last_heart      TEXT DEFAULT NULL,
            created_at      TEXT DEFAULT (datetime('now','localtime')),
            created_by      TEXT DEFAULT 'admin',
            max_concurrent  INTEGER DEFAULT 1,
            concurrent_count INTEGER DEFAULT 0,
            first_user_at   TEXT DEFAULT NULL,
            last_user_at    TEXT DEFAULT NULL,
            total_users     INTEGER DEFAULT 0
        )
    """)

    # ===== 公用卡会话 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS public_sessions (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            card_id         INTEGER NOT NULL,
            device_id       TEXT NOT NULL,
            device_name     TEXT DEFAULT '',
            token           TEXT DEFAULT NULL,
            ip              TEXT DEFAULT NULL,
            started_at      TEXT DEFAULT (datetime('now','localtime')),
            last_heart      TEXT DEFAULT NULL,
            heartbeat_count INTEGER DEFAULT 0,
            active          INTEGER DEFAULT 1,
            FOREIGN KEY (card_id) REFERENCES cards(id)
        )
    """)
    db.execute("CREATE INDEX IF NOT EXISTS idx_ps_card ON public_sessions(card_id, active)")

    # ===== 公告表 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS announcements (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            title           TEXT DEFAULT '',
            content         TEXT NOT NULL,
            priority        INTEGER DEFAULT 0,
            mode            TEXT DEFAULT 'banner',
            start_time      TEXT DEFAULT NULL,
            end_time        TEXT DEFAULT NULL,
            active          INTEGER DEFAULT 1,
            dismissable     INTEGER DEFAULT 1,
            sort_order      INTEGER DEFAULT 0,
            created_at      TEXT DEFAULT (datetime('now','localtime')),
            updated_at      TEXT DEFAULT (datetime('now','localtime'))
        )
    """)

    # ===== 公告已读 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS announcement_reads (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            ann_id          INTEGER NOT NULL,
            device_id       TEXT NOT NULL,
            dismissed       INTEGER DEFAULT 1,
            read_at         TEXT DEFAULT (datetime('now','localtime')),
            UNIQUE(ann_id, device_id),
            FOREIGN KEY (ann_id) REFERENCES announcements(id)
        )
    """)

    # ===== 操作日志 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS logs (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            card_id         INTEGER DEFAULT NULL,
            card_key        TEXT DEFAULT NULL,
            device_id       TEXT DEFAULT NULL,
            card_type       TEXT DEFAULT NULL,
            action          TEXT NOT NULL,
            detail          TEXT DEFAULT NULL,
            ip              TEXT DEFAULT NULL,
            created_at      TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    db.execute("CREATE INDEX IF NOT EXISTS idx_logs_card ON logs(card_id)")
    db.execute("CREATE INDEX IF NOT EXISTS idx_logs_time ON logs(created_at)")

    # ===== 管理员 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS admins (
            id              INTEGER PRIMARY KEY AUTOINCREMENT,
            username        TEXT NOT NULL UNIQUE,
            password_hash   TEXT NOT NULL,
            role            TEXT DEFAULT 'admin',
            last_login      TEXT DEFAULT NULL,
            login_ip        TEXT DEFAULT NULL,
            active          INTEGER DEFAULT 1,
            created_at      TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    # 默认密码: admin / admin888
    default_hash = hash_password("admin888")
    db.execute("INSERT OR IGNORE INTO admins (username, password_hash, role) VALUES (?,?,?)",
               ("admin", default_hash, "superadmin"))

    # ===== 配置表 =====
    db.execute("""
        CREATE TABLE IF NOT EXISTS config (
            key             TEXT PRIMARY KEY,
            value           TEXT NOT NULL,
            updated_at      TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    defaults = [
        ("force_min_version", "0"),
        ("latest_version", "0"),
        ("version_name", ""),
        ("download_url", ""),
        ("update_notes", ""),
        ("app_name", APP_NAME),
    ]
    for k, v in defaults:
        db.execute("INSERT OR IGNORE INTO config (key, value) VALUES (?,?)", (k, v))

    db.commit()
    return db

# ========== HTTP 服务器 ==========
class APIHandler(BaseHTTPRequestHandler):
    db = None
    _sessions = admin_sessions()

    @classmethod
    def ensure_db(cls):
        if cls.db is None:
            cls.db = init_db()
        return cls.db

    def log_message(self, fmt, *args):
        ts = datetime.now().strftime("%H:%M:%S")
        print(f"[{ts}] {args[0] if args else fmt}")

    def _send(self, status=200, content_type="application/json", body=""):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Session-Token")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.wfile.write(body)

    def _json(self, data: dict, status=200):
        self._send(status, "application/json; charset=utf-8", json.dumps(data, ensure_ascii=False))

    def _html(self, body: str):
        self._send(200, "text/html; charset=utf-8", body)

    def _respond_enc(self, ok: bool, msg: str, **extra):
        """客户端加密响应"""
        resp = json.dumps({"ok": ok, "msg": msg, **extra}, ensure_ascii=False)
        enc = xor_encrypt(resp.encode())
        self._send(200, "text/plain", enc)

    def _get_config(self, key: str, default=""):
        row = self.db.execute("SELECT value FROM config WHERE key=?", (key,)).fetchone()
        return row["value"] if row else default

    def _set_config(self, key: str, value: str):
        self.db.execute("INSERT OR REPLACE INTO config (key, value, updated_at) VALUES (?,?,datetime('now','localtime'))",
                        (key, str(value)))
        self.db.commit()

    def _check_admin_auth(self) -> tuple[bool, str]:
        """验证管理后台登录状态, 返回 (是否已登录, 用户名)"""
        # 支持两种方式: Cookie 或 Header (方便 API 调用)
        token = None
        # 1. Cookie
        cookie_header = self.headers.get("Cookie", "")
        for part in cookie_header.split(";"):
            part = part.strip()
            if part.startswith("session="):
                token = part[8:]
                break
        # 2. Header
        if not token:
            token = self.headers.get("X-Session-Token", "")

        if token and token in self._sessions:
            username, expire_ts = self._sessions[token]
            if time.time() < expire_ts:
                return True, username
            else:
                del self._sessions[token]
        return False, ""

    # ─────────────────── GET 路由 ───────────────────
    def do_GET(self):
        path = urlparse(self.path).path.rstrip("/")
        self.db = self.ensure_db()

        # OPTIONS 预检
        if self.command == "OPTIONS":
            return self._send(204)

        # ── Web 页面 ──
        if path == "/admin":
            return self._html(ADMIN_HTML)
        if path == "/login":
            return self._html(LOGIN_HTML)

        # ── 公开 API ──
        if path == "/api/status":
            return self._json({"ok": True, "msg": "running", "version": "3.0", "server_time": datetime.now().isoformat()})

        if path == "/api/ann/active":
            # 客户端拉取有效公告
            device_id = parse_qs(urlparse(self.path).query).get("device_id", [""])[0]
            return self._get_active_announcements(device_id)

        if path == "/api/update/check":
            # 客户端检查更新
            return self._get_update_check()

        # ── 管理后台 API (需登录) ──
        if path.startswith("/api/admin"):
            if path == "/api/admin/list":
                return self._admin_list_cards()
            if path == "/api/admin/stats":
                return self._admin_get_stats()
            if path == "/api/admin/logs":
                return self._admin_get_logs()
            return self._json({"ok": False, "msg": "未知API路径"}, 404)

        # ── 公告 GET ──
        if path == "/api/ann/list":
            return self._ann_list()

        self._json({"ok": False, "msg": "not found"}, 404)

    # ─────────────────── POST 路由 ───────────────────
    def do_POST(self):
        path = urlparse(self.path).path.rstrip("/")
        self.db = self.ensure_db()

        length = int(self.headers.get("Content-Length", 0))
        raw_body = self.rfile.read(length) if length > 0 else b""

        # ── 客户端 API (XOR-CBC 加密) ──
        if path in ("/api/verify", "/api/heartbeat", "/api/check"):
            return self._handle_client_api(path, raw_body)

        # ── 管理后台 API (明文 JSON) ──
        if path.startswith("/api/admin"):
            return self._handle_admin_api(path, raw_body)

        # ── 公告管理 ──
        if path.startswith("/api/ann"):
            return self._handle_ann_api(path, raw_body)

        # ── 更新管理 ──
        if path.startswith("/api/update"):
            return self._handle_update_api(path, raw_body)

        self._json({"ok": False, "msg": "not found"}, 404)

    def do_OPTIONS(self):
        self._send(204)

    # ================================================================
    #  客户端 API
    # ================================================================
    def _handle_client_api(self, path, raw_body):
        """处理加密客户端请求"""
        try:
            body_str = raw_body.decode("utf-8") if raw_body else "{}"
            if raw_body:
                plain = xor_decrypt(body_str)
                data = json.loads(plain)
            else:
                data = {}
        except Exception:
            return self._respond_enc(False, "解密失败")

        db = self.db
        ip = self.client_address[0]

        # ── 验证 ──
        if path == "/api/verify":
            key = data.get("key", "").strip()
            dev = data.get("device_id", "").strip()

            if not key:
                return self._respond_enc(False, "缺少卡密")

            row = db.execute("SELECT * FROM cards WHERE card_key=? AND active=1", (key,)).fetchone()
            if not row:
                return self._respond_enc(False, "卡密不存在或已禁用")

            if row["banned"]:
                return self._respond_enc(False, "卡密已封禁")

            # 检查到期
            if is_expired(row["expire_at"]):
                return self._respond_enc(False, "卡密已过期")

            card_type = row["card_type"]
            card_id = row["id"]
            token = secrets.token_hex(16)
            now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            if card_type == "public":
                # ── 公用卡验证 ──
                # 检查并发限制
                max_c = row["max_concurrent"] or 0
                if max_c > 0:
                    current = db.execute(
                        "SELECT COUNT(*) as cnt FROM public_sessions WHERE card_id=? AND active=1", (card_id,)
                    ).fetchone()["cnt"]
                    if current >= max_c:
                        return self._respond_enc(False, f"卡密并发数已达上限 ({max_c})")

                # 检查是否已有此设备的活跃会话 (复用)
                existing = db.execute(
                    "SELECT id, token FROM public_sessions WHERE card_id=? AND device_id=? AND active=1",
                    (card_id, dev)
                ).fetchone()

                if existing:
                    # 复用现有会话
                    db.execute("UPDATE public_sessions SET last_heart=?, heartbeat_count=heartbeat_count+1 WHERE id=?",
                               (now_str, existing["id"]))
                    token = existing["token"]
                else:
                    # 创建新会话
                    db.execute("""
                        INSERT INTO public_sessions (card_id, device_id, token, ip, last_heart)
                        VALUES (?,?,?,?,?)
                    """, (card_id, dev, token, ip, now_str))
                    # 更新卡密统计
                    db.execute("""
                        UPDATE cards SET
                            concurrent_count = (SELECT COUNT(*) FROM public_sessions WHERE card_id=? AND active=1),
                            total_users = total_users + 1,
                            first_user_at = COALESCE(first_user_at, ?),
                            last_user_at = ?
                        WHERE id=?
                    """, (card_id, now_str, now_str, card_id))

                expire = row["expire_at"] or "永久"
                db.execute("INSERT INTO logs (card_id,card_key,device_id,card_type,action,detail,ip) VALUES (?,?,?,?,?,?,?)",
                           (card_id, row["card_key"][:16], dev, "public", "login_new" if not existing else "login_reuse",
                            f"token={token[:8]}", ip))
                db.commit()

                duration_label = row["duration_label"]
                print(f"[LOGIN] public card={key[:16]}... dev={dev[:16]}... ({duration_label})")
                return self._respond_enc(True, "ok", token=token, expire=expire,
                                         card_type="public", duration=row["duration_label"])

            else:
                # ── 设备绑定卡验证 ──
                if row["device_id"] and row["device_id"] != dev:
                    return self._respond_enc(False, "卡密已绑定其他设备")

                if not row["device_id"]:
                    # 首次激活 — 绑定设备
                    expire_at = calc_expire(row["duration_type"], row["duration_value"])
                    db.execute("""
                        UPDATE cards SET device_id=?, hwid=?, activated_at=?, expire_at=COALESCE(?,expire_at) WHERE id=?
                    """, (dev, dev, now_str, expire_at, card_id))
                    # 如果有时长, 现在才计算到期
                    if expire_at:
                        expire = expire_at
                    else:
                        expire = "永久"

                db.execute("UPDATE cards SET token=?, last_heart=? WHERE id=?", (token, now_str, card_id))

                db.execute("INSERT INTO logs (card_id,card_key,device_id,card_type,action,detail,ip) VALUES (?,?,?,?,?,?,?)",
                           (card_id, row["card_key"][:16], dev, "device", "login",
                            f"token={token[:8]}", ip))
                db.commit()

                expire = row["expire_at"] or "永久"
                duration_label = row["duration_label"]
                print(f"[LOGIN] device card={key[:16]}... dev={dev[:16]}... ({duration_label})")
                return self._respond_enc(True, "ok", token=token, expire=expire,
                                         card_type="device", duration=row["duration_label"])

        # ── 心跳 ──
        elif path == "/api/heartbeat":
            dev = data.get("device_id", "")
            tok = data.get("token", "")
            card_type = data.get("card_type", "device")
            now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

            if card_type == "public":
                # 公用卡心跳 — 更新 public_sessions
                ps = db.execute(
                    "SELECT ps.*, c.banned, c.active, c.expire_at FROM public_sessions ps JOIN cards c ON ps.card_id=c.id WHERE ps.device_id=? AND ps.token=? AND ps.active=1",
                    (dev, tok)
                ).fetchone()
                if not ps:
                    # 尝试只按 token 找
                    ps = db.execute(
                        "SELECT ps.*, c.banned, c.active, c.expire_at FROM public_sessions ps JOIN cards c ON ps.card_id=c.id WHERE ps.token=? AND ps.active=1",
                        (tok,)
                    ).fetchone()

                if not ps:
                    return self._respond_enc(False, "invalid", banned=True)

                if ps["banned"] or not ps["active"]:
                    return self._respond_enc(False, "banned", banned=True)

                if is_expired(ps["expire_at"]):
                    db.execute("UPDATE public_sessions SET active=0 WHERE id=?", (ps["id"],))
                    db.commit()
                    return self._respond_enc(False, "卡密已过期", banned=True)

                db.execute("UPDATE public_sessions SET last_heart=?, heartbeat_count=heartbeat_count+1 WHERE id=?",
                           (now_str, ps["id"]))
                db.commit()
                return self._respond_enc(True, "ok")

            else:
                # 设备卡心跳
                row = db.execute("SELECT * FROM cards WHERE device_id=? AND token=? AND banned=0 AND active=1",
                                 (dev, tok)).fetchone()
                if not row:
                    return self._respond_enc(False, "invalid", banned=True)

                if is_expired(row["expire_at"]):
                    return self._respond_enc(False, "卡密已过期", banned=True)

                db.execute("UPDATE cards SET last_heart=? WHERE id=?", (now_str, row["id"]))
                db.commit()
                return self._respond_enc(True, "ok")

        # ── 综合检查 (更新+公告) ──
        elif path == "/api/check":
            # 返回版本信息和公告
            force_ver = self._get_config("force_min_version", "0")
            latest_ver = self._get_config("latest_version", "0")
            ver_name = self._get_config("version_name", "")
            dl_url = self._get_config("download_url", "")
            update_notes = self._get_config("update_notes", "")
            device_id = data.get("device_id", "")

            # 获取有效公告
            anns = self._get_anns_for_device(device_id)

            return self._respond_enc(True, "ok",
                                     force_min_version=force_ver,
                                     latest_version=latest_ver,
                                     version_name=ver_name,
                                     download_url=dl_url,
                                     update_notes=update_notes,
                                     server_time=datetime.now().isoformat(),
                                     announcements=anns)

    # ================================================================
    #  公告系统
    # ================================================================
    def _get_anns_for_device(self, device_id: str) -> list:
        """获取设备可见的公告列表 (排除已关闭的)"""
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        rows = self.db.execute("""
            SELECT a.* FROM announcements a
            WHERE a.active = 1
              AND (a.start_time IS NULL OR a.start_time <= ?)
              AND (a.end_time IS NULL OR a.end_time >= ?)
            ORDER BY a.priority DESC, a.sort_order DESC, a.id DESC
        """, (now, now)).fetchall()

        result = []
        for row in rows:
            # 检查是否已关闭
            is_read = False
            if device_id:
                read = self.db.execute(
                    "SELECT dismissed FROM announcement_reads WHERE ann_id=? AND device_id=?",
                    (row["id"], device_id)
                ).fetchone()
                is_read = read and read["dismissed"]

            result.append({
                "id": row["id"],
                "title": row["title"],
                "content": row["content"],
                "priority": row["priority"],
                "mode": row["mode"],
                "dismissable": bool(row["dismissable"]),
                "dismissed": is_read,
                "start_time": row["start_time"],
                "end_time": row["end_time"],
            })
        return result

    def _get_active_announcements(self, device_id=""):
        anns = self._get_anns_for_device(device_id)
        return self._json({"ok": True, "announcements": anns})

    def _get_update_check(self):
        return self._json({
            "ok": True,
            "force_min_version": self._get_config("force_min_version", "0"),
            "latest_version": self._get_config("latest_version", "0"),
            "version_name": self._get_config("version_name", ""),
            "download_url": self._get_config("download_url", ""),
            "update_notes": self._get_config("update_notes", ""),
        })

    def _ann_list(self):
        rows = self.db.execute("SELECT * FROM announcements ORDER BY sort_order DESC, id DESC LIMIT 100").fetchall()
        anns = []
        for r in rows:
            anns.append({
                "id": r["id"], "title": r["title"], "content": r["content"],
                "priority": r["priority"], "mode": r["mode"],
                "start_time": r["start_time"], "end_time": r["end_time"],
                "active": r["active"], "dismissable": r["dismissable"],
                "created_at": r["created_at"]
            })
        return self._json({"ok": True, "announcements": anns})

    def _handle_ann_api(self, path, raw_body):
        try:
            data = json.loads(raw_body) if raw_body else {}
        except:
            return self._json({"ok": False, "msg": "JSON解析错误"})

        if path == "/api/ann/add":
            return self._ann_add(data)
        elif path == "/api/ann/edit":
            return self._ann_edit(data)
        elif path == "/api/ann/del":
            return self._ann_del(data)
        elif path == "/api/ann/dismiss":
            return self._ann_dismiss(data)
        return self._json({"ok": False, "msg": "未知路径"})

    def _ann_add(self, data):
        self.db.execute("""
            INSERT INTO announcements (title, content, priority, mode, start_time, end_time, dismissable, sort_order)
            VALUES (?,?,?,?,?,?,?,?)
        """, (
            data.get("title", ""),
            data.get("content", ""),
            data.get("priority", 0),
            data.get("mode", "banner"),
            data.get("start_time", None),
            data.get("end_time", None),
            data.get("dismissable", 1),
            data.get("sort_order", 0),
        ))
        self.db.commit()
        print(f"[ANN] 新增公告: {data.get('title', '')[:30]}")
        return self._json({"ok": True, "msg": "公告已发布"})

    def _ann_edit(self, data):
        ann_id = data.get("id", 0)
        row = self.db.execute("SELECT * FROM announcements WHERE id=?", (ann_id,)).fetchone()
        if not row:
            return self._json({"ok": False, "msg": "公告不存在"})

        fields = ["title", "content", "priority", "mode", "start_time", "end_time", "active", "dismissable", "sort_order"]
        sets = []
        vals = []
        for f in fields:
            if f in data:
                sets.append(f"{f}=?")
                vals.append(data[f])
        if sets:
            sets.append("updated_at=datetime('now','localtime')")
            vals.append(ann_id)
            self.db.execute(f"UPDATE announcements SET {','.join(sets)} WHERE id=?", vals)
            self.db.commit()
        return self._json({"ok": True, "msg": "已更新"})

    def _ann_del(self, data):
        ann_id = data.get("id", 0)
        self.db.execute("DELETE FROM announcements WHERE id=?", (ann_id,))
        self.db.execute("DELETE FROM announcement_reads WHERE ann_id=?", (ann_id,))
        self.db.commit()
        return self._json({"ok": True, "msg": "已删除"})

    def _ann_dismiss(self, data):
        """客户端关闭公告"""
        ann_id = data.get("ann_id", 0)
        device_id = data.get("device_id", "")
        if not ann_id or not device_id:
            return self._json({"ok": False, "msg": "缺少参数"})
        self.db.execute("INSERT OR REPLACE INTO announcement_reads (ann_id, device_id, dismissed) VALUES (?,?,1)",
                        (ann_id, device_id))
        self.db.commit()
        return self._json({"ok": True})

    # ================================================================
    #  更新管理
    # ================================================================
    def _handle_update_api(self, path, raw_body):
        try:
            data = json.loads(raw_body) if raw_body else {}
        except:
            return self._json({"ok": False, "msg": "JSON解析错误"})

        if path == "/api/update/set":
            for k in ["force_min_version", "latest_version", "version_name", "download_url", "update_notes"]:
                if k in data:
                    self._set_config(k, str(data[k]))
            print(f"[UPDATE] 版本配置已更新: v{data.get('version_name','?')}")
            return self._json({"ok": True, "msg": "版本配置已更新"})
        return self._json({"ok": False, "msg": "未知路径"})

    # ================================================================
    #  管理后台 API
    # ================================================================
    def _handle_admin_api(self, path, raw_body):
        try:
            data = json.loads(raw_body) if raw_body else {}
        except:
            return self._json({"ok": False, "msg": "JSON解析错误"})

        # ── 登录 (不需要认证) ──
        if path == "/api/admin/login":
            return self._admin_login(data)
        if path == "/api/admin/logout":
            return self._admin_logout()
        if path == "/api/admin/check_session":
            ok, user = self._check_admin_auth()
            return self._json({"ok": ok, "username": user})

        # ── 以下需要认证 ──
        ok, user = self._check_admin_auth()
        if not ok:
            return self._json({"ok": False, "msg": "未登录, 请先登录"}, 401)

        # ── 卡密生成 ──
        if path == "/api/admin/gen":
            return self._admin_gen_cards(data, user)
        # ── 卡密列表 ──
        if path == "/api/admin/list":
            return self._admin_list_cards(data)
        # ── 卡密操作 ──
        if path == "/api/admin/action":
            return self._admin_card_action(data, user)
        # ── 批量操作 ──
        if path == "/api/admin/batch":
            return self._admin_batch_action(data, user)
        # ── 统计看板 ──
        if path == "/api/admin/stats":
            return self._admin_get_stats()
        # ── 日志查询 ──
        if path == "/api/admin/logs":
            return self._admin_get_logs(data)
        # ── 导出卡密 ──
        if path == "/api/admin/export":
            return self._admin_export_cards(data)
        # ── 修改密码 ──
        if path == "/api/admin/change_pwd":
            return self._admin_change_pwd(data, user)

        return self._json({"ok": False, "msg": "未知admin API"})

    def _admin_login(self, data):
        username = data.get("username", "").strip()
        password = data.get("password", "")

        row = self.db.execute("SELECT * FROM admins WHERE username=? AND active=1", (username,)).fetchone()
        if not row:
            return self._json({"ok": False, "msg": "用户名或密码错误"})

        pw_hash = hash_password(password)
        if pw_hash != row["password_hash"]:
            return self._json({"ok": False, "msg": "用户名或密码错误"})

        # 生成 session token
        token = secrets.token_hex(32)
        self._sessions[token] = (username, time.time() + ADMIN_SESSION_TIMEOUT)

        # 更新登录记录
        ip = self.client_address[0]
        self.db.execute("UPDATE admins SET last_login=datetime('now','localtime'), login_ip=? WHERE id=?",
                        (ip, row["id"]))
        self.db.commit()

        print(f"[ADMIN] {username} 登录成功 (IP: {ip})")
        return self._json({"ok": True, "msg": "登录成功", "username": username, "token": token, "role": row["role"]})

    def _admin_logout(self):
        token = self.headers.get("X-Session-Token", "")
        cookie_header = self.headers.get("Cookie", "")
        for part in cookie_header.split(";"):
            part = part.strip()
            if part.startswith("session="):
                token = part[8:]
                break
        if token in self._sessions:
            del self._sessions[token]
        return self._json({"ok": True})

    def _admin_gen_cards(self, data, user):
        """生成卡密 — 支持多时长 + 公用卡 + 批量"""
        prefix     = data.get("prefix", "KEY")
        count      = min(int(data.get("count", 1)), 500)
        card_type  = data.get("card_type", "device")      # device / public
        dur_type   = data.get("duration_type", "permanent")
        dur_value  = int(data.get("duration_value", 0))
        dur_label  = data.get("duration_label", "")
        max_conc   = int(data.get("max_concurrent", 0))   # 公用卡最大并发, 0=不限

        if card_type not in ("device", "public"):
            return self._json({"ok": False, "msg": "无效的卡密类型"})
        if dur_type not in DURATION_MAP:
            return self._json({"ok": False, "msg": "无效的时长类型"})

        # 自动生成标签
        if not dur_label:
            if dur_type == "permanent":
                dur_label = "永久"
            else:
                dur_label = f"{dur_value}{DURATION_MAP[dur_type]['label']}"

        expire_at = calc_expire(dur_type, dur_value)

        keys = []
        now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        for _ in range(count):
            # 生成唯一卡密
            while True:
                suffix = secrets.token_hex(4).upper()
                card_key = f"{prefix}-{suffix}"
                existing = self.db.execute("SELECT id FROM cards WHERE card_key=?", (card_key,)).fetchone()
                if not existing:
                    break

            self.db.execute("""
                INSERT INTO cards (card_key, prefix, card_type, duration_type, duration_value, duration_label,
                                   expire_at, max_concurrent, created_by, created_at, active)
                VALUES (?,?,?,?,?,?,?,?,?,?,1)
            """, (card_key, prefix, card_type, dur_type, dur_value, dur_label, expire_at, max_conc, user, now))
            keys.append(card_key)

        self.db.commit()

        # 日志
        self.db.execute("INSERT INTO logs (action, detail, ip) VALUES (?,?,?)",
                        ("batch_gen", f"user={user} count={count} type={card_type} duration={dur_label}", self.client_address[0]))
        self.db.commit()

        print(f"[ADMIN] {user} 生成 {count} 张卡密 (类型:{card_type}, 时长:{dur_label})")
        return self._json({"ok": True, "keys": keys, "count": count,
                           "card_type": card_type, "duration": dur_label})

    def _admin_list_cards(self, data=None):
        """列出卡密 — 支持筛选"""
        data = data or {}
        page      = max(int(data.get("page", 1)), 1)
        per_page  = min(int(data.get("per_page", 50)), 200)
        card_type = data.get("card_type", "")       # device / public / ""
        dur_type  = data.get("duration_type", "")   # hour/day/week/month/year/permanent
        banned    = data.get("banned", "")           # 0/1/""
        active    = data.get("active", "")           # 0/1/""
        search    = data.get("search", "").strip()
        sort_by   = data.get("sort", "id_desc")     # id_desc / id_asc / expire / created

        where = ["1=1"]
        params = []

        if card_type:
            where.append("card_type=?")
            params.append(card_type)
        if dur_type:
            where.append("duration_type=?")
            params.append(dur_type)
        if banned != "":
            where.append("banned=?")
            params.append(int(banned))
        if active != "":
            where.append("active=?")
            params.append(int(active))
        if search:
            where.append("(card_key LIKE ? OR prefix LIKE ?)")
            params.extend([f"%{search}%", f"%{search}%"])

        # 排序
        sort_map = {
            "id_desc": "id DESC",
            "id_asc": "id ASC",
            "expire": "expire_at ASC",
            "created": "created_at DESC",
        }
        order = sort_map.get(sort_by, "id DESC")

        # 总数
        total = self.db.execute(f"SELECT COUNT(*) as cnt FROM cards WHERE {' AND '.join(where)}", params).fetchone()["cnt"]

        # 分页
        offset = (page - 1) * per_page
        rows = self.db.execute(
            f"SELECT * FROM cards WHERE {' AND '.join(where)} ORDER BY {order} LIMIT ? OFFSET ?",
            params + [per_page, offset]
        ).fetchall()

        cards = []
        for r in rows:
            cards.append({
                "id": r["id"], "card_key": r["card_key"], "prefix": r["prefix"],
                "card_type": r["card_type"], "device_id": r["device_id"] or "",
                "banned": r["banned"], "active": r["active"],
                "duration_type": r["duration_type"], "duration_value": r["duration_value"],
                "duration_label": r["duration_label"],
                "expire_at": r["expire_at"] or "", "activated_at": r["activated_at"] or "",
                "last_heart": r["last_heart"] or "",
                "is_expired": is_expired(r["expire_at"]),
                "max_concurrent": r["max_concurrent"],
                "concurrent_count": r["concurrent_count"],
                "total_users": r["total_users"],
                "created_at": r["created_at"], "created_by": r["created_by"],
            })

        return self._json({
            "ok": True,
            "cards": cards,
            "total": total,
            "page": page,
            "per_page": per_page,
            "total_pages": max(1, (total + per_page - 1) // per_page),
        })

    def _admin_card_action(self, data, user):
        """单张卡密操作"""
        card_id = data.get("id", 0)
        action  = data.get("action", "")
        row = self.db.execute("SELECT * FROM cards WHERE id=?", (card_id,)).fetchone()
        if not row:
            return self._json({"ok": False, "msg": "卡密不存在"})

        ip = self.client_address[0]
        ck = row["card_key"][:16]

        if action == "ban":
            self.db.execute("UPDATE cards SET banned=1 WHERE id=?", (card_id,))
            self.db.execute("INSERT INTO logs (card_id,card_key,action,detail,ip) VALUES (?,?,?,?,?)",
                            (card_id, ck, "ban", f"by {user}", ip))
        elif action == "unban":
            self.db.execute("UPDATE cards SET banned=0 WHERE id=?", (card_id,))
            self.db.execute("INSERT INTO logs (card_id,card_key,action,detail,ip) VALUES (?,?,?,?,?)",
                            (card_id, ck, "unban", f"by {user}", ip))
        elif action == "enable":
            self.db.execute("UPDATE cards SET active=1 WHERE id=?", (card_id,))
        elif action == "disable":
            self.db.execute("UPDATE cards SET active=0 WHERE id=?", (card_id,))
        elif action == "unbind":
            self.db.execute("UPDATE cards SET device_id=NULL, token=NULL WHERE id=?", (card_id,))
            # 同时断开公用卡会话
            self.db.execute("UPDATE public_sessions SET active=0 WHERE card_id=?", (card_id,))
            self.db.execute("UPDATE cards SET concurrent_count=0 WHERE id=?", (card_id,))
            self.db.execute("INSERT INTO logs (card_id,card_key,action,detail,ip) VALUES (?,?,?,?,?)",
                            (card_id, ck, "unbind", f"by {user}", ip))
        elif action == "delete":
            # 先清理相关数据
            self.db.execute("DELETE FROM public_sessions WHERE card_id=?", (card_id,))
            self.db.execute("DELETE FROM logs WHERE card_id=?", (card_id,))
            self.db.execute("DELETE FROM cards WHERE id=?", (card_id,))
            self.db.commit()
            print(f"[ADMIN] {user} 删除卡密 #{card_id} ({ck})")
            return self._json({"ok": True, "msg": "已删除"})
        elif action == "kick":
            # 踢下线 (公用卡)
            self.db.execute("UPDATE public_sessions SET active=0 WHERE card_id=?", (card_id,))
            self.db.execute("UPDATE cards SET concurrent_count=0, token=NULL WHERE id=?", (card_id,))
        else:
            return self._json({"ok": False, "msg": f"未知操作: {action}"})

        self.db.commit()
        print(f"[ADMIN] {user} {action} 卡密 #{card_id} ({ck})")
        return self._json({"ok": True, "msg": f"操作 {action} 成功"})

    def _admin_batch_action(self, data, user):
        """批量操作"""
        ids = data.get("ids", [])
        action = data.get("action", "")
        if not ids or not action:
            return self._json({"ok": False, "msg": "缺少参数"})

        cnt = 0
        for cid in ids:
            if action == "ban":
                self.db.execute("UPDATE cards SET banned=1 WHERE id=?", (cid,))
                cnt += self.db.total_changes
            elif action == "unban":
                self.db.execute("UPDATE cards SET banned=0 WHERE id=?", (cid,))
                cnt += self.db.total_changes
            elif action == "enable":
                self.db.execute("UPDATE cards SET active=1 WHERE id=?", (cid,))
                cnt += self.db.total_changes
            elif action == "disable":
                self.db.execute("UPDATE cards SET active=0 WHERE id=?", (cid,))
                cnt += self.db.total_changes
            elif action == "delete":
                self.db.execute("DELETE FROM cards WHERE id=?", (cid,))
                cnt += self.db.total_changes

        self.db.commit()
        self.db.execute("INSERT INTO logs (action, detail, ip) VALUES (?,?,?)",
                        ("batch_action", f"by {user} action={action} count={len(ids)}", self.client_address[0]))
        self.db.commit()
        return self._json({"ok": True, "msg": f"已{action} {cnt} 张卡密"})

    def _admin_get_stats(self):
        """数据统计看板"""
        now = datetime.now()
        today = now.strftime("%Y-%m-%d")
        month_ago = (now - timedelta(days=30)).strftime("%Y-%m-%d")

        # 总卡密数
        total_cards = self.db.execute("SELECT COUNT(*) as cnt FROM cards").fetchone()["cnt"]
        total_active = self.db.execute("SELECT COUNT(*) as cnt FROM cards WHERE active=1 AND banned=0").fetchone()["cnt"]
        total_public = self.db.execute("SELECT COUNT(*) as cnt FROM cards WHERE card_type='public' AND active=1 AND banned=0").fetchone()["cnt"]
        total_device = self.db.execute("SELECT COUNT(*) as cnt FROM cards WHERE card_type='device' AND active=1 AND banned=0").fetchone()["cnt"]
        total_banned = self.db.execute("SELECT COUNT(*) as cnt FROM cards WHERE banned=1").fetchone()["cnt"]

        # 已激活
        activated = self.db.execute("SELECT COUNT(*) as cnt FROM cards WHERE device_id IS NOT NULL AND active=1 AND banned=0").fetchone()["cnt"]

        # 各时长统计
        dur_stats = self.db.execute("""
            SELECT duration_type, duration_label, COUNT(*) as cnt
            FROM cards WHERE active=1 AND banned=0
            GROUP BY duration_type, duration_label ORDER BY cnt DESC
        """).fetchall()
        duration_stats = [{"type": r["duration_type"], "label": r["duration_label"], "count": r["cnt"]} for r in dur_stats]

        # 到期分布
        expiring_7d = self.db.execute("""
            SELECT COUNT(*) as cnt FROM cards WHERE active=1 AND banned=0 AND device_id IS NOT NULL
            AND expire_at IS NOT NULL AND expire_at <= datetime('now','+7 days') AND expire_at > datetime('now')
        """).fetchone()["cnt"]
        expiring_30d = self.db.execute("""
            SELECT COUNT(*) as cnt FROM cards WHERE active=1 AND banned=0 AND device_id IS NOT NULL
            AND expire_at IS NOT NULL AND expire_at <= datetime('now','+30 days') AND expire_at > datetime('now')
        """).fetchone()["cnt"]
        expired = self.db.execute("""
            SELECT COUNT(*) as cnt FROM cards WHERE active=1 AND banned=0
            AND expire_at IS NOT NULL AND expire_at < datetime('now')
        """).fetchone()["cnt"]

        # 今日数据
        today_logins = self.db.execute("""
            SELECT COUNT(*) as cnt FROM logs WHERE action LIKE '%login%' AND created_at >= ?
        """, (today,)).fetchone()["cnt"]
        today_activations = self.db.execute("""
            SELECT COUNT(*) as cnt FROM cards WHERE activated_at >= ?
        """, (today,)).fetchone()["cnt"]

        # 公用卡在线数
        public_online = self.db.execute("SELECT COUNT(*) as cnt FROM public_sessions WHERE active=1").fetchone()["cnt"]
        public_total_users = self.db.execute("SELECT COALESCE(SUM(total_users),0) as cnt FROM cards WHERE card_type='public'").fetchone()["cnt"]

        # 最近 30 天登录趋势
        daily_logins = self.db.execute("""
            SELECT substr(created_at,1,10) as dt, COUNT(*) as cnt
            FROM logs WHERE action LIKE '%login%' AND created_at >= ?
            GROUP BY dt ORDER BY dt
        """, (month_ago,)).fetchall()
        daily_login_data = [{"date": r["dt"], "count": r["cnt"]} for r in daily_logins]

        return self._json({
            "ok": True,
            "stats": {
                "total_cards": total_cards,
                "total_active": total_active,
                "total_public": total_public,
                "total_device": total_device,
                "total_banned": total_banned,
                "activated": activated,
                "activation_rate": f"{(activated / total_active * 100):.1f}%" if total_active > 0 else "0%",
                "expiring_7d": expiring_7d,
                "expiring_30d": expiring_30d,
                "expired": expired,
                "today_logins": today_logins,
                "today_activations": today_activations,
                "public_online": public_online,
                "public_total_users": public_total_users,
                "duration_stats": duration_stats,
                "daily_logins": daily_login_data,
            }
        })

    def _admin_get_logs(self, data=None):
        """日志查询"""
        data = data or {}
        page     = max(int(data.get("page", 1)), 1)
        per_page = min(int(data.get("per_page", 50)), 200)
        action   = data.get("action", "")
        card_id  = data.get("card_id", "")
        device_id = data.get("device_id", "")
        date_from = data.get("date_from", "")
        date_to   = data.get("date_to", "")

        where = ["1=1"]
        params = []

        if action:
            where.append("action=?")
            params.append(action)
        if card_id:
            where.append("card_id=?")
            params.append(int(card_id))
        if device_id:
            where.append("device_id LIKE ?")
            params.append(f"%{device_id}%")
        if date_from:
            where.append("created_at >= ?")
            params.append(date_from)
        if date_to:
            where.append("created_at <= ?")
            params.append(date_to + " 23:59:59")

        total = self.db.execute(f"SELECT COUNT(*) as cnt FROM logs WHERE {' AND '.join(where)}", params).fetchone()["cnt"]
        offset = (page - 1) * per_page
        rows = self.db.execute(
            f"SELECT * FROM logs WHERE {' AND '.join(where)} ORDER BY id DESC LIMIT ? OFFSET ?",
            params + [per_page, offset]
        ).fetchall()

        logs = []
        for r in rows:
            logs.append({
                "id": r["id"], "card_id": r["card_id"], "card_key": r["card_key"],
                "device_id": r["device_id"], "card_type": r["card_type"],
                "action": r["action"], "detail": r["detail"], "ip": r["ip"],
                "created_at": r["created_at"],
            })

        return self._json({
            "ok": True, "logs": logs, "total": total,
            "page": page, "per_page": per_page,
            "total_pages": max(1, (total + per_page - 1) // per_page),
        })

    def _admin_export_cards(self, data):
        """导出未使用的卡密"""
        card_type = data.get("card_type", "")
        dur_type  = data.get("duration_type", "")

        where = ["device_id IS NULL", "active=1", "banned=0"]
        params = []
        if card_type:
            where.append("card_type=?")
            params.append(card_type)
        if dur_type:
            where.append("duration_type=?")
            params.append(dur_type)

        rows = self.db.execute(
            f"SELECT card_key, card_type, duration_label FROM cards WHERE {' AND '.join(where)} ORDER BY id DESC LIMIT 1000",
            params
        ).fetchall()

        lines = ["卡密,类型,时长"]
        for r in rows:
            lines.append(f"{r['card_key']},{r['card_type']},{r['duration_label']}")

        csv_content = "\n".join(lines)
        self._send(200, "text/csv; charset=utf-8-sig", csv_content)

    def _admin_change_pwd(self, data, user):
        old_pw = data.get("old_password", "")
        new_pw = data.get("new_password", "")
        if len(new_pw) < 6:
            return self._json({"ok": False, "msg": "新密码至少6位"})

        row = self.db.execute("SELECT * FROM admins WHERE username=?", (user,)).fetchone()
        if not row or row["password_hash"] != hash_password(old_pw):
            return self._json({"ok": False, "msg": "旧密码错误"})

        self.db.execute("UPDATE admins SET password_hash=? WHERE username=?",
                        (hash_password(new_pw), user))
        self.db.commit()
        # 清除所有会话, 强制重新登录
        self._sessions.clear()
        return self._json({"ok": True, "msg": "密码已修改, 请重新登录"})


# ================================================================
#  登录页 HTML
# ================================================================
LOGIN_HTML = r'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ImGuiOverlay - 管理登录</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font:14px system-ui;background:#0d1117;color:#c9d1d9;display:flex;justify-content:center;align-items:center;min-height:100vh}
.login-box{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:40px;width:360px;text-align:center}
.login-box h1{font-size:22px;color:#58a6ff;margin-bottom:8px}
.login-box p{color:#8b949e;margin-bottom:24px;font-size:13px}
.login-box input{width:100%;padding:10px 14px;border-radius:6px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:14px;margin-bottom:12px}
.login-box button{width:100%;padding:10px;border-radius:6px;border:none;background:#238636;color:#fff;font-size:15px;cursor:pointer;margin-top:8px}
.login-box button:hover{background:#2ea043}
.error{color:#f85149;font-size:13px;margin-top:8px;display:none}
</style>
</head>
<body>
<div class="login-box">
  <h1>ImGuiOverlay</h1>
  <p>管理后台登录</p>
  <input id="user" placeholder="用户名" value="admin">
  <input id="pass" type="password" placeholder="管理密码">
  <button onclick="login()">登录</button>
  <div class="error" id="err"></div>
</div>
<script>
async function login(){
  const u=document.getElementById("user").value.trim();
  const p=document.getElementById("pass").value;
  const e=document.getElementById("err");
  e.style.display="none";
  if(!u||!p){ e.textContent="请填写用户名和密码";e.style.display="block";return }
  try{
    const r=await fetch("/api/admin/login",{method:"POST",body:JSON.stringify({username:u,password:p}),headers:{"Content-Type":"application/json"}});
    const d=await r.json();
    if(d.ok){
      document.cookie="session="+d.token+";path=/;max-age=3600";
      location.href="/admin";
    }else{
      e.textContent=d.msg||"登录失败";e.style.display="block";
    }
  }catch(x){
    e.textContent="连接失败, 请检查服务器";e.style.display="block";
  }
}
document.getElementById("pass").addEventListener("keydown",e=>{if(e.key==="Enter")login()});
</script>
</body>
</html>'''

# ================================================================
#  管理后台 HTML (SPA 单页应用)
# ================================================================
ADMIN_HTML = r'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ImGuiOverlay 卡密管理 v3.0</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font:13px system-ui;background:#0d1117;color:#c9d1d9;display:flex;min-height:100vh}
.sidebar{width:200px;background:#161b22;border-right:1px solid #21262d;padding:16px 0;flex-shrink:0}
.sidebar h2{font-size:15px;color:#58a6ff;padding:0 16px 16px;border-bottom:1px solid #21262d;margin-bottom:8px}
.sidebar a{display:block;padding:10px 16px;color:#8b949e;text-decoration:none;font-size:13px;transition:all .15s;cursor:pointer}
.sidebar a:hover,.sidebar a.active{color:#c9d1d9;background:#1c2129}
.main{flex:1;padding:20px 24px;overflow-y:auto}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px;margin-bottom:16px}
.card h3{font-size:14px;color:#8b949e;margin-bottom:10px;padding-bottom:8px;border-bottom:1px solid #21262d}
input,select,textarea,button{padding:8px 12px;border-radius:6px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:13px}
input:focus,select:focus,textarea:focus{outline:none;border-color:#58a6ff}
button{cursor:pointer;background:#238636;border-color:#238636;color:#fff;min-width:60px;transition:all .15s}
button:hover{opacity:0.85}
button.danger{background:#da3633;border-color:#da3633}
button.warn{background:#9e6a03;border-color:#9e6a03}
button.info{background:#1f6feb;border-color:#1f6feb}
button.sm{padding:4px 10px;font-size:12px;min-width:auto}
table{width:100%;border-collapse:collapse;font-size:12px}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #21262d}
th{color:#8b949e;font-weight:500;position:sticky;top:0;background:#161b22}
tr:hover{background:#1c2129}
.badge{padding:2px 6px;border-radius:10px;font-size:11px;display:inline-block}
.badge-ok{background:#23863622;color:#3fb950}
.badge-warn{background:#9e6a0322;color:#d29922}
.badge-danger{background:#da363322;color:#f85149}
.badge-info{background:#1f6feb22;color:#58a6ff}
.flex{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:12px}
.grid3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:12px}
.grid4{display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:12px}
.stat-num{font-size:28px;font-weight:700;color:#58a6ff}
.stat-label{font-size:12px;color:#8b949e;margin-top:4px}
.pagination{display:flex;gap:4px;margin-top:12px;justify-content:center}
.pagination button{min-width:36px;padding:6px 10px;font-size:12px;background:#21262d;border-color:#30363d}
.pagination button.current{background:#1f6feb;border-color:#1f6feb}
#toast{position:fixed;top:16px;right:16px;padding:12px 18px;border-radius:8px;font-size:13px;display:none;z-index:999;max-width:400px}
.tab-btn{padding:6px 14px;border-radius:6px;cursor:pointer;background:transparent;border:1px solid transparent;color:#8b949e;font-size:12px}
.tab-btn.active{background:#1c2129;border-color:#30363d;color:#c9d1d9}
.modal-overlay{position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.7);display:flex;justify-content:center;align-items:center;z-index:100}
.modal-box{background:#161b22;border:1px solid #30363d;border-radius:12px;padding:24px;width:500px;max-width:90vw;max-height:80vh;overflow-y:auto}
.chart-bar{height:8px;border-radius:4px;background:#21262d;overflow:hidden;margin:4px 0}
.chart-fill{height:100%;border-radius:4px;transition:width .5s}
textarea{width:100%;resize:vertical;min-height:60px}
.toolbar{margin-bottom:12px}
@media(max-width:800px){.sidebar{display:none}.grid2,.grid3,.grid4{grid-template-columns:1fr}}
</style>
</head>
<body>
<!-- 侧边栏 -->
<div class="sidebar">
  <h2>ImGuiOverlay v3.0</h2>
  <a class="active" onclick="switchTab('gen')" id="nav-gen">生成卡密</a>
  <a onclick="switchTab('cards')" id="nav-cards">卡密管理</a>
  <a onclick="switchTab('stats')" id="nav-stats">统计看板</a>
  <a onclick="switchTab('announce')" id="nav-announce">公告管理</a>
  <a onclick="switchTab('update')" id="nav-update">版本管理</a>
  <a onclick="switchTab('logs')" id="nav-logs">操作日志</a>
  <a onclick="switchTab('settings')" id="nav-settings">系统设置</a>
  <a onclick="doLogout()" style="color:#f85149">退出登录</a>
</div>

<div class="main" id="main"></div>
<div id="toast"></div>

<script>
// ── 全局状态 ──
const API="/api/admin";
let SESSION_TOKEN=document.cookie.split(";").find(c=>c.trim().startsWith("session="))?.split("=")[1]||"";
let currentTab="gen";
let cardPage=1,cardFilters={},logPage=1;

// ── 工具 ──
function toast(msg,ok=true){
  const t=document.getElementById("toast");
  t.textContent=msg;t.style.display="block";
  t.style.background=ok?"#238636":"#da3633";t.style.color="#fff";
  setTimeout(()=>t.style.display="none",2000);
}
async function api(path,data={}){
  try{
    const r=await fetch(path,{method:"POST",body:JSON.stringify(data),headers:{"Content-Type":"application/json","X-Session-Token":SESSION_TOKEN}});
    if(r.status===401){location.href="/login";return null}
    return await r.json();
  }catch(e){toast("连接失败",false);return null}
}
function fdate(d){if(!d||d==="永久")return"永久";return d.replace("T"," ").substring(0,19)}
function badge(v,labels=["","",""],colors=["ok","warn","danger"]){
  let c=colors[Math.min(v||0,colors.length-1)];
  return `<span class="badge badge-${c}">${labels[Math.min(v||0,labels.length-1)]||v}</span>`;
}
function formatNum(n){return n>999?(n/1000).toFixed(1)+"k":n}

// ── 导航 ──
function switchTab(tab){
  currentTab=tab;cardPage=1;logPage=1;
  document.querySelectorAll(".sidebar a").forEach(a=>a.classList.remove("active"));
  document.getElementById("nav-"+tab)?.classList.add("active");
  const m=document.getElementById("main");
  if(tab==="gen") renderGen();
  else if(tab==="cards") renderCards();
  else if(tab==="stats") renderStats();
  else if(tab==="announce") renderAnnounce();
  else if(tab==="update") renderUpdate();
  else if(tab==="logs") renderLogs();
  else if(tab==="settings") renderSettings();
}
function doLogout(){
  api("/api/admin/logout").then(()=>{document.cookie="session=;max-age=0";location.href="/login"});
}

// ── Tab 1: 生成卡密 ──
function renderGen(){
  const presets=[{t:"hour",v:1,l:"1小时"},{t:"hour",v:6,l:"6小时"},{t:"hour",v:12,l:"12小时"},{t:"day",v:1,l:"1天"},{t:"day",v:3,l:"3天"},{t:"day",v:7,l:"7天"},{t:"week",v:1,l:"1周"},{t:"month",v:1,l:"1个月"},{t:"month",v:3,l:"3个月"},{t:"month",v:6,l:"6个月"},{t:"year",v:1,l:"1年"},{t:"permanent",v:0,l:"永久"}];
  let h=`
<div class="card"><h3>生成卡密</h3>
<div class="flex" style="margin-bottom:12px">
  <div><label style="font-size:11px;color:#8b949e">前缀</label><br><input id="prefix" value="VIP" size="10"></div>
  <div><label style="font-size:11px;color:#8b949e">数量</label><br><input id="count" type="number" value="1" min="1" max="500" size="6"></div>
  <div><label style="font-size:11px;color:#8b949e">卡密类型</label><br><select id="ctype"><option value="device">设备绑定</option><option value="public">公用卡 (不绑设备)</option></select></div>
</div>
<div style="margin-bottom:10px"><label style="font-size:11px;color:#8b949e">公用卡最大并发数 (0=不限制)</label><br><input id="maxcon" type="number" value="0" min="0" size="6" style="width:120px"></div>
<div><label style="font-size:11px;color:#8b949e">时长选择</label></div>
<div class="flex" style="margin:8px 0">`;
  presets.forEach((p,i)=>{
    h+=`<button class="sm ${i===3?'info':''}" id="preset-${i}" onclick="selPreset(${i},'${p.t}',${p.v},'${p.l}')">${p.l}</button>`;
  });
  h+=`</div>
<div style="margin:10px 0"><label style="font-size:11px;color:#8b949e">自定义</label><br>
  <div class="flex">
    <select id="dtype"><option value="permanent">永久</option><option value="hour">小时</option><option value="day">天</option><option value="week">周</option><option value="month">月</option><option value="year">年</option></select>
    <input id="dvalue" type="number" value="0" min="0" size="6" style="width:80px">
    <input id="dlabel" placeholder="标签(可选)" size="10">
  </div>
</div>
<button onclick="genCards()" style="margin-top:8px;padding:10px 24px">生成卡密</button>
<div id="genResult" style="margin-top:12px;font-family:monospace;font-size:12px;color:#58a6ff;white-space:pre-wrap;max-height:300px;overflow-y:auto"></div>
</div>`;
  document.getElementById("main").innerHTML=h;
  window._selPreset={t:"day",v:1,l:"1天"};
}
function selPreset(i,t,v,l){
  window._selPreset={t,v,l};
  document.querySelectorAll("[id^='preset-']").forEach(b=>b.classList.remove("info"));
  document.getElementById("preset-"+i)?.classList.add("info");
  document.getElementById("dtype").value=t;
  document.getElementById("dvalue").value=v;
  document.getElementById("dlabel").value=l;
}
async function genCards(){
  const p=window._selPreset||{t:"permanent",v:0,l:"永久"};
  const prefix=document.getElementById("prefix").value||"KEY";
  const count=parseInt(document.getElementById("count").value)||1;
  const ctype=document.getElementById("ctype").value;
  const maxcon=parseInt(document.getElementById("maxcon").value)||0;
  const dtype=document.getElementById("dtype").value;
  const dvalue=parseInt(document.getElementById("dvalue").value)||p.v;
  const dlabel=document.getElementById("dlabel").value||p.l;

  const r=await api("/api/admin/gen",{prefix,count,card_type:ctype,duration_type:dtype,duration_value:dvalue,duration_label:dlabel,max_concurrent:maxcon});
  if(!r||!r.ok){toast(r?.msg||"生成失败",false);return}
  document.getElementById("genResult").innerHTML=
    `已生成 <b>${r.count}</b> 张卡密 (${r.card_type}, ${r.duration})：<br><br>`+
    r.keys.map(k=>`<span style="cursor:pointer" onclick="copyText('${k}')" title="点击复制">${k}</span>`).join("<br>");
  toast("已生成 "+r.count+" 张卡密");
}
function copyText(t){navigator.clipboard.writeText(t).then(()=>toast("已复制"));}

// ── Tab 2: 卡密管理 ──
async function renderCards(filters={}){
  cardFilters={...cardFilters,...filters};
  const f=cardFilters;
  const qs=Object.entries(f).map(([k,v])=>`${k}=${encodeURIComponent(v)}`).join("&");
  const r=await api("/api/admin/list",{...f,page:cardPage,per_page:30});
  if(!r||!r.ok)return;

  let h=`
<div class="card"><h3>卡密管理 (共 ${r.total} 张)</h3>
<div class="flex toolbar">
  <select onchange="renderCards({card_type:this.value})"><option value="">全部类型</option><option value="device" ${f.card_type=="device"?"selected":""}>设备绑定</option><option value="public" ${f.card_type=="public"?"selected":""}>公用卡</option></select>
  <select onchange="renderCards({duration_type:this.value})"><option value="">全部时长</option><option value="hour" ${f.duration_type=="hour"?"selected":""}>小时</option><option value="day" ${f.duration_type=="day"?"selected":""}>天</option><option value="week" ${f.duration_type=="week"?"selected":""}>周</option><option value="month" ${f.duration_type=="month"?"selected":""}>月</option><option value="year" ${f.duration_type=="year"?"selected":""}>年</option><option value="permanent" ${f.duration_type=="permanent"?"selected":""}>永久</option></select>
  <input id="searchCard" placeholder="搜索卡密..." size="16" value="${f.search||""}" onkeydown="if(event.key==='Enter')renderCards({search:this.value})">
  <button class="sm" onclick="renderCards({search:document.getElementById('searchCard').value})">搜索</button>
  <button class="sm info" onclick="exportCards()">导出CSV</button>
</div>
<table><thead><tr>
  <th><input type="checkbox" id="selectAll" onclick="toggleAll(this)"></th>
  <th>ID</th><th>卡密</th><th>类型</th><th>设备/状态</th><th>时长</th><th>到期</th><th>创建</th><th>操作</th>
</tr></thead><tbody id="cardBody">`;

  (r.cards||[]).forEach(c=>{
    let devInfo="-";
    if(c.card_type==="public"){
      devInfo=`在线:${c.concurrent_count}/${c.max_concurrent||"∞"} 累计:${c.total_users}`;
    }else{
      devInfo=c.device_id?c.device_id.substring(0,12)+"...":"未激活";
    }
    let statusBadge="";
    if(c.banned) statusBadge='<span class="badge badge-danger">封禁</span> ';
    if(!c.active) statusBadge+='<span class="badge badge-warn">禁用</span> ';
    if(c.is_expired&&c.expire_at) statusBadge+='<span class="badge badge-danger">已过期</span> ';
    if(c.card_type==="public") statusBadge='<span class="badge badge-info">公用</span> '+statusBadge;

    let actions='';
    if(c.banned) actions+=`<button class="sm" onclick="cardAct(${c.id},'unban')">解封</button> `;
    else actions+=`<button class="sm danger" onclick="cardAct(${c.id},'ban')">封禁</button> `;
    if(c.active) actions+=`<button class="sm warn" onclick="cardAct(${c.id},'disable')">禁用</button> `;
    else actions+=`<button class="sm" onclick="cardAct(${c.id},'enable')">启用</button> `;
    if(c.card_type!=="public"&&c.device_id) actions+=`<button class="sm warn" onclick="cardAct(${c.id},'unbind')">解绑</button> `;
    if(c.card_type==="public"&&c.concurrent_count>0) actions+=`<button class="sm danger" onclick="cardAct(${c.id},'kick')">踢下线</button> `;
    actions+=`<button class="sm danger" onclick="cardAct(${c.id},'delete')">删除</button>`;

    h+=`<tr>
      <td><input type="checkbox" value="${c.id}" class="cb-card"></td>
      <td>${c.id}</td>
      <td style="font-family:monospace;cursor:pointer" onclick="copyText('${c.card_key}')" title="点击复制">${c.card_key}</td>
      <td>${statusBadge}</td>
      <td style="font-size:11px">${devInfo}</td>
      <td>${c.duration_label}</td>
      <td style="${c.is_expired?'color:#f85149':''}">${fdate(c.expire_at)}</td>
      <td style="font-size:11px">${c.created_at?.substring(0,10)}</td>
      <td style="white-space:nowrap">${actions}</td>
    </tr>`;
  });
  h+=`</tbody></table>`;

  // 分页
  let pages="";
  for(let i=1;i<=r.total_pages;i++){
    pages+=`<button class="${i===cardPage?'current':''}" onclick="cardPage=${i};renderCards()">${i}</button>`;
  }
  // 批量操作
  let batchBtn='<button class="sm danger" onclick="batchAct('+"'"+'ban'+"'"+')">批量封禁</button> <button class="sm" onclick="batchAct('+"'"+'unban'+"'"+')">批量解封</button> <button class="sm danger" onclick="batchAct('+"'"+'delete'+"'"+')">批量删除</button>';
  h+=`<div class="flex" style="margin-top:8px">${batchBtn}</div><div class="pagination">${pages}</div></div>`;
  document.getElementById("main").innerHTML=h;
}
async function cardAct(id,action){
  if(action==="delete"&&!confirm("确定删除该卡密？此操作不可恢复！"))return;
  const r=await api("/api/admin/action",{id,action});
  if(r&&r.ok)toast(action+" 成功");else toast(r?.msg||"操作失败",false);
  renderCards();
}
function toggleAll(el){
  document.querySelectorAll(".cb-card").forEach(cb=>cb.checked=el.checked);
}
async function batchAct(action){
  const ids=[...document.querySelectorAll(".cb-card:checked")].map(cb=>parseInt(cb.value));
  if(!ids.length){toast("请先选择卡密",false);return}
  if(action==="delete"&&!confirm(`确定删除这 ${ids.length} 张卡密？`))return;
  const r=await api("/api/admin/batch",{ids,action});
  if(r&&r.ok)toast("已"+action+" "+ids.length+" 张");else toast(r?.msg||"失败",false);
  renderCards();
}
async function exportCards(){
  const r=await api("/api/admin/list",{per_page:500});
  if(!r||!r.cards)return;
  let csv="卡密,类型,时长,状态,到期\n";
  r.cards.forEach(c=>csv+=`${c.card_key},${c.card_type},${c.duration_label},${c.banned?"封禁":"正常"},${c.expire_at||"永久"}\n`);
  const blob=new Blob(["\uFEFF"+csv],{type:"text/csv"});
  const a=document.createElement("a");a.href=URL.createObjectURL(blob);a.download="cards_export.csv";a.click();
}

// ── Tab 3: 统计看板 ──
async function renderStats(){
  const r=await api("/api/admin/stats");
  if(!r||!r.ok){document.getElementById("main").innerHTML="<div class='card'>加载失败</div>";return}
  const s=r.stats;
  let h=`
<div class="grid4" style="margin-bottom:16px">
  <div class="card"><div class="stat-num">${formatNum(s.total_active)}</div><div class="stat-label">活跃卡密</div></div>
  <div class="card"><div class="stat-num">${formatNum(s.activated)}</div><div class="stat-label">已激活 (${s.activation_rate})</div></div>
  <div class="card"><div class="stat-num">${formatNum(s.public_online)}</div><div class="stat-label">公用卡在线</div></div>
  <div class="card"><div class="stat-num">${s.today_logins}</div><div class="stat-label">今日登录</div></div>
</div>
<div class="grid3" style="margin-bottom:16px">
  <div class="card"><div class="stat-num" style="color:#3fb950">${s.total_device}</div><div class="stat-label">设备绑定卡</div></div>
  <div class="card"><div class="stat-num" style="color:#58a6ff">${s.total_public}</div><div class="stat-label">公用卡 (累计${formatNum(s.public_total_users)}人次)</div></div>
  <div class="card"><div class="stat-num" style="color:#f85149">${s.total_banned}</div><div class="stat-label">已封禁</div></div>
</div>
<div class="grid2">
  <div class="card"><h3>到期分布</h3>
    <div class="flex" style="justify-content:space-between;margin:8px 0"><span>即将到期(7天)</span><span style="color:#d29922;font-weight:600">${s.expiring_7d}</span></div>
    <div class="flex" style="justify-content:space-between;margin:8px 0"><span>即将到期(30天)</span><span style="color:#d29922;font-weight:600">${s.expiring_30d}</span></div>
    <div class="flex" style="justify-content:space-between;margin:8px 0"><span>已过期</span><span style="color:#f85149;font-weight:600">${s.expired}</span></div>
  </div>
  <div class="card"><h3>时长分布</h3>
    ${(s.duration_stats||[]).map(d=>
      `<div class="flex" style="justify-content:space-between;margin:6px 0"><span>${d.label}</span><span style="font-weight:600">${d.count}</span></div>`
    ).join("")}
  </div>
</div>`;
  document.getElementById("main").innerHTML=h;
}

// ── Tab 4: 公告管理 ──
async function renderAnnounce(){
  const r=await fetch("/api/ann/list").then(r=>r.json());
  const anns=r.announcements||[];
  let h=`
<div class="card"><h3>发布公告</h3>
<div class="flex" style="margin-bottom:8px"><input id="annTitle" placeholder="公告标题" style="flex:1"></div>
<textarea id="annContent" placeholder="公告内容..."></textarea>
<div class="flex" style="margin:8px 0">
  <select id="annPriority"><option value="0">普通</option><option value="1">重要</option><option value="2">紧急</option></select>
  <select id="annMode"><option value="banner">横幅通知</option><option value="popup">弹窗强制</option></select>
  <input id="annStart" type="datetime-local" placeholder="开始时间(可选)">
  <input id="annEnd" type="datetime-local" placeholder="结束时间(可选)">
  <label style="font-size:12px"><input type="checkbox" id="annDismiss" checked> 可关闭</label>
</div>
<button onclick="addAnn()">发布公告</button>
</div>

<div class="card"><h3>公告列表 (${anns.length})</h3>
<table><thead><tr><th>ID</th><th>标题</th><th>内容</th><th>优先级</th><th>模式</th><th>时间</th><th>状态</th><th>操作</th></tr></thead><tbody>
${anns.map(a=>{
  let pBadge=a.priority==2?'<span class="badge badge-danger">紧急</span>':a.priority==1?'<span class="badge badge-warn">重要</span>':'<span class="badge badge-info">普通</span>';
  let mBadge=a.mode==="popup"?'<span class="badge badge-danger">弹窗</span>':'<span class="badge badge-ok">横幅</span>';
  return `<tr>
    <td>${a.id}</td><td>${a.title||"-"}</td>
    <td style="max-width:200px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">${a.content}</td>
    <td>${pBadge}</td><td>${mBadge}</td>
    <td style="font-size:11px">${a.start_time?.substring(0,16)||"-"} ~ ${a.end_time?.substring(0,16)||"-"}</td>
    <td>${a.active?'<span class="badge badge-ok">启用</span>':'<span class="badge badge-warn">停用</span>'}</td>
    <td>
      <button class="sm" onclick="toggleAnn(${a.id},${a.active?0:1})">${a.active?'停用':'启用'}</button>
      <button class="sm danger" onclick="delAnn(${a.id})">删除</button>
    </td>
  </tr>`;
}).join("")}
</tbody></table></div>`;
  document.getElementById("main").innerHTML=h;
}
async function addAnn(){
  const r=await api("/api/ann/add",{
    title:document.getElementById("annTitle").value,
    content:document.getElementById("annContent").value,
    priority:parseInt(document.getElementById("annPriority").value),
    mode:document.getElementById("annMode").value,
    start_time:document.getElementById("annStart").value||null,
    end_time:document.getElementById("annEnd").value||null,
    dismissable:document.getElementById("annDismiss").checked?1:0,
  });
  if(r&&r.ok){toast("公告已发布");renderAnnounce()}else toast(r?.msg||"发布失败",false);
}
async function toggleAnn(id,active){await api("/api/ann/edit",{id,active});renderAnnounce()}
async function delAnn(id){if(confirm("确定删除?")){await api("/api/ann/del",{id});renderAnnounce()}}

// ── Tab 5: 版本管理 ──
async function renderUpdate(){
  const r=await fetch("/api/update/check").then(r=>r.json());
  let h=`
<div class="card"><h3>版本更新配置</h3>
<div class="flex" style="margin-bottom:8px">
  <div><label style="font-size:11px;color:#8b949e">最新版本号</label><br><input id="verName" placeholder="v2.40" value="${r.version_name||""}"></div>
  <div><label style="font-size:11px;color:#8b949e">最新版本代码</label><br><input id="latestVer" type="number" placeholder="240" value="${r.latest_version||"0"}"></div>
  <div><label style="font-size:11px;color:#8b949e">强制最低版本</label><br><input id="forceVer" type="number" placeholder="0" value="${r.force_min_version||"0"}"></div>
</div>
<div style="margin-bottom:8px"><label style="font-size:11px;color:#8b949e">下载链接</label><br><input id="dlUrl" placeholder="https://..." style="width:100%" value="${r.download_url||""}"></div>
<div style="margin-bottom:8px"><label style="font-size:11px;color:#8b949e">更新说明</label><br><textarea id="updateNotes">${r.update_notes||""}</textarea></div>
<button onclick="saveUpdate()">保存配置</button>
<p style="margin-top:8px;font-size:12px;color:#8b949e">提示: 客户端版本低于"强制最低版本"时弹窗强制更新且无法跳过; 低于"最新版本代码"但高于最低版本时提示可选更新。</p>
</div>`;
  document.getElementById("main").innerHTML=h;
}
async function saveUpdate(){
  const r=await api("/api/update/set",{
    version_name:document.getElementById("verName").value,
    latest_version:document.getElementById("latestVer").value,
    force_min_version:document.getElementById("forceVer").value,
    download_url:document.getElementById("dlUrl").value,
    update_notes:document.getElementById("updateNotes").value,
  });
  if(r&&r.ok)toast("版本配置已保存");else toast(r?.msg||"保存失败",false);
}

// ── Tab 6: 操作日志 ──
async function renderLogs(filters={}){
  cardFilters={...cardFilters,...filters};
  const r=await api("/api/admin/logs",{page:logPage,per_page:30,...cardFilters});
  if(!r||!r.ok){document.getElementById("main").innerHTML="<div class='card'>加载失败</div>";return}
  let h=`
<div class="card"><h3>操作日志 (共 ${r.total} 条)</h3>
<div class="flex toolbar">
  <input id="logDev" placeholder="设备ID..." size="16">
  <input id="logDate" type="date">
  <button class="sm" onclick="renderLogs({device_id:document.getElementById('logDev').value,date_from:document.getElementById('logDate').value})">查询</button>
</div>
<table><thead><tr><th>时间</th><th>动作</th><th>卡密</th><th>设备</th><th>详情</th><th>IP</th></tr></thead><tbody>
${(r.logs||[]).map(l=>`<tr>
  <td style="font-size:11px">${l.created_at}</td>
  <td>${l.action}</td>
  <td style="font-size:11px">${l.card_key||"-"}</td>
  <td style="font-size:11px">${l.device_id||"-"}</td>
  <td style="font-size:11px">${l.detail||""}</td>
  <td style="font-size:11px">${l.ip}</td>
</tr>`).join("")}
</tbody></table>`;

  let pages="";
  for(let i=1;i<=r.total_pages;i++) pages+=`<button class="${i===logPage?'current':''}" onclick="logPage=${i};renderLogs()">${i}</button>`;
  h+=`<div class="pagination">${pages}</div></div>`;
  document.getElementById("main").innerHTML=h;
}

// ── Tab 7: 系统设置 ──
function renderSettings(){
  document.getElementById("main").innerHTML=`
<div class="card"><h3>修改管理员密码</h3>
<div style="max-width:400px">
  <input id="oldPwd" type="password" placeholder="旧密码" style="margin-bottom:8px;width:100%"><br>
  <input id="newPwd" type="password" placeholder="新密码 (至少6位)" style="margin-bottom:8px;width:100%"><br>
  <input id="newPwd2" type="password" placeholder="确认新密码" style="margin-bottom:8px;width:100%"><br>
  <button onclick="changePwd()">修改密码</button>
</div></div>`;
}
async function changePwd(){
  const oldPwd=document.getElementById("oldPwd").value;
  const newPwd=document.getElementById("newPwd").value;
  const newPwd2=document.getElementById("newPwd2").value;
  if(newPwd!==newPwd2){toast("两次密码不一致",false);return}
  if(newPwd.length<6){toast("密码至少6位",false);return}
  const r=await api("/api/admin/change_pwd",{old_password:oldPwd,new_password:newPwd});
  if(r&&r.ok){toast("密码已修改, 请重新登录");setTimeout(()=>location.href="/login",1500)}
  else toast(r?.msg||"修改失败",false);
}

// ── 启动: 先检查登录状态 ──
(async function init(){
  const r=await api("/api/admin/check_session");
  if(!r||!r.ok){location.href="/login";return}
  renderGen();
})();
</script>
</body>
</html>'''


# ================================================================
#  主程序入口
# ================================================================
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ImGuiOverlay 卡密系统 v3.0")
    parser.add_argument("--port", type=int, default=SERVER_PORT, help=f"监听端口 (默认: {SERVER_PORT})")
    parser.add_argument("--host", type=str, default="0.0.0.0", help="监听地址")
    args = parser.parse_args()

    port = args.port
    host = args.host

    # 初始化数据库
    db = init_db()
    print("=" * 55)
    print(f"  {APP_NAME} 卡密验证系统 v3.0")
    print(f"  数据库: {DB_PATH}")
    print(f"  监听:   http://{host}:{port}")
    print(f"  管理:   http://{host}:{port}/admin")
    print(f"  登录:   http://{host}:{port}/login")
    print(f"  默认账号: admin / admin888")
    print("=" * 55)

    server = HTTPServer((host, port), APIHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n服务器已停止")
        server.server_close()
