"""
ImGuiOverlay 卡密验证服务端 (Python + SQLite)
启动: python server.py  监听 0.0.0.0:8080

通信加密: XOR-CBC (简单高效, 服务端+客户端密钥一致)
"""

import sqlite3, json, time, secrets, hashlib, struct
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime, timedelta

# ========== XOR-CBC 加密 (与客户端 crypto.h 同步) ==========
XOR_KEY = b'ImGuiOverlay2026'  # 16字节密钥, 需与客户端一致
XOR_IV  = b'InitVector123456'  # 16字节IV

def xor_encrypt(plain: bytes) -> str:
    """XOR-CBC 加密, 输出 hex"""
    key, iv = XOR_KEY, XOR_IV
    result = bytearray()
    prev = iv
    for i in range(0, len(plain), 16):
        block = plain[i:i+16]
        if len(block) < 16:
            block = block + bytes([16 - len(block)]) * (16 - len(block))  # PKCS7 pad
        enc = bytes(b ^ key[j % 16] ^ prev[j % 16] for j, b in enumerate(block))
        result.extend(enc)
        prev = enc
    return result.hex().upper()

def xor_decrypt(hex_str: str) -> bytes:
    """XOR-CBC 解密 (hex 输入)"""
    data = bytes.fromhex(hex_str)
    key, iv = XOR_KEY, XOR_IV
    result = bytearray()
    prev = iv
    for i in range(0, len(data), 16):
        block = data[i:i+16]
        dec = bytes(b ^ key[j % 16] ^ prev[j % 16] for j, b in enumerate(block))
        result.extend(dec)
        prev = block
    # Remove PKCS7 padding
    pad = result[-1]
    if 1 <= pad <= 16:
        result = result[:-pad]
    return bytes(result)

# Test roundtrip
_test = b'{"test":true}'
assert xor_decrypt(xor_encrypt(_test)) == _test, "XOR roundtrip failed!"

# ========== 数据库 ==========
DB_PATH = "overlay.db"

def init_db():
    db = sqlite3.connect(DB_PATH)
    db.execute("""
        CREATE TABLE IF NOT EXISTS cards (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            card_key TEXT NOT NULL UNIQUE,
            device_id TEXT DEFAULT NULL,
            hwid TEXT DEFAULT NULL,
            token TEXT DEFAULT NULL,
            banned INTEGER DEFAULT 0,
            expire_at TEXT DEFAULT NULL,
            activated_at TEXT DEFAULT NULL,
            last_heart TEXT DEFAULT NULL,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    db.execute("""
        CREATE TABLE IF NOT EXISTS versions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            version INTEGER NOT NULL,
            url TEXT DEFAULT '',
            md5 TEXT DEFAULT '',
            changelog TEXT DEFAULT '',
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    db.execute("""
        CREATE TABLE IF NOT EXISTS commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT DEFAULT '*',
            cmd TEXT NOT NULL,
            params TEXT DEFAULT '{}',
            executed INTEGER DEFAULT 0,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    db.execute("""
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            card_id INTEGER DEFAULT NULL,
            device_id TEXT DEFAULT NULL,
            action TEXT NOT NULL,
            ip TEXT DEFAULT NULL,
            created_at TEXT DEFAULT (datetime('now','localtime'))
        )
    """)
    # 插入示例卡密
    db.execute("INSERT OR IGNORE INTO cards (card_key, expire_at) VALUES (?,?)",
               ("DEMO-KEY-001", (datetime.now()+timedelta(days=30)).strftime('%Y-%m-%d %H:%M:%S')))
    db.execute("INSERT OR IGNORE INTO cards (card_key) VALUES (?)", ("VIP-UNLIMITED-2024",))
    db.commit()
    return db

# ========== HTTP 服务器 ==========
class APIHandler(BaseHTTPRequestHandler):
    db = init_db()

    def log_message(self, fmt, *args):
        print(f"[{datetime.now().strftime('%H:%M:%S')}] {args[0]}")

    def _respond(self, ok, msg, **extra):
        resp = json.dumps({"ok": ok, "msg": msg, **extra})
        enc = xor_encrypt(resp.encode())
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(enc.encode())

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode() if length > 0 else ""
        try:
            plain = xor_decrypt(body).decode() if body else "{}"
            data = json.loads(plain)
        except:
            return self._respond(False, "decrypt error")

        path = self.path.split("?")[0]
        db = self.db

        if path == "/api/verify":
            key = data.get("key", "")
            dev = data.get("device_id", "")
            hw = data.get("hwid", "")
            row = db.execute("SELECT * FROM cards WHERE card_key=?", (key,)).fetchone()
            if not row: return self._respond(False, "卡密不存在")
            if row[5]: return self._respond(False, "卡密已封禁")
            if row[6] and datetime.strptime(row[6], "%Y-%m-%d %H:%M:%S") < datetime.now():
                return self._respond(False, "卡密已过期")
            if row[2] and row[2] != dev: return self._respond(False, "已绑定其他设备")
            if not row[2]:
                db.execute("UPDATE cards SET device_id=?,hwid=?,activated_at=datetime('now','localtime') WHERE id=?",
                           (dev, hw, row[0]))
            token = secrets.token_hex(16)
            db.execute("UPDATE cards SET token=?,last_heart=datetime('now','localtime') WHERE id=?", (token, row[0]))
            db.execute("INSERT INTO logs (card_id,device_id,action,ip) VALUES (?,?,?,?)",
                       (row[0], dev, "login", self.client_address[0]))
            db.commit()
            expire = row[6] or (datetime.now()+timedelta(days=365)).strftime('%Y-%m-%d %H:%M:%S')
            print(f"[LOGIN] card={key[:8]}... dev={dev[:16]}...")
            return self._respond(True, "ok", token=token, expire=expire)

        elif path == "/api/heartbeat":
            dev = data.get("device_id", "")
            tok = data.get("token", "")
            row = db.execute("SELECT * FROM cards WHERE device_id=? AND token=?", (dev, tok)).fetchone()
            if not row: return self._respond(False, "invalid", banned=True)
            if row[5]: return self._respond(False, "banned", banned=True)
            db.execute("UPDATE cards SET last_heart=datetime('now','localtime') WHERE id=?", (row[0],))
            db.commit()
            return self._respond(True, "ok")

        elif path == "/api/version":
            row = db.execute("SELECT * FROM versions ORDER BY id DESC LIMIT 1").fetchone()
            if row:
                return self._respond(True, "ok", available=True, version=row[1],
                                     url=row[2], md5=row[3], changelog=row[4])
            return self._respond(True, "ok", available=False)

        elif path == "/api/command":
            dev = data.get("device_id", "")
            row = db.execute("SELECT * FROM commands WHERE (device_id=? OR device_id='*') AND executed=0 ORDER BY id LIMIT 1",
                             (dev,)).fetchone()
            if row:
                db.execute("UPDATE commands SET executed=1 WHERE id=?", (row[0],))
                db.commit()
                params = json.loads(row[3]) if row[3] else {}
                return self._respond(True, "ok", cmd=row[2], params=params)
            return self._respond(True, "ok", cmd="")

        else:
            return self._respond(False, "unknown")

    def do_GET(self):
        self._respond(False, "POST only")

if __name__ == "__main__":
    init_db()
    print("=" * 50)
    print("  ImGuiOverlay 卡密服务端")
    print("  http://0.0.0.0:8080")
    print("  示例卡密: DEMO-KEY-001 / VIP-UNLIMITED-2024")
    print("=" * 50)
    HTTPServer(("0.0.0.0", 8080), APIHandler).serve_forever()
