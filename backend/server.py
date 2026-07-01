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

    def _json(self, **data):
        resp = json.dumps(data)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(resp.encode())

    def _html(self, body):
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(body.encode())

    def do_GET(self):
        if self.path == "/admin" or self.path == "/admin/":
            return self._html(ADMIN_HTML)
        self._respond(False, "not found")

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length).decode() if length > 0 else ""
        path = self.path.split("?")[0]
        db = self.db

        # 管理后台 API — 明文 JSON（浏览器直接 POST）
        if path.startswith("/api/admin/"):
            try: data = json.loads(body) if body else {}
            except: return self._respond(False, "json error")
            if path == "/api/admin/gen":
                prefix = data.get("prefix", "KEY")
                count = data.get("count", 1)
                expire_days = data.get("expire_days", 0)
                keys = []
                for _ in range(count):
                    k = f"{prefix}-{secrets.token_hex(4).upper()}"
                    if expire_days > 0:
                        db.execute("INSERT INTO cards (card_key, expire_at) VALUES (?, datetime('now', '+{} days'))".format(expire_days), (k,))
                    else:
                        db.execute("INSERT INTO cards (card_key) VALUES (?)", (k,))
                    keys.append(k)
                db.commit()
                print(f"[ADMIN] 生成 {count} 张卡密")
                return self._json(ok=True, keys=keys)
            elif path == "/api/admin/list":
                rows = db.execute("SELECT id, card_key, device_id, expire_at, banned, activated_at FROM cards ORDER BY id DESC LIMIT 100").fetchall()
                cards = []
                for r in rows:
                    cards.append({"id": r[0], "key": r[1], "device": r[2] or "", "expire": r[3] or "", "banned": r[4], "activated": r[5] or ""})
                return self._json(ok=True, cards=cards)
            elif path == "/api/admin/action":
                card_id = data.get("id", 0)
                action = data.get("action", "")
                if action == "ban": db.execute("UPDATE cards SET banned=1 WHERE id=?", (card_id,))
                elif action == "unban": db.execute("UPDATE cards SET banned=0 WHERE id=?", (card_id,))
                elif action == "unbind": db.execute("UPDATE cards SET device_id=NULL,token=NULL WHERE id=?", (card_id,))
                elif action == "delete": db.execute("DELETE FROM cards WHERE id=?", (card_id,))
                db.commit()
                print(f"[ADMIN] {action} card #{card_id}")
                return self._json(ok=True)
            return self._respond(False, "unknown admin path")

        # 客户端 API — XOR 加密
        try:
            plain = xor_decrypt(body).decode() if body else "{}"
            data = json.loads(plain)
        except:
            return self._respond(False, "decrypt error")
            db.commit()
            print(f"[ADMIN] {action} card #{card_id}")
            return self._respond(True, "ok")

        # ===== 客户端 API =====
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
        if self.path == "/admin" or self.path == "/admin/":
            return self._html(ADMIN_HTML)
        self._respond(False, "POST only")

# ========== 管理后台 HTML ==========
ADMIN_HTML = '''<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>ImGuiOverlay 卡密管理</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font:13px system-ui;background:#0d1117;color:#c9d1d9;padding:20px;max-width:900px;margin:0 auto}
h1{font-size:18px;color:#58a6ff;margin-bottom:16px}
.card{background:#161b22;border:1px solid #30363d;border-radius:8px;padding:16px;margin-bottom:16px}
input,select,button{padding:8px 12px;border-radius:6px;border:1px solid #30363d;background:#0d1117;color:#c9d1d9;font-size:13px}
button{cursor:pointer;background:#238636;border-color:#238636;color:#fff;min-width:60px}
button.danger{background:#da3633;border-color:#da3633}
button.warn{background:#9e6a03;border-color:#9e6a03}
table{width:100%;border-collapse:collapse;font-size:12px}
th,td{padding:8px 10px;text-align:left;border-bottom:1px solid #30363d}
th{color:#8b949e;font-weight:500}
tr:hover{background:#1c2129}
.badge{padding:2px 6px;border-radius:10px;font-size:11px}
.ok{background:#23863622;color:#3fb950}
.warn{background:#9e6a0322;color:#d29922}
.danger{background:#da363322;color:#f85149}
#toast{position:fixed;top:16px;right:16px;padding:12px 16px;border-radius:8px;font-size:13px;display:none;z-index:9}
</style>
</head>
<body>
<h1>ImGuiOverlay 卡密管理系统</h1>

<div class="card">
<h3 style="margin-bottom:10px;color:#8b949e">生成卡密</h3>
<div style="display:flex;gap:8px;flex-wrap:wrap;align-items:end">
  <div><label style="font-size:11px;color:#8b949e">前缀</label><br><input id="prefix" value="KEY" size="10"></div>
  <div><label style="font-size:11px;color:#8b949e">数量</label><br><input id="count" type="number" value="1" min="1" max="50" size="6"></div>
  <div><label style="font-size:11px;color:#8b949e">有效期(天,0=永久)</label><br><input id="expire" type="number" value="30" min="0" size="6"></div>
  <button onclick="genCards()">生成</button>
</div>
<div id="genResult" style="margin-top:10px;font-family:monospace;color:#58a6ff;white-space:pre-wrap"></div>
</div>

<div class="card">
<h3 style="margin-bottom:10px;color:#8b949e">卡密列表</h3>
<button onclick="loadList()">刷新列表</button>
<div style="overflow-x:auto;margin-top:10px">
<table><thead><tr><th>ID</th><th>卡密</th><th>绑定设备</th><th>到期</th><th>状态</th><th>操作</th></tr></thead>
<tbody id="cardList"><tr><td colspan="6" style="text-align:center;color:#8b949e">点击刷新加载数据</td></tr></tbody></table>
</div>
</div>

<div id="toast"></div>

<script>
function toast(msg, ok){ const t=document.getElementById("toast"); t.textContent=msg; t.style.display="block"; t.style.background=ok?'#238636':'#da3633'; t.style.color='#fff'; setTimeout(()=>t.style.display='none',2000) }

async function apiPost(path, data){
  try{ const r=await fetch(path,{method:'POST',body:data?JSON.stringify(data):'{}',headers:{'Content-Type':'application/json'}}); return await r.json() }
  catch(e){ toast('连接失败',false); return null }
}

function genCards(){
  const p=document.getElementById("prefix").value||"KEY";
  const c=parseInt(document.getElementById("count").value)||1;
  const e=parseInt(document.getElementById("expire").value)||0;
  apiPost("/api/admin/gen",{prefix:p,count:c,expire_days:e}).then(r=>{
    if(r&&r.ok){ document.getElementById("genResult").textContent=r.keys.join("\\n"); toast("已生成 "+r.keys.length+" 张",true); loadList() }
    else toast("生成失败",false)
  })
}

function loadList(){
  apiPost("/api/admin/list",{}).then(r=>{
    if(!r||!r.ok) return;
    const tb=document.getElementById("cardList");
    if(!r.cards||!r.cards.length){ tb.innerHTML='<tr><td colspan="6" style="text-align:center;color:#8b949e">暂无卡密</td></tr>'; return }
    tb.innerHTML=r.cards.map(c=>{
      let badge=c.banned?'<span class="badge danger">封禁</span>':c.device?'<span class="badge ok">已激活</span>':'<span class="badge warn">未使用</span>';
      let act=c.banned?'<button class="warn" onclick="act('+c.id+',\\'unban\\')">解封</button>':
               c.device?'<button class="danger" onclick="act('+c.id+',\\'ban\\')">封禁</button> <button class="warn" onclick="act('+c.id+',\\'unbind\\')">解绑</button>':
               '<button class="danger" onclick="act('+c.id+',\\'delete\\')">删除</button>';
      return '<tr><td>'+c.id+'</td><td style="font-family:monospace">'+c.key+'</td><td style="font-size:11px">'+c.device+'</td><td>'+c.expire+'</td><td>'+badge+'</td><td>'+act+'</td></tr>'
    }).join('')
  })
}

function act(id, action){
  if(action=='delete'&&!confirm('确定删除?')) return;
  apiPost("/api/admin/action",{id:id,action:action}).then(r=>{ if(r&&r.ok){ toast(action+' OK',true); loadList() } })
}

loadList();
</script>
</body>
</html>'''

if __name__ == "__main__":
    init_db()
    print("=" * 50)
    print("  ImGuiOverlay 卡密服务端")
    print("  http://0.0.0.0:8080")
    print("  示例卡密: DEMO-KEY-001 / VIP-UNLIMITED-2024")
    print("=" * 50)
    HTTPServer(("0.0.0.0", 8080), APIHandler).serve_forever()
