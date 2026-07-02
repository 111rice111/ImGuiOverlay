"""
ImGuiOverlay 卡密验证服务端 (Python + SQLite)
启动: python server.py  监听 0.0.0.0:8080

通信加密: XOR-CBC (简单高效, 服务端+客户端密钥一致)
"""

import sqlite3, json, time, secrets, hashlib, struct
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime, timedelta
from health_monitor import (
    start_monitor, run_full_diagnostic, get_current_metrics,
    get_recent_diagnostics, get_alert_history, get_metrics_history,
    get_announcements, add_announcement, del_announcement,
    get_force_version, set_force_version, MANUAL_TROUBLESHOOTING
)

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
        path = self.path.split("?")[0]
        # 管理后台页面
        if path == "/admin" or path == "/admin/":
            return self._html(ADMIN_HTML)
        # 诊断仪表盘
        if path == "/diag" or path == "/diag/":
            return self._html(DIAG_HTML)
        # 公开健康检查
        if path == "/api/status":
            return self._json(ok=True, msg="running", version="2.38")
        # 诊断数据 API (GET)
        if path == "/api/diag/now":
            return self._json(ok=True, report=run_full_diagnostic())
        if path == "/api/diag/metrics":
            return self._json(ok=True, metrics=get_current_metrics())
        if path == "/api/diag/alerts":
            return self._json(ok=True, alerts=get_alert_history(50))
        if path == "/api/diag/manual":
            return self._json(ok=True, guide=MANUAL_TROUBLESHOOTING)
        # 公告查询
        if path == "/api/ann/list":
            return self._json(ok=True, announcements=get_announcements())
        if path == "/api/update/get":
            return self._json(ok=True, version=get_force_version())
        # 客户端: 检查强制更新 + 获取公告
        if path == "/api/check":
            ver = get_force_version()
            ann = get_announcements()
            return self._json(ok=True, force_version=ver, announcements=ann)
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

        # ── 诊断 API (明文, 浏览器调用) ──
        if path.startswith("/api/diag/"):
            try: data = json.loads(body) if body else {}
            except: return self._json(ok=False, msg="json error")
            if path == "/api/diag/now":
                return self._json(ok=True, report=run_full_diagnostic())
            elif path == "/api/diag/metrics":
                return self._json(ok=True, metrics=get_current_metrics())
            elif path == "/api/diag/history":
                return self._json(ok=True, history=get_recent_diagnostics(30))
            elif path == "/api/diag/alerts":
                return self._json(ok=True, alerts=get_alert_history(50))
            elif path == "/api/diag/charts":
                hours = data.get("hours", 1)
                return self._json(ok=True, data=get_metrics_history(hours))
            elif path == "/api/diag/manual":
                return self._json(ok=True, guide=MANUAL_TROUBLESHOOTING)
            return self._json(ok=False, msg="unknown diag path")

        # ── 公告 API (明文) ──
        if path.startswith("/api/ann/"):
            try: data = json.loads(body) if body else {}
            except: return self._json(ok=False, msg="json error")
            if path == "/api/ann/list":
                return self._json(ok=True, announcements=get_announcements())
            elif path == "/api/ann/add":
                ok = add_announcement(data.get("content", ""))
                return self._json(ok=ok)
            elif path == "/api/ann/del":
                ok = del_announcement(data.get("id", 0))
                return self._json(ok=ok)
            return self._json(ok=False, msg="unknown ann path")

        # ── 强制更新 API (明文) ──
        if path.startswith("/api/update/"):
            try: data = json.loads(body) if body else {}
            except: return self._json(ok=False, msg="json error")
            if path == "/api/update/set":
                ok = set_force_version(data.get("version", "0.0.0"))
                return self._json(ok=ok)
            elif path == "/api/update/get":
                return self._json(ok=True, version=get_force_version())
            return self._json(ok=False, msg="unknown update path")

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

DIAG_HTML = '''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ImGuiOverlay — 服务器诊断</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:'Segoe UI',system-ui,sans-serif;background:#0a0e14;color:#cdd6f4;min-height:100vh}
.header{background:linear-gradient(135deg,#1a1f2e,#0d1117);padding:16px 24px;border-bottom:1px solid #2a3040;display:flex;justify-content:space-between;align-items:center}
.header h1{font-size:1.3em;color:#89b4fa}
.header .status{font-size:0.85em;padding:4px 12px;border-radius:12px}
.status-ok{background:#1a3a1a;color:#a6e3a1}
.status-fail{background:#3a1a1a;color:#f38ba8}
.nav{display:flex;gap:4px;padding:8px 24px;background:#11151c;border-bottom:1px solid #1e2530}
.nav a{color:#6c7086;text-decoration:none;padding:6px 16px;border-radius:6px;font-size:0.9em;transition:all .2s}
.nav a:hover,.nav a.active{color:#89b4fa;background:#1e2530}
.panels{padding:16px 24px;display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:1400px;margin:0 auto}
@media(max-width:900px){.panels{grid-template-columns:1fr}}
.card{background:#11151c;border:1px solid #1e2530;border-radius:8px;padding:14px}
.card h3{color:#89b4fa;font-size:0.95em;margin-bottom:10px;padding-bottom:6px;border-bottom:1px solid #1e2530}
.metric-row{display:flex;justify-content:space-between;padding:5px 0;font-size:0.85em;border-bottom:1px solid #181c24}
.metric-row:last-child{border-bottom:none}
.metric-val{font-weight:600;font-family:'Cascadia Code',monospace}
.metric-ok{color:#a6e3a1}
.metric-warn{color:#fab387}
.metric-fail{color:#f38ba8}
.alert-item{padding:6px 10px;margin:4px 0;border-radius:4px;font-size:0.82em;display:flex;align-items:flex-start;gap:6px}
.alert-critical{border-left:3px solid #f38ba8;background:#2a1015}
.alert-warning{border-left:3px solid #fab387;background:#2a2010}
.alert-info{border-left:3px solid #89b4fa;background:#10182a}
.btn{background:#1e2530;color:#cdd6f4;border:1px solid #2a3040;padding:7px 16px;border-radius:6px;cursor:pointer;font-size:0.85em;transition:all .2s}
.btn:hover{background:#2a3040;border-color:#89b4fa}
.btn-danger{background:#3a101a;border-color:#f38ba8;color:#f38ba8}
.btn-danger:hover{background:#4a1520}
.btn-sm{padding:4px 10px;font-size:0.78em}
.flex-row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
input,textarea{background:#0d1117;border:1px solid #2a3040;color:#cdd6f4;padding:6px 10px;border-radius:4px;font-size:0.85em;width:100%}
textarea{min-height:60px;resize:vertical}
.chart-bar{height:8px;border-radius:4px;background:#1e2530;margin:4px 0;overflow:hidden}
.chart-fill{height:100%;border-radius:4px;transition:width .5s}
.full{grid-column:1/-1}
.suggestion{background:#0a1015;border-left:3px solid #89b4fa;padding:10px 14px;margin:6px 0;border-radius:0 6px 6px 0;font-size:0.85em;white-space:pre-wrap;font-family:'Cascadia Code',monospace}
.refresh{font-size:0.75em;color:#6c7086}
</style>
</head>
<body>
<div class="header">
  <div>
    <h1>⚙ 服务器诊断仪表盘</h1>
    <span class="refresh" id="refreshStatus">等待数据...</span>
  </div>
  <span class="status status-ok" id="overallStatus">检测中...</span>
</div>
<div class="nav">
  <a href="/admin">← 卡密管理</a>
  <a href="#" class="active" onclick="switchTab('overview',this)">概览</a>
  <a href="#" onclick="switchTab('announce',this)">公告管理</a>
  <a href="#" onclick="switchTab('update',this)">强制更新</a>
</div>

<div class="panels" id="panelOverview">
  <!-- 系统指标 -->
  <div class="card">
    <h3>📊 实时系统指标</h3>
    <div id="metricsDisplay"><span class="refresh">加载中...</span></div>
  </div>
  <!-- 检查清单 -->
  <div class="card">
    <h3>🔍 诊断检查</h3>
    <div id="checksDisplay"><span class="refresh">加载中...</span></div>
  </div>
  <!-- 告警 -->
  <div class="card full">
    <h3>🔔 最近告警</h3>
    <div id="alertsDisplay"><span class="refresh">加载中...</span></div>
  </div>
  <!-- 修复建议 -->
  <div class="card full">
    <h3>💡 修复建议</h3>
    <div id="suggestionsDisplay"><span class="refresh">加载中...</span></div>
  </div>
  <!-- 手动排查 -->
  <div class="card full">
    <h3>📋 手动排查指引</h3>
    <div id="manualDisplay"><span class="refresh">加载中...</span></div>
  </div>
</div>

<div class="panels" id="panelAnnounce" style="display:none">
  <div class="card">
    <h3>📢 发布公告</h3>
    <textarea id="annContent" placeholder="公告内容..."></textarea>
    <br><br><button class="btn" onclick="addAnn()">发布公告</button>
  </div>
  <div class="card">
    <h3>📋 公告列表</h3>
    <div id="annList"><span class="refresh">加载中...</span></div>
  </div>
</div>

<div class="panels" id="panelUpdate" style="display:none">
  <div class="card">
    <h3>📦 强制更新设置</h3>
    <p style="color:#6c7086;font-size:0.82em;margin-bottom:8px">设置后, 版本低于此值的客户端将被强制要求更新</p>
    <div class="flex-row">
      <input id="forceVer" placeholder="版本号, 如 2.38" style="width:200px">
      <button class="btn" onclick="setForceVer()">设置</button>
      <button class="btn btn-danger btn-sm" onclick="setForceVer('0.0.0')">关闭强制</button>
    </div>
    <p style="margin-top:8px;font-size:0.85em">当前: <b id="curForceVer">---</b></p>
  </div>
</div>

<script>
let autoRefresh=null;

function switchTab(tab,el){
  ["panelOverview","panelAnnounce","panelUpdate"].forEach(id=>document.getElementById(id).style.display="none");
  document.getElementById("panel"+(tab.charAt(0).toUpperCase()+tab.slice(1).replace("nnounce","nnounce"))).style.display="";
  document.querySelectorAll(".nav a").forEach(a=>a.classList.remove("active"));
  if(el)el.classList.add("active");
  if(tab==="overview") runDiag();
  if(tab==="announce") loadAnns();
  if(tab==="update") loadForceVer();
}

async function apiGet(url){let r=await fetch(url);return r.json()}
async function apiPost(url,data){let r=await fetch(url,{method:"POST",body:JSON.stringify(data)});return r.json()}

async function runDiag(){
  document.getElementById("overallStatus").textContent="检测中...";
  try{
    let r=await apiGet("/api/diag/now");
    let rep=r.report;
    document.getElementById("overallStatus").textContent=rep.ok?"运行正常":"异常";
    document.getElementById("overallStatus").className="status "+(rep.ok?"status-ok":"status-fail");
    document.getElementById("refreshStatus").textContent="刷新于 "+new Date().toLocaleTimeString();

    // 指标
    let m=rep.metrics;
    let met=`<div class="metric-row"><span>API 延迟</span><span class="metric-val ${m.api_ms<1000?'metric-ok':'metric-warn'}">${m.api_ms.toFixed(0)}ms</span></div>`;
    met+=`<div class="metric-row"><span>API 状态</span><span class="metric-val ${m.api_ok?'metric-ok':'metric-fail'}">${m.api_ok?'正常':'异常'}</span></div>`;
    met+=`<div class="metric-row"><span>公网隧道</span><span class="metric-val ${m.bore_alive?'metric-ok':'metric-fail'}">${m.bore_alive?'连通':'断开'}</span></div>`;
    if(m.cpu_pct>0) met+=`<div class="metric-row"><span>CPU</span><span class="metric-val ${m.cpu_pct>85?'metric-warn':'metric-ok'}">${m.cpu_pct.toFixed(0)}%</span></div>`;
    if(m.mem_pct>0) met+=`<div class="metric-row"><span>内存</span><span class="metric-val ${m.mem_pct>90?'metric-warn':'metric-ok'}">${m.mem_pct.toFixed(0)}%</span></div>`;
    if(m.disk_free_gb>0) met+=`<div class="metric-row"><span>磁盘剩余</span><span class="metric-val ${m.disk_free_gb<1?'metric-fail':'metric-ok'}">${m.disk_free_gb.toFixed(1)}GB</span></div>`;
    document.getElementById("metricsDisplay").innerHTML=met;

    // 检查
    let chk="";
    rep.checks.forEach(c=>{
      let cls=c.status==="OK"?"metric-ok":c.status==="WARN"?"metric-warn":"metric-fail";
      chk+=`<div class="metric-row"><span>${c.name}</span><span class="metric-val ${cls}">${c.status}</span></div>`;
      if(c.detail) chk+=`<div style="font-size:0.75em;color:#6c7086;padding:0 0 4px 8px">${c.detail}</div>`;
    });
    document.getElementById("checksDisplay").innerHTML=chk;

    // 告警
    let al="";
    rep.alerts.forEach(a=>al+=`<div class="alert-item alert-${a.level}">${a.message}</div>`);
    document.getElementById("alertsDisplay").innerHTML=al||"<span class='refresh'>无告警</span>";

    // 建议
    let sug="";
    rep.suggestions.forEach(s=>sug+=`<div class="suggestion">${s}</div>`);
    document.getElementById("suggestionsDisplay").innerHTML=sug||"<span class='refresh'>无建议</span>";
  }catch(e){
    document.getElementById("overallStatus").textContent="离线";
    document.getElementById("overallStatus").className="status status-fail";
  }
}

async function loadManual(){
  let r=await apiGet("/api/diag/manual");
  if(r.ok) document.getElementById("manualDisplay").innerHTML=`<pre class="suggestion" style="font-size:0.78em">${r.guide}</pre>`;
}

// 公告
async function addAnn(){
  let c=document.getElementById("annContent").value.trim();
  if(!c) return;
  await apiPost("/api/ann/add",{content:c});
  document.getElementById("annContent").value="";
  loadAnns();
}
async function loadAnns(){
  let r=await apiGet("/api/ann/list");
  let h="";
  (r.announcements||[]).forEach(a=>h+=`<div class="alert-item alert-info"><span style="flex:1">${a.content}</span><button class="btn btn-sm btn-danger" onclick="delAnn(${a.id})">×</button></div>`);
  document.getElementById("annList").innerHTML=h||"<span class='refresh'>暂无公告</span>";
}
async function delAnn(id){await apiPost("/api/ann/del",{id:id});loadAnns()}

// 版本
async function loadForceVer(){
  let r=await apiGet("/api/update/get");
  document.getElementById("curForceVer").textContent=r.version||"0.0.0 (不强制)";
  document.getElementById("forceVer").value=r.version==="0.0.0"?"":r.version;
}
async function setForceVer(v){
  let ver=v||document.getElementById("forceVer").value.trim()||"0.0.0";
  await apiPost("/api/update/set",{version:ver});
  loadForceVer();
}

runDiag(); loadManual(); setInterval(runDiag,15000);
</script>
</body>
</html>'''

if __name__ == "__main__":
    init_db()
    # 启动健康监控后台线程
    mon = start_monitor(port=8080)
    print("=" * 50)
    print("  ImGuiOverlay 卡密服务端 v2.38")
    print("  本地: http://0.0.0.0:8080")
    print("  管理: http://0.0.0.0:8080/admin")
    print("  诊断: http://0.0.0.0:8080/diag")
    print("  公网: http://bore.pub:3699")
    print("  示例卡密: DEMO-KEY-001 / VIP-UNLIMITED-2024")
    print("  健康监控: 已启动")
    print("=" * 50)
    HTTPServer(("0.0.0.0", 8080), APIHandler).serve_forever()
