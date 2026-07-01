"""
ImGuiOverlay 卡密验证服务端 v2 (Python + SQLite)
- 8080: 卡密验证 API (XOR-CBC 加密)
- 8090: Web 管理面板 (无需加密, 浏览器直接访问)

启动: python server_v2.py
"""

import sqlite3, json, time, secrets, hashlib, struct, os, uuid
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime, timedelta
from urllib.parse import urlparse, parse_qs
import threading

# ========== XOR-CBC 加密 (与客户端 crypto.h 同步) ==========
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

# ========== 数据库 ==========
DB_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "overlay.db")
if not os.path.exists(DB_PATH):
    DB_PATH = "overlay.db"

def get_db():
    db = sqlite3.connect(DB_PATH)
    db.row_factory = sqlite3.Row
    return db

def init_db():
    db = get_db()
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
    db.execute("INSERT OR IGNORE INTO cards (card_key, expire_at) VALUES (?,?)",
               ("DEMO-KEY-001", (datetime.now()+timedelta(days=30)).strftime('%Y-%m-%d %H:%M:%S')))
    db.execute("INSERT OR IGNORE INTO cards (card_key) VALUES (?)", ("VIP-UNLIMITED-2024",))
    db.commit()
    db.close()

# ========== 管理面板 HTML ==========
ADMIN_HTML = """<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>卡密管理面板</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;background:#f0f2f5;color:#333;min-height:100vh}
.header{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;padding:20px 24px;text-align:center}
.header h1{font-size:1.5em;margin-bottom:4px}
.header p{opacity:0.8;font-size:0.85em}
.container{max-width:900px;margin:0 auto;padding:16px}
.card{background:#fff;border-radius:12px;padding:20px;margin-bottom:16px;box-shadow:0 1px 3px rgba(0,0,0,0.1)}
.card h2{font-size:1.1em;margin-bottom:12px;color:#555;border-bottom:2px solid #667eea;padding-bottom:8px}
.form-row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}
.form-row input,.form-row select{flex:1;min-width:120px;padding:10px 12px;border:1px solid #ddd;border-radius:8px;font-size:0.9em}
.btn{padding:10px 20px;border:none;border-radius:8px;cursor:pointer;font-size:0.9em;font-weight:600;transition:all 0.2s}
.btn-primary{background:#667eea;color:#fff}
.btn-primary:hover{background:#5a6fd6}
.btn-danger{background:#e74c3c;color:#fff}
.btn-danger:hover{background:#c0392b}
.btn-success{background:#27ae60;color:#fff}
.btn-success:hover{background:#219a52}
.btn-sm{padding:6px 12px;font-size:0.8em}
table{width:100%;border-collapse:collapse;font-size:0.85em}
th,td{padding:10px 8px;text-align:left;border-bottom:1px solid #eee}
th{background:#f8f9fa;color:#666;font-weight:600}
.status-active{color:#27ae60;font-weight:600}
.status-inactive{color:#95a5a6}
.status-banned{color:#e74c3c;font-weight:600}
.badge{display:inline-block;padding:2px 8px;border-radius:10px;font-size:0.75em;font-weight:600}
.badge-active{background:#d5f5e3;color:#27ae60}
.badge-banned{background:#fadbd8;color:#e74c3c}
.badge-expired{background:#fdebd0;color:#e67e22}
.badge-free{background:#d6eaf8;color:#2980b9}
.msg{padding:10px 14px;border-radius:8px;margin-bottom:12px;display:none}
.msg-success{background:#d5f5e3;color:#27ae60;display:block}
.msg-error{background:#fadbd8;color:#e74c3c;display:block}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:12px;margin-bottom:16px}
.stat-card{background:#fff;border-radius:10px;padding:14px;text-align:center;box-shadow:0 1px 3px rgba(0,0,0,0.1)}
.stat-card .num{font-size:1.8em;font-weight:700;color:#667eea}
.stat-card .label{font-size:0.8em;color:#888;margin-top:4px}
.mono{font-family:'Courier New',monospace;font-size:0.8em;word-break:break-all}
</style>
</head>
<body>
<div class="header">
<h1>🔑 卡密管理面板</h1>
<p>ImGuiOverlay License Server</p>
</div>
<div class="container">

<div id="msg"></div>

<div class="stats" id="stats"></div>

<div class="card">
<h2>➕ 生成卡密</h2>
<div class="form-row">
<input type="text" id="newKey" placeholder="留空自动生成随机卡密">
<select id="expireDays">
<option value="0">永久有效</option>
<option value="1">1 天</option>
<option value="7">7 天</option>
<option value="30" selected>30 天</option>
<option value="90">90 天</option>
<option value="365">365 天</option>
</select>
<select id="genCount">
<option value="1">1 张</option>
<option value="5">5 张</option>
<option value="10">10 张</option>
<option value="20">20 张</option>
</select>
<button class="btn btn-primary" onclick="genCards()">生成</button>
</div>
</div>

<div class="card">
<h2>📋 所有卡密 <button class="btn btn-sm btn-primary" onclick="loadCards()" style="float:right">刷新</button></h2>
<div style="overflow-x:auto">
<table>
<thead><tr><th>ID</th><th>卡密</th><th>设备</th><th>状态</th><th>到期</th><th>最后心跳</th><th>操作</th></tr></thead>
<tbody id="cardTable"></tbody>
</table>
</div>
</div>

</div>
<script>
const API = '/api/admin';
function showMsg(text, type){const m=document.getElementById('msg');m.textContent=text;m.className='msg msg-'+type;setTimeout(()=>m.className='msg',3000)}

async function api(action, data={}){
  const r = await fetch(API,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,...data})});
  return r.json();
}

async function loadCards(){
  const res = await api('list');
  if(!res.ok) return showMsg(res.msg,'error');
  const tbody = document.getElementById('cardTable');
  tbody.innerHTML = res.cards.map(c=>{
    let status='<span class="badge badge-free">未激活</span>';
    if(c.banned) status='<span class="badge badge-banned">已封禁</span>';
    else if(c.device_id && c.expired) status='<span class="badge badge-expired">已过期</span>';
    else if(c.device_id) status='<span class="badge badge-active">使用中</span>';
    const keyShort = c.card_key.length>16?c.card_key.slice(0,16)+'...':c.card_key;
    const devShort = c.device_id ? c.device_id.slice(0,12)+'...' : '-';
    const expire = c.expire_at ? c.expire_at.slice(0,10) : '永久';
    const heart = c.last_heart ? c.last_heart.slice(5,16) : '-';
    const banBtn = c.banned
      ? `<button class="btn btn-sm btn-success" onclick="unbanCard(${c.id})">解封</button>`
      : `<button class="btn btn-sm btn-danger" onclick="banCard(${c.id})">封禁</button>`;
    const unbindBtn = c.device_id
      ? `<button class="btn btn-sm" style="background:#f39c12;color:#fff" onclick="unbindCard(${c.id})">解绑</button>`
      : '';
    return `<tr>
      <td>${c.id}</td><td class="mono">${keyShort}</td><td class="mono">${devShort}</td><td>${status}</td>
      <td>${expire}</td><td>${heart}</td>
      <td>${banBtn} ${unbindBtn}</td></tr>`;
  }).join('');
}

async function loadStats(){
  const res = await api('stats');
  if(!res.ok) return;
  document.getElementById('stats').innerHTML = `
    <div class="stat-card"><div class="num">${res.total}</div><div class="label">总卡密</div></div>
    <div class="stat-card"><div class="num">${res.active}</div><div class="label">使用中</div></div>
    <div class="stat-card"><div class="num">${res.banned}</div><div class="label">已封禁</div></div>
    <div class="stat-card"><div class="num">${res.free}</div><div class="label">未激活</div></div>`;
}

async function genCards(){
  const key = document.getElementById('newKey').value.trim();
  const days = parseInt(document.getElementById('expireDays').value);
  const count = parseInt(document.getElementById('genCount').value);
  const res = await api('gen',{key,days,count});
  if(res.ok) showMsg(res.msg,'success');
  else showMsg(res.msg,'error');
  loadCards();loadStats();
}
async function banCard(id){if(!confirm('确认封禁?'))return;const r=await api('ban',{id});showMsg(r.msg,r.ok?'success':'error');loadCards();loadStats()}
async function unbanCard(id){const r=await api('unban',{id});showMsg(r.msg,r.ok?'success':'error');loadCards();loadStats()}
async function unbindCard(id){if(!confirm('确认解绑设备?'))return;const r=await api('unbind',{id});showMsg(r.msg,r.ok?'success':'error');loadCards();loadStats()}

loadCards();loadStats();
</script>
</body>
</html>"""

# ========== 管理 API 处理器 ==========
class AdminHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[ADMIN {datetime.now().strftime('%H:%M:%S')}] {args[0]}")

    def _respond(self, data):
        resp = json.dumps(data, ensure_ascii=False)
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(resp.encode())

    def _html(self, html):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(html.encode())

    def do_GET(self):
        if self.path == "/" or self.path == "/admin":
            return self._html(ADMIN_HTML)
        self._respond({"ok": False, "msg": "not found"})

    def do_POST(self):
        if self.path != "/api/admin":
            return self._respond({"ok": False, "msg": "not found"})

        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode() if length > 0 else "{}"
        try:
            data = json.loads(body)
        except:
            return self._respond({"ok": False, "msg": "invalid json"})

        action = data.get("action", "")
        db = get_db()

        try:
            if action == "list":
                rows = db.execute("SELECT * FROM cards ORDER BY id DESC").fetchall()
                cards = []
                for r in rows:
                    expired = False
                    if r["expire_at"]:
                        try:
                            expired = datetime.strptime(r["expire_at"], "%Y-%m-%d %H:%M:%S") < datetime.now()
                        except: pass
                    cards.append({
                        "id": r["id"], "card_key": r["card_key"],
                        "device_id": r["device_id"], "banned": bool(r["banned"]),
                        "expire_at": r["expire_at"], "last_heart": r["last_heart"],
                        "created_at": r["created_at"], "expired": expired
                    })
                return self._respond({"ok": True, "cards": cards})

            elif action == "stats":
                total = db.execute("SELECT COUNT(*) as c FROM cards").fetchone()["c"]
                active = db.execute("SELECT COUNT(*) as c FROM cards WHERE device_id IS NOT NULL AND banned=0").fetchone()["c"]
                banned = db.execute("SELECT COUNT(*) as c FROM cards WHERE banned=1").fetchone()["c"]
                free = db.execute("SELECT COUNT(*) as c FROM cards WHERE device_id IS NULL AND banned=0").fetchone()["c"]
                return self._respond({"ok": True, "total": total, "active": active, "banned": banned, "free": free})

            elif action == "gen":
                key = data.get("key", "").strip()
                days = int(data.get("days", 30))
                count = int(data.get("count", 1))
                count = max(1, min(count, 50))

                generated = []
                for _ in range(count):
                    if not key:
                        key = "OVERLAY-" + secrets.token_hex(4).upper()
                    expire = None
                    if days > 0:
                        expire = (datetime.now() + timedelta(days=days)).strftime('%Y-%m-%d %H:%M:%S')
                    try:
                        db.execute("INSERT INTO cards (card_key, expire_at) VALUES (?,?)", (key, expire))
                        generated.append(key)
                    except sqlite3.IntegrityError:
                        key = "OVERLAY-" + secrets.token_hex(6).upper()
                        db.execute("INSERT INTO cards (card_key, expire_at) VALUES (?,?)", (key, expire))
                        generated.append(key)
                    key = ""  # 后续循环自动生成
                db.commit()
                return self._respond({"ok": True, "msg": f"成功生成 {len(generated)} 张卡密: {', '.join(generated)}"})

            elif action == "ban":
                cid = int(data.get("id", 0))
                db.execute("UPDATE cards SET banned=1 WHERE id=?", (cid,))
                db.commit()
                return self._respond({"ok": True, "msg": "已封禁"})

            elif action == "unban":
                cid = int(data.get("id", 0))
                db.execute("UPDATE cards SET banned=0 WHERE id=?", (cid,))
                db.commit()
                return self._respond({"ok": True, "msg": "已解封"})

            elif action == "unbind":
                cid = int(data.get("id", 0))
                db.execute("UPDATE cards SET device_id=NULL, hwid=NULL, token=NULL WHERE id=?", (cid,))
                db.commit()
                return self._respond({"ok": True, "msg": "已解绑设备"})

            else:
                return self._respond({"ok": False, "msg": "unknown action"})

        except Exception as e:
            return self._respond({"ok": False, "msg": str(e)})
        finally:
            db.close()

# ========== 卡密验证 API (与 v1 兼容) ==========
class APIHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        print(f"[API {datetime.now().strftime('%H:%M:%S')}] {args[0]}")

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
        db = get_db()

        if path == "/api/verify":
            key = data.get("key", "")
            dev = data.get("device_id", "")
            hw = data.get("hwid", "")
            row = db.execute("SELECT * FROM cards WHERE card_key=?", (key,)).fetchone()
            if not row: return self._respond(False, "卡密不存在")
            if row["banned"]: return self._respond(False, "卡密已封禁")
            if row["expire_at"]:
                try:
                    if datetime.strptime(row["expire_at"], "%Y-%m-%d %H:%M:%S") < datetime.now():
                        return self._respond(False, "卡密已过期")
                except: pass
            if row["device_id"] and row["device_id"] != dev:
                return self._respond(False, "已绑定其他设备")
            if not row["device_id"]:
                db.execute("UPDATE cards SET device_id=?,hwid=?,activated_at=datetime('now','localtime') WHERE id=?",
                           (dev, hw, row["id"]))
            token = secrets.token_hex(16)
            db.execute("UPDATE cards SET token=?,last_heart=datetime('now','localtime') WHERE id=?", (token, row["id"]))
            db.execute("INSERT INTO logs (card_id,device_id,action,ip) VALUES (?,?,?,?)",
                       (row["id"], dev, "login", self.client_address[0]))
            db.commit()
            expire = row["expire_at"] or (datetime.now()+timedelta(days=365)).strftime('%Y-%m-%d %H:%M:%S')
            print(f"[LOGIN] card={key[:8]}... dev={dev[:16]}...")
            return self._respond(True, "ok", token=token, expire=expire)

        elif path == "/api/heartbeat":
            dev = data.get("device_id", "")
            tok = data.get("token", "")
            row = db.execute("SELECT * FROM cards WHERE device_id=? AND token=?", (dev, tok)).fetchone()
            if not row: return self._respond(False, "invalid", banned=True)
            if row["banned"]: return self._respond(False, "banned", banned=True)
            db.execute("UPDATE cards SET last_heart=datetime('now','localtime') WHERE id=?", (row["id"],))
            db.commit()
            return self._respond(True, "ok")

        elif path == "/api/version":
            row = db.execute("SELECT * FROM versions ORDER BY id DESC LIMIT 1").fetchone()
            if row:
                return self._respond(True, "ok", available=True, version=row["version"],
                                     url=row["url"], md5=row["md5"], changelog=row["changelog"])
            return self._respond(True, "ok", available=False)

        elif path == "/api/command":
            dev = data.get("device_id", "")
            row = db.execute("SELECT * FROM commands WHERE (device_id=? OR device_id='*') AND executed=0 ORDER BY id LIMIT 1",
                             (dev,)).fetchone()
            if row:
                db.execute("UPDATE commands SET executed=1 WHERE id=?", (row["id"],))
                db.commit()
                params = json.loads(row["params"]) if row["params"] else {}
                return self._respond(True, "ok", cmd=row["cmd"], params=params)
            return self._respond(True, "ok", cmd="")

        else:
            return self._respond(False, "unknown")

    def do_GET(self):
        self._respond(False, "POST only")

# ========== 启动 ==========
def run_api():
    init_db()
    print("=" * 50)
    print("  ImGuiOverlay 卡密验证 API")
    print("  http://0.0.0.0:8080  (客户端连接)")
    print("=" * 50)
    HTTPServer(("0.0.0.0", 8080), APIHandler).serve_forever()

def run_admin():
    print("=" * 50)
    print("  ImGuiOverlay 管理面板")
    print("  http://0.0.0.0:8090  (浏览器打开)")
    print("=" * 50)
    HTTPServer(("0.0.0.0", 8090), AdminHandler).serve_forever()

if __name__ == "__main__":
    t1 = threading.Thread(target=run_api, daemon=True)
    t2 = threading.Thread(target=run_admin, daemon=True)
    t1.start()
    t2.start()
    print("\n  服务器已启动!")
    print("  API:  http://0.0.0.0:8080")
    print("  面板: http://0.0.0.0:8090")
    print("\n  按 Ctrl+C 停止\n")
    try:
        while True: time.sleep(1)
    except KeyboardInterrupt:
        print("\n  已停止")
