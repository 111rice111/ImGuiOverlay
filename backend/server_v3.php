<?php
/**
 * ImGuiOverlay 卡密验证系统 v3.0 — PHP 版 (Baota 面板专用)
 * 单文件部署, 上传到 Baota 网站目录即可运行
 * 管理后台: http://你的IP/?page=admin
 * 
 * 加密协议: XOR-CBC, key=ImGuiOverlay2026, iv=InitVector123456, PKCS7, hex输出
 * 兼容现有 C++ 客户端 net_client.h
 */

// ========== 基础配置 ==========
define('DB_PATH', __DIR__ . '/overlay_v3.db');
define('XOR_KEY', 'ImGuiOverlay2026');
define('XOR_IV', 'InitVector123456');
define('ADMIN_SESSION_TIMEOUT', 3600);
error_reporting(0); // 生产环境关闭错误输出, 避免破坏 JSON
ini_set('display_errors', '0');
ob_start(); // 捕获所有输出，防止意外内容破坏加密响应

// ========== XOR-CBC 加密 ==========
function xor_encrypt($plain) {
    $key = XOR_KEY; $iv = XOR_IV;
    $padLen = 16 - (strlen($plain) % 16);
    $padded = $plain . str_repeat(chr($padLen), $padLen);
    $result = ''; $prev = $iv;
    $blocks = strlen($padded) / 16;
    for ($b = 0; $b < $blocks; $b++) {
        $block = substr($padded, $b * 16, 16);
        $encBlock = '';
        for ($j = 0; $j < 16; $j++) {
            $encBlock .= chr(ord($block[$j]) ^ ord($key[$j % 16]) ^ ord($prev[$j % 16]));
        }
        $result .= $encBlock;
        $prev = $encBlock;
    }
    return strtoupper(bin2hex($result));
}

function xor_decrypt($hexStr) {
    $data = hex2bin($hexStr);
    if ($data === false) return false;
    $key = XOR_KEY; $iv = XOR_IV;
    $result = ''; $prev = $iv;
    $blocks = strlen($data) / 16;
    for ($b = 0; $b < $blocks; $b++) {
        $block = substr($data, $b * 16, 16);
        $decBlock = '';
        for ($j = 0; $j < 16; $j++) {
            $decBlock .= chr(ord($block[$j]) ^ ord($key[$j % 16]) ^ ord($prev[$j % 16]));
        }
        $result .= $decBlock;
        $prev = $block;
    }
    $pad = ord($result[strlen($result) - 1]);
    if ($pad >= 1 && $pad <= 16) $result = substr($result, 0, -$pad);
    return $result;
}

// ========== 数据库 ==========
function get_db() {
    static $db = null;
    if ($db === null) {
        $db = new PDO('sqlite:' . DB_PATH);
        $db->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
        init_db($db);
    }
    return $db;
}

function init_db($db) {
    $db->exec("CREATE TABLE IF NOT EXISTS cards (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        card_key TEXT NOT NULL UNIQUE,
        prefix TEXT DEFAULT '',
        card_type TEXT DEFAULT 'device',
        device_id TEXT DEFAULT NULL,
        hwid TEXT DEFAULT NULL,
        token TEXT DEFAULT NULL,
        banned INTEGER DEFAULT 0,
        active INTEGER DEFAULT 1,
        duration_type TEXT DEFAULT 'permanent',
        duration_value INTEGER DEFAULT 0,
        duration_label TEXT DEFAULT '永久',
        expire_at TEXT DEFAULT NULL,
        activated_at TEXT DEFAULT NULL,
        last_heart TEXT DEFAULT NULL,
        created_at TEXT DEFAULT (datetime('now','localtime')),
        created_by TEXT DEFAULT 'admin',
        max_concurrent INTEGER DEFAULT 1,
        concurrent_count INTEGER DEFAULT 0,
        first_user_at TEXT DEFAULT NULL,
        last_user_at TEXT DEFAULT NULL,
        total_users INTEGER DEFAULT 0
    )");
    $db->exec("CREATE TABLE IF NOT EXISTS public_sessions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        card_id INTEGER NOT NULL,
        device_id TEXT NOT NULL,
        device_name TEXT DEFAULT '',
        token TEXT DEFAULT NULL,
        ip TEXT DEFAULT NULL,
        started_at TEXT DEFAULT (datetime('now','localtime')),
        last_heart TEXT DEFAULT NULL,
        heartbeat_count INTEGER DEFAULT 0,
        active INTEGER DEFAULT 1
    )");
    $db->exec("CREATE INDEX IF NOT EXISTS idx_ps_card ON public_sessions(card_id, active)");
    $db->exec("CREATE TABLE IF NOT EXISTS announcements (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        title TEXT DEFAULT '',
        content TEXT NOT NULL,
        priority INTEGER DEFAULT 0,
        mode TEXT DEFAULT 'banner',
        start_time TEXT DEFAULT NULL,
        end_time TEXT DEFAULT NULL,
        active INTEGER DEFAULT 1,
        dismissable INTEGER DEFAULT 1,
        sort_order INTEGER DEFAULT 0,
        created_at TEXT DEFAULT (datetime('now','localtime')),
        updated_at TEXT DEFAULT (datetime('now','localtime'))
    )");
    $db->exec("CREATE TABLE IF NOT EXISTS announcement_reads (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        ann_id INTEGER NOT NULL,
        device_id TEXT NOT NULL,
        dismissed INTEGER DEFAULT 1,
        read_at TEXT DEFAULT (datetime('now','localtime')),
        UNIQUE(ann_id, device_id)
    )");
    $db->exec("CREATE TABLE IF NOT EXISTS logs (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        card_id INTEGER DEFAULT NULL,
        card_key TEXT DEFAULT NULL,
        device_id TEXT DEFAULT NULL,
        card_type TEXT DEFAULT NULL,
        action TEXT NOT NULL,
        detail TEXT DEFAULT NULL,
        ip TEXT DEFAULT NULL,
        user_agent TEXT DEFAULT NULL,
        created_at TEXT DEFAULT (datetime('now','localtime'))
    )");
    $db->exec("CREATE INDEX IF NOT EXISTS idx_logs_card ON logs(card_id)");
    $db->exec("CREATE INDEX IF NOT EXISTS idx_logs_device ON logs(device_id)");
    $db->exec("CREATE TABLE IF NOT EXISTS admins (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        username TEXT NOT NULL UNIQUE,
        password_hash TEXT NOT NULL,
        role TEXT DEFAULT 'admin',
        last_login TEXT DEFAULT NULL,
        login_ip TEXT DEFAULT NULL,
        active INTEGER DEFAULT 1,
        created_at TEXT DEFAULT (datetime('now','localtime'))
    )");
    $db->exec("CREATE TABLE IF NOT EXISTS config (
        key TEXT PRIMARY KEY,
        value TEXT NOT NULL,
        updated_at TEXT DEFAULT (datetime('now','localtime'))
    )");
    // 初始化管理员账号
    $db->exec("INSERT OR IGNORE INTO admins (username, password_hash, role) VALUES ('admin', '" . hash('sha256', 'admin888') . "', 'superadmin')");
    // 初始化配置
    $defaults = [
        'force_min_version' => '0',
        'latest_version' => '0',
        'version_name' => '',
        'download_url' => '',
        'update_notes' => '',
        'app_name' => 'ImGuiOverlay'
    ];
    foreach ($defaults as $k => $v) {
        $db->exec("INSERT OR IGNORE INTO config (key, value) VALUES ('$k', '$v')");
    }
}

// ========== 工具函数 ==========
$DURATION_MAP = [
    'hour' => ['label' => '小时', 'unit' => 'hours'],
    'day'  => ['label' => '天',   'unit' => 'days'],
    'week' => ['label' => '周',   'unit' => 'weeks'],
    'month'=> ['label' => '月',   'unit' => 'months'],
    'year' => ['label' => '年',   'unit' => 'years'],
    'permanent' => ['label' => '永久', 'unit' => null]
];
$PRESET = [
    ['hour', 1,  '1小时'],
    ['hour', 6,  '6小时'],
    ['hour', 12, '12小时'],
    ['day',  1,  '1天'],
    ['day',  3,  '3天'],
    ['day',  7,  '7天'],
    ['week', 1,  '1周'],
    ['month',1,  '1个月'],
    ['month',3,  '3个月'],
    ['month',6,  '6个月'],
    ['year', 1,  '1年'],
    ['permanent', 0, '永久']
];

function calc_expire($type, $value) {
    global $DURATION_MAP;
    if ($type == 'permanent' || $value <= 0) return null;
    $dt = new DateTime();
    $unit = $DURATION_MAP[$type]['unit'];
    if ($unit == 'months') $dt->modify("+$value months");
    elseif ($unit == 'years') $dt->modify("+$value years");
    elseif ($unit == 'weeks') $dt->modify("+$value weeks");
    else $dt->modify("+$value $unit");
    return $dt->format('Y-m-d H:i:s');
}

function is_expired($expire_at) {
    return $expire_at !== null && strtotime($expire_at) < time();
}

// ========== Admin Session 管理 ==========
function get_sessions() {
    $f = __DIR__ . '/sessions.json';
    if (!file_exists($f)) return [];
    $data = json_decode(file_get_contents($f), true);
    return is_array($data) ? $data : [];
}

function save_sessions($s) {
    file_put_contents(__DIR__ . '/sessions.json', json_encode($s));
}

function check_admin() {
    $token = $_COOKIE['admin_token'] ?? '';
    if (!$token) return false;
    $s = get_sessions();
    if (!isset($s[$token]) || $s[$token]['expire'] < time()) {
        unset($s[$token]);
        save_sessions($s);
        return false;
    }
    $s[$token]['expire'] = time() + ADMIN_SESSION_TIMEOUT;
    save_sessions($s);
    return $s[$token]['username'];
}

function gen_card_key($prefix) {
    $chars = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
    $key = $prefix ? $prefix . '-' : '';
    for ($i = 0; $i < 16; $i++) {
        $key .= $chars[rand(0, 23)];
        if (($i + 1) % 4 == 0 && $i < 15) $key .= '-';
    }
    return $key;
}

// ========== 响应函数 ==========
function json_resp($d) {
    header('Content-Type: application/json; charset=utf-8');
    echo json_encode($d, JSON_UNESCAPED_UNICODE);
    exit;
}

function enc_resp($ok, $msg, $extra = []) {
    // C++ 客户端期望 "ok" 字段, 不是 "success"
    // 清理所有输出缓冲区，确保只返回纯加密数据
    while (ob_get_level()) ob_end_clean();
    $resp = ['ok' => (bool)$ok, 'msg' => $msg];
    foreach ($extra as $k => $v) $resp[$k] = $v;
    header('Content-Type: text/plain; charset=utf-8');
    echo xor_encrypt(json_encode($resp));
    exit;
}

function add_log($db, $card_id, $card_key, $device_id, $card_type, $action, $detail = null) {
    $ip = $_SERVER['REMOTE_ADDR'] ?? '';
    $ua = $_SERVER['HTTP_USER_AGENT'] ?? '';
    $stmt = $db->prepare("INSERT INTO logs (card_id,card_key,device_id,card_type,action,detail,ip,user_agent) VALUES (?,?,?,?,?,?,?,?)");
    $stmt->execute([$card_id, $card_key, $device_id, $card_type, $action, $detail, $ip, $ua]);
}

// ============================================================
// ========== 路由 (清晰分离管理端 / 客户端) ==========
// ============================================================
$method = $_SERVER['REQUEST_METHOD'];
$path    = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH) ?: '/';
$page    = $_GET['page'] ?? '';
$admin_api = $_GET['admin_api'] ?? '';

// --- 1. 管理后台页面 ---
if ($page == 'admin' && !$admin_api) {
    $user = check_admin();
    if (!$user) {
        show_login_page();
        exit;
    }
    show_admin_page($user);
    exit;
}

// --- 2. 管理端 API (JSON) ---
if ($admin_api) {
    handle_admin_api();
    exit;
}

// --- 3. 客户端 API (加密) ---
// 客户端路径: POST /api/verify, POST /api/heartbeat, POST /api/check
// 也支持 GET ?api=verify 风格 (兼容旧客户端)
$api_paths = ['/api/verify','/api/heartbeat','/api/check','/api/version','/api/command','/api/download','/api/ann/dismiss'];
$is_api_path = ($path && in_array($path, $api_paths));

if ($method === 'POST') {
    $raw = file_get_contents('php://input');
    if ($raw && preg_match('/^[0-9A-Fa-f]{32,}$/', trim($raw))) {
        handle_client_api($raw);
        exit;
    }
}
// 支持 API 路径或 ?api= 参数
if ($is_api_path || (isset($_GET['api']) && $_GET['api'])) {
    handle_client_api(file_get_contents('php://input'));
    exit;
}

// --- 4. 状态页 ---
http_response_code(200);
echo '<!DOCTYPE html><html><head><meta charset="utf-8"><title>ImGuiOverlay API</title></head><body>';
echo '<h2>ImGuiOverlay API v3.0 (PHP)</h2>';
echo '<p>Status: <b style="color:green">OK</b></p>';
echo '<p><a href="?page=admin">管理后台</a></p>';
echo '</body></html>';
exit;


// ============================================================
// ========== 客户端 API 处理 ==========
// ============================================================
function handle_client_api($raw) {
    if (!$raw) $raw = file_get_contents('php://input');
    $plain = xor_decrypt(trim($raw));
    if (!$plain) enc_resp(false, 'Decrypt failed');
    $data = json_decode($plain, true);
    if (!$data || !isset($data['action'])) enc_resp(false, 'Invalid request');

    $db = get_db();
    switch ($data['action']) {
        case 'verify':
            handle_verify($db, $data);
            break;
        case 'heartbeat':
            handle_heartbeat($db, $data);
            break;
        case 'check':
            handle_check($db, $data);
            break;
        default:
            enc_resp(false, 'Unknown action');
    }
}

function handle_verify($db, $data) {
    // 兼容客户端: 客户端发 "key", 也支持 "card_key"
    $card_key  = trim($data['card_key'] ?? ($data['key'] ?? ''));
    $device_id = $data['device_id'] ?? '';
    $hwid     = $data['hwid'] ?? '';

    if (!$card_key) enc_resp(false, '卡密不能为空');

    $stmt = $db->prepare("SELECT * FROM cards WHERE card_key = ?");
    $stmt->execute([$card_key]);
    $card = $stmt->fetch(PDO::FETCH_ASSOC);

    if (!$card) {
        add_log($db, null, $card_key, $device_id, null, 'verify_fail', '卡密不存在');
        enc_resp(false, '卡密不存在');
    }
    if (!$card['active']) enc_resp(false, '卡密已禁用');
    if ($card['banned']) enc_resp(false, '卡密已被封禁');
    if ($card['expire_at'] !== null && is_expired($card['expire_at'])) enc_resp(false, '卡密已过期');

    if ($card['card_type'] == 'device') {
        // 设备绑定卡
        if ($card['device_id'] === null) {
            // 首次激活
            $token = bin2hex(random_bytes(16));
            $expire = $card['expire_at'];
            if ($expire === null && $card['duration_type'] != 'permanent') {
                $expire = calc_expire($card['duration_type'], $card['duration_value']);
                $db->prepare("UPDATE cards SET expire_at = ? WHERE id = ?")->execute([$expire, $card['id']]);
            }
            $db->prepare("UPDATE cards SET device_id=?, hwid=?, token=?, activated_at=datetime('now','localtime'), last_heart=datetime('now','localtime') WHERE id=?")
               ->execute([$device_id, $hwid, $token, $card['id']]);
            add_log($db, $card['id'], $card_key, $device_id, 'device', 'activate', '首次激活');
            enc_resp(true, '激活成功', ['token' => $token, 'expire' => $expire, 'card_type' => 'device']);
        } elseif ($card['device_id'] == $device_id) {
            // 设备匹配, 重新签发 token
            $token = bin2hex(random_bytes(16));
            $db->prepare("UPDATE cards SET token=?, last_heart=datetime('now','localtime') WHERE id=?")
               ->execute([$token, $card['id']]);
            enc_resp(true, '验证成功', ['token' => $token, 'expire' => $card['expire_at'], 'card_type' => 'device']);
        } else {
            add_log($db, $card['id'], $card_key, $device_id, 'device', 'verify_fail', '设备不匹配');
            enc_resp(false, '卡密已绑定其他设备');
        }
    } elseif ($card['card_type'] == 'public') {
        // 公用卡
        $max_c = intval($card['max_concurrent']);
        if ($max_c > 0) {
            $cntStmt = $db->prepare("SELECT COUNT(*) as c FROM public_sessions WHERE card_id=? AND active=1");
            $cntStmt->execute([$card['id']]);
            $cnt = $cntStmt->fetch(PDO::FETCH_ASSOC)['c'];
            if (intval($cnt) >= $max_c) enc_resp(false, "卡密并发数已达上限 ($max_c)");
        }
        // 查找已有 session
        $stmt = $db->prepare("SELECT * FROM public_sessions WHERE card_id=? AND device_id=? AND active=1");
        $stmt->execute([$card['id'], $device_id]);
        $sess = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($sess) {
            $token = $sess['token'];
            $db->prepare("UPDATE public_sessions SET last_heart=datetime('now','localtime'), heartbeat_count=heartbeat_count+1 WHERE id=?")
               ->execute([$sess['id']]);
        } else {
            $token = bin2hex(random_bytes(16));
            $ip = $_SERVER['REMOTE_ADDR'] ?? '';
            $db->prepare("INSERT INTO public_sessions (card_id,device_id,token,ip,started_at,last_heart) VALUES (?,?,?,?,datetime('now','localtime'),datetime('now','localtime'))")
               ->execute([$card['id'], $device_id, $token, $ip]);
            $db->prepare("UPDATE cards SET concurrent_count=concurrent_count+1, first_user_at=COALESCE(first_user_at,datetime('now','localtime')), last_user_at=datetime('now','localtime'), total_users=total_users+1 WHERE id=?")
               ->execute([$card['id']]);
        }
        // 首次激活时计算过期时间
        if ($card['activated_at'] === null) {
            $expire = calc_expire($card['duration_type'], $card['duration_value']);
            $db->prepare("UPDATE cards SET activated_at=datetime('now','localtime'), expire_at=? WHERE id=?")
               ->execute([$expire, $card['id']]);
        }
        add_log($db, $card['id'], $card_key, $device_id, 'public', 'login');
        enc_resp(true, '验证成功', ['token' => $token, 'expire' => $card['expire_at'], 'card_type' => 'public']);
    }
}

function handle_heartbeat($db, $data) {
    $device_id = $data['device_id'] ?? '';
    $token     = $data['token'] ?? '';
    $card_type = $data['card_type'] ?? 'device';

    if (!$token) enc_resp(false, '缺少 token');

    // 通过 token 找到对应的卡
    if ($card_type == 'device') {
        $stmt = $db->prepare("SELECT * FROM cards WHERE token=? AND active=1 AND banned=0");
        $stmt->execute([$token]);
        $card = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$card) enc_resp(false, 'Token 无效或已过期');
        if ($card['expire_at'] !== null && is_expired($card['expire_at'])) enc_resp(false, '卡密已过期');
        if ($card['device_id'] && $card['device_id'] != $device_id) enc_resp(false, '设备不匹配');
        $db->prepare("UPDATE cards SET last_heart=datetime('now','localtime') WHERE id=?")->execute([$card['id']]);
    } elseif ($card_type == 'public') {
        $stmt = $db->prepare("SELECT ps.*, c.expire_at, c.active, c.banned FROM public_sessions ps JOIN cards c ON ps.card_id=c.id WHERE ps.token=? AND ps.device_id=? AND ps.active=1");
        $stmt->execute([$token, $device_id]);
        $sess = $stmt->fetch(PDO::FETCH_ASSOC);
        if (!$sess) enc_resp(false, 'Token 无效或已过期');
        if ($sess['expire_at'] !== null && is_expired($sess['expire_at'])) enc_resp(false, '卡密已过期');
        $db->prepare("UPDATE public_sessions SET last_heart=datetime('now','localtime'), heartbeat_count=heartbeat_count+1 WHERE id=?")
           ->execute([$sess['id']]);
        $card = $sess;
    } else {
        enc_resp(false, '未知卡密类型');
    }

    // 返回最新配置 (强制更新 / 公告等)
    $cfg = [];
    foreach ($db->query("SELECT key,value FROM config") as $r) $cfg[$r['key']] = $r['value'];
    enc_resp(true, 'ok', [
        'expire'             => $card['expire_at'] ?? '',
        'force_min_version'  => intval($cfg['force_min_version'] ?? 0),
        'latest_version'     => intval($cfg['latest_version'] ?? 0),
        'version_name'       => $cfg['version_name'] ?? '',
        'download_url'       => $cfg['download_url'] ?? '',
        'update_notes'       => $cfg['update_notes'] ?? ''
    ]);
}

function handle_check($db, $data) {
    // 客户端心跳/公告拉取 (加密请求)
    $cfg = [];
    foreach ($db->query("SELECT key,value FROM config") as $r) $cfg[$r['key']] = $r['value'];
    $now = date('Y-m-d H:i:s');
    $stmt = $db->prepare("SELECT * FROM announcements WHERE active=1 AND (start_time IS NULL OR start_time<=?) AND (end_time IS NULL OR end_time>=?) ORDER BY priority DESC, id DESC LIMIT 5");
    $stmt->execute([$now, $now]);
    $anns = $stmt->fetchAll(PDO::FETCH_ASSOC);
    enc_resp(true, 'ok', [
        'force_min_version'  => intval($cfg['force_min_version'] ?? 0),
        'latest_version'     => intval($cfg['latest_version'] ?? 0),
        'version_name'       => $cfg['version_name'] ?? '',
        'download_url'       => $cfg['download_url'] ?? '',
        'update_notes'       => $cfg['update_notes'] ?? '',
        'announcements'      => $anns
    ]);
}


// ============================================================
// ========== 管理端 API 处理 ==========
// ============================================================
function handle_admin_api() {
    $db = get_db();
    $admin_api = $_GET['admin_api'] ?? '';

    // 登录不需要鉴权
    if ($admin_api == 'login') {
        $raw  = file_get_contents('php://input');
        $data = $raw ? json_decode($raw, true) : $_POST;
        $u = $data['username'] ?? '';
        $p = $data['password'] ?? '';
        $stmt = $db->prepare("SELECT * FROM admins WHERE username=? AND active=1");
        $stmt->execute([$u]);
        $admin = $stmt->fetch(PDO::FETCH_ASSOC);
        if ($admin && hash('sha256', $p) == $admin['password_hash']) {
            $token = bin2hex(random_bytes(16));
            $s = get_sessions();
            $s[$token] = ['username' => $u, 'expire' => time() + ADMIN_SESSION_TIMEOUT];
            save_sessions($s);
            setcookie('admin_token', $token, time() + ADMIN_SESSION_TIMEOUT, '/');
            $db->prepare("UPDATE admins SET last_login=datetime('now','localtime'), login_ip=? WHERE id=?")
               ->execute([$_SERVER['REMOTE_ADDR'] ?? '', $admin['id']]);
            json_resp(['success' => true]);
        }
        json_resp(['success' => false, 'msg' => '用户名或密码错误']);
    }

    // 退出
    if ($admin_api == 'logout') {
        $token = $_COOKIE['admin_token'] ?? '';
        $s = get_sessions();
        unset($s[$token]);
        save_sessions($s);
        setcookie('admin_token', '', time() - 3600, '/');
        json_resp(['success' => true]);
    }

    // 以下所有接口需要登录
    $user = check_admin();
    if (!$user) json_resp(['success' => false, 'msg' => '未登录', 'code' => 401]);

    $raw  = file_get_contents('php://input');
    $data = $raw ? json_decode($raw, true) : $_POST;

    switch ($admin_api) {
        case 'stats':
            $stats = [
                'total_cards'    => intval($db->query("SELECT COUNT(*) as c FROM cards")->fetch(PDO::FETCH_ASSOC)['c']),
                'active_cards'   => intval($db->query("SELECT COUNT(*) as c FROM cards WHERE active=1 AND banned=0")->fetch(PDO::FETCH_ASSOC)['c']),
                'online_devices' => intval($db->query("SELECT COUNT(*) as c FROM cards WHERE last_heart > datetime('now','localtime','-5 minutes')")->fetch(PDO::FETCH_ASSOC)['c']),
                'today_logins'   => intval($db->query("SELECT COUNT(*) as c FROM logs WHERE action='activate' AND date(created_at)=date('now','localtime')")->fetch(PDO::FETCH_ASSOC)['c']),
                'total_users'    => intval($db->query("SELECT SUM(total_users) as c FROM cards")->fetch(PDO::FETCH_ASSOC)['c'] ?: 0)
            ];
            json_resp(['success' => true, 'stats' => $stats]);
            break;

        case 'list_cards':
            $page   = intval($_GET['page'] ?? 1);
            $limit  = 20;
            $offset = ($page - 1) * $limit;
            $stmt = $db->prepare("SELECT * FROM cards ORDER BY id DESC LIMIT ? OFFSET ?");
            $stmt->execute([$limit, $offset]);
            $cards = $stmt->fetchAll(PDO::FETCH_ASSOC);
            $total = $db->query("SELECT COUNT(*) as c FROM cards")->fetch(PDO::FETCH_ASSOC)['c'];
            json_resp(['success' => true, 'cards' => $cards, 'total' => intval($total), 'page' => $page]);
            break;

        case 'gen_cards':
            $prefix = $data['prefix'] ?? '';
            $count  = intval($data['count'] ?? 1);
            $dtype  = $data['duration_type'] ?? 'permanent';
            $dval   = intval($data['duration_value'] ?? 0);
            $ctype  = $data['card_type'] ?? 'device';
            $max_c = intval($data['max_concurrent'] ?? 1);
            $label  = $dtype == 'permanent' ? '永久' : $dval . get_duration_label($dtype);
            $keys   = [];
            for ($i = 0; $i < $count; $i++) {
                $key = gen_card_key($prefix);
                $db->prepare("INSERT INTO cards (card_key,prefix,card_type,duration_type,duration_value,duration_label,max_concurrent,created_by) VALUES (?,?,?,?,?,?,?,?)")
                   ->execute([$key, $prefix, $ctype, $dtype, $dval, $label, $max_c, $user]);
                $keys[] = $key;
            }
            json_resp(['success' => true, 'keys' => $keys]);
            break;

        case 'ban_card':
            $id = intval($data['id'] ?? 0);
            $db->prepare("UPDATE cards SET banned=1 WHERE id=?")->execute([$id]);
            json_resp(['success' => true]);
            break;

        case 'unban_card':
            $id = intval($data['id'] ?? 0);
            $db->prepare("UPDATE cards SET banned=0 WHERE id=?")->execute([$id]);
            json_resp(['success' => true]);
            break;

        case 'delete_card':
            $id = intval($data['id'] ?? 0);
            $db->prepare("DELETE FROM cards WHERE id=?")->execute([$id]);
            json_resp(['success' => true]);
            break;

        case 'list_logs':
            $page   = intval($_GET['page'] ?? 1);
            $limit  = 50;
            $offset = ($page - 1) * $limit;
            $stmt = $db->prepare("SELECT * FROM logs ORDER BY id DESC LIMIT ? OFFSET ?");
            $stmt->execute([$limit, $offset]);
            json_resp(['success' => true, 'logs' => $stmt->fetchAll(PDO::FETCH_ASSOC)]);
            break;

        case 'list_anns':
            json_resp(['success' => true, 'announcements' => $db->query("SELECT * FROM announcements ORDER BY id DESC")->fetchAll(PDO::FETCH_ASSOC)]);
            break;

        case 'save_ann':
            $id = intval($data['id'] ?? 0);
            $title   = $data['title'] ?? '';
            $content = $data['content'] ?? '';
            $priority = intval($data['priority'] ?? 0);
            $mode   = $data['mode'] ?? 'banner';
            $start  = $data['start_time'] ?: null;
            $end    = $data['end_time'] ?: null;
            $dismissable = intval($data['dismissable'] ?? 1);
            $active = intval($data['active'] ?? 1);
            if ($id > 0) {
                $stmt = $db->prepare("UPDATE announcements SET title=?,content=?,priority=?,mode=?,start_time=?,end_time=?,dismissable=?,active=?,updated_at=datetime('now','localtime') WHERE id=?");
                $stmt->execute([$title, $content, $priority, $mode, $start, $end, $dismissable, $active, $id]);
            } else {
                $stmt = $db->prepare("INSERT INTO announcements (title,content,priority,mode,start_time,end_time,dismissable,active) VALUES (?,?,?,?,?,?,?,?)");
                $stmt->execute([$title, $content, $priority, $mode, $start, $end, $dismissable, $active]);
            }
            json_resp(['success' => true]);
            break;

        case 'delete_ann':
            $id = intval($data['id'] ?? 0);
            $db->prepare("DELETE FROM announcements WHERE id=?")->execute([$id]);
            json_resp(['success' => true]);
            break;

        case 'get_config':
            $cfg = [];
            foreach ($db->query("SELECT key,value FROM config") as $r) $cfg[$r['key']] = $r['value'];
            json_resp(['success' => true, 'config' => $cfg]);
            break;

        case 'save_config':
            foreach ($data as $k => $v) {
                if (in_array($k, ['force_min_version','latest_version','version_name','download_url','update_notes','app_name'])) {
                    $db->prepare("INSERT OR REPLACE INTO config (key,value,updated_at) VALUES (?,?,datetime('now','localtime'))")->execute([$k, $v]);
                }
            }
            json_resp(['success' => true]);
            break;

        case 'change_password':
            $stmt = $db->prepare("SELECT * FROM admins WHERE username=?");
            $stmt->execute([$user]);
            $admin = $stmt->fetch(PDO::FETCH_ASSOC);
            if (hash('sha256', $data['old_password'] ?? '') != $admin['password_hash']) {
                json_resp(['success' => false, 'msg' => '原密码错误']);
            }
            $db->prepare("UPDATE admins SET password_hash=? WHERE username=?")
               ->execute([hash('sha256', $data['new_password'] ?? ''), $user]);
            json_resp(['success' => true, 'msg' => '密码已修改']);
            break;

        default:
            json_resp(['success' => false, 'msg' => 'Unknown action']);
    }
}

function get_duration_label($type) {
    $m = ['hour'=>'小时','day'=>'天','week'=>'周','month'=>'月','year'=>'年'];
    return $m[$type] ?? '';
}


// ============================================================
// ========== 管理后台页面 (HTML + JS) ==========
// ============================================================
function show_login_page() {
    ?>
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <title>登录 - ImGuiOverlay</title>
        <style>
            * { box-sizing: border-box; }
            body { font-family: sans-serif; background: #1a1a2e; color: #eee; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; }
            .box { background: #16213e; padding: 40px; border-radius: 8px; width: 320px; }
            h2 { text-align: center; margin-bottom: 30px; }
            input { width: 100%; padding: 10px; margin: 10px 0; background: #0f3460; border: 1px solid #533483; color: #eee; border-radius: 4px; box-sizing: border-box; }
            button { width: 100%; padding: 12px; background: #533483; color: #fff; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
            button:hover { background: #7c4dab; }
            .msg { color: #ff6b6b; text-align: center; margin-top: 10px; display: none; }
        </style>
    </head>
    <body>
        <div class="box">
            <h2>🔐 管理后台登录</h2>
            <input id="user" placeholder="用户名" autocomplete="off">
            <input id="pass" type="password" placeholder="密码">
            <button onclick="doLogin()">登录</button>
            <div class="msg" id="msg"></div>
        </div>
        <script>
        function doLogin() {
            var u = document.getElementById("user").value;
            var p = document.getElementById("pass").value;
            fetch("?admin_api=login", {
                method: "POST",
                headers: {"Content-Type": "application/json"},
                body: JSON.stringify({username: u, password: p})
            })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (d.success) location.reload();
                else { document.getElementById("msg").innerText = d.msg; document.getElementById("msg").style.display = "block"; }
            });
        }
        document.getElementById("pass").addEventListener("keypress", function(e) {
            if (e.key === "Enter") doLogin();
        });
        </script>
    </body>
    </html>
    <?php
}

function show_admin_page($username) {
    global $PRESET;
    ?>
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <title>管理后台 - ImGuiOverlay</title>
        <style>
            * { box-sizing: border-box; }
            body { font-family: "Segoe UI", sans-serif; background: #0f0f23; color: #e0e0e0; margin: 0; }
            .header { background: linear-gradient(135deg, #1a1a2e, #16213e); padding: 15px 30px; display: flex; justify-content: space-between; align-items: center; border-bottom: 2px solid #533483; }
            .header h1 { margin: 0; font-size: 20px; }
            .header .user { font-size: 14px; }
            .header button { background: #533483; color: #fff; border: none; padding: 8px 16px; border-radius: 4px; cursor: pointer; }
            .tabs { display: flex; background: #16213e; padding: 0 20px; }
            .tab { padding: 12px 20px; cursor: pointer; border-bottom: 3px solid transparent; color: #888; }
            .tab.active { border-bottom-color: #533483; color: #fff; }
            .content { padding: 20px; }
            .tab-content { display: none; }
            .tab-content.active { display: block; }
            table { width: 100%; border-collapse: collapse; background: #1a1a2e; }
            th, td { padding: 10px; text-align: left; border-bottom: 1px solid #2d2d4a; font-size: 13px; }
            th { background: #16213e; position: sticky; top: 0; }
            tr:hover { background: #1e1e3a; }
            .btn { padding: 6px 12px; border: none; border-radius: 4px; cursor: pointer; font-size: 12px; margin: 2px; }
            .btn-primary { background: #533483; color: #fff; }
            .btn-danger { background: #c0392b; color: #fff; }
            .btn-success { background: #27ae60; color: #fff; }
            .badge { padding: 2px 8px; border-radius: 10px; font-size: 11px; }
            .badge-active { background: #27ae60; }
            .badge-banned { background: #c0392b; }
            .badge-expired { background: #f39c12; }
            input, select, textarea { background: #16213e; border: 1px solid #533483; color: #e0e0e0; padding: 8px; border-radius: 4px; }
            .card { background: #1a1a2e; padding: 20px; border-radius: 8px; margin-bottom: 20px; }
            .stat-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }
            .stat-card { background: linear-gradient(135deg, #16213e, #1a1a2e); padding: 20px; border-radius: 8px; border-left: 4px solid #533483; }
            .stat-card .num { font-size: 28px; font-weight: bold; color: #533483; }
            .stat-card .label { font-size: 13px; color: #888; }
            .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.7); z-index: 1000; }
            .modal.show { display: flex; align-items: center; justify-content: center; }
            .modal-box { background: #1a1a2e; padding: 30px; border-radius: 8px; min-width: 400px; max-width: 600px; max-height: 80vh; overflow-y: auto; }
            .form-group { margin-bottom: 15px; }
            .form-group label { display: block; margin-bottom: 5px; color: #888; }
            .close { float: right; cursor: pointer; font-size: 20px; color: #888; }
            #msg-toast { position: fixed; top: 20px; right: 20px; background: #4CAF50; color: white; padding: 12px 20px; border-radius: 4px; z-index: 9999; display: none; }
        </style>
    </head>
    <body>
        <div id="msg-toast"></div>
        <div class="header">
            <h1>🛡️ ImGuiOverlay 管理后台</h1>
            <div class="user">👤 <?php echo htmlspecialchars($username); ?> <button onclick="doLogout()">退出</button></div>
        </div>
        <div class="tabs">
            <div class="tab active" onclick="switchTab('stats', this)">📊 统计</div>
            <div class="tab" onclick="switchTab('gen', this)">➕ 生成卡密</div>
            <div class="tab" onclick="switchTab('cards', this)">🃏 卡密管理</div>
            <div class="tab" onclick="switchTab('anns', this)">📢 公告</div>
            <div class="tab" onclick="switchTab('update', this)">🔄 更新</div>
            <div class="tab" onclick="switchTab('logs', this)">📝 日志</div>
            <div class="tab" onclick="switchTab('settings', this)">⚙️ 设置</div>
        </div>
        <div class="content">
            <!-- 统计 -->
            <div class="tab-content active" id="tab-stats"></div>

            <!-- 生成卡密 -->
            <div class="tab-content" id="tab-gen">
                <div class="card">
                    <h3>生成卡密</h3>
                    <div class="form-group"><label>前缀 (可选)</label><input id="gen_prefix" placeholder="VIP"></div>
                    <div class="form-group"><label>数量</label><input id="gen_count" type="number" value="1" min="1" max="100"></div>
                    <div class="form-group">
                        <label>卡类型</label>
                        <select id="gen_type" onchange="document.getElementById('gen_max_group').style.display = this.value === 'public' ? 'block' : 'none'">
                            <option value="device">设备绑定卡</option>
                            <option value="public">公用卡</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <label>时长</label>
                        <select id="gen_duration">
                            <?php foreach ($PRESET as $d): ?>
                            <option value="<?php echo $d[0]; ?>,<?php echo $d[1]; ?>"><?php echo $d[2]; ?></option>
                            <?php endforeach; ?>
                        </select>
                    </div>
                    <div class="form-group" id="gen_max_group" style="display:none;">
                        <label>公用卡最大并发 (0=不限制)</label>
                        <input id="gen_max" type="number" value="1" min="0">
                    </div>
                    <button class="btn btn-primary" onclick="genCards()">生成</button>
                    <pre id="gen_result" style="margin-top:15px;background:#0f0f23;padding:15px;border-radius:4px;display:none"></pre>
                </div>
            </div>

            <!-- 卡密管理 -->
            <div class="tab-content" id="tab-cards">
                <div class="card">
                    <table id="cards_table">
                        <thead>
                            <tr><th>ID</th><th>卡密</th><th>类型</th><th>时长</th><th>设备</th><th>到期</th><th>状态</th><th>操作</th></tr>
                        </thead>
                        <tbody></tbody>
                    </table>
                    <div id="cards_pager" style="margin-top:15px;text-align:center;"></div>
                </div>
            </div>

            <!-- 公告 -->
            <div class="tab-content" id="tab-anns">
                <div class="card"><button class="btn btn-primary" onclick="showAnnModal()">+ 新建公告</button></div>
                <div class="card">
                    <table id="anns_table">
                        <thead><tr><th>ID</th><th>标题</th><th>优先级</th><th>模式</th><th>时间</th><th>状态</th><th>操作</th></tr></thead>
                        <tbody></tbody>
                    </table>
                </div>
            </div>

            <!-- 更新 -->
            <div class="tab-content" id="tab-update">
                <div class="card">
                    <h3>版本配置</h3>
                    <div class="form-group"><label>最低版本 (强制更新)</label><input id="cfg_force_min" type="number" value="0"></div>
                    <div class="form-group"><label>最新版本号</label><input id="cfg_latest_ver" type="number" value="0"></div>
                    <div class="form-group"><label>版本名称</label><input id="cfg_ver_name" placeholder="v2.38"></div>
                    <div class="form-group"><label>下载地址</label><input id="cfg_dl_url" placeholder="http://..."></div>
                    <div class="form-group"><label>更新说明</label><textarea id="cfg_notes" rows="3" style="width:100%"></textarea></div>
                    <button class="btn btn-primary" onclick="saveConfig()">保存</button>
                </div>
            </div>

            <!-- 日志 -->
            <div class="tab-content" id="tab-logs">
                <div class="card">
                    <table id="logs_table">
                        <thead><tr><th>ID</th><th>卡密</th><th>设备</th><th>类型</th><th>动作</th><th>详情</th><th>时间</th></tr></thead>
                        <tbody></tbody>
                    </table>
                </div>
            </div>

            <!-- 设置 -->
            <div class="tab-content" id="tab-settings">
                <div class="card">
                    <h3>修改密码</h3>
                    <div class="form-group"><label>原密码</label><input id="old_pass" type="password"></div>
                    <div class="form-group"><label>新密码</label><input id="new_pass" type="password"></div>
                    <button class="btn btn-primary" onclick="changePass()">修改密码</button>
                </div>
            </div>
        </div>

        <!-- 公告编辑弹窗 -->
        <div class="modal" id="ann_modal">
            <div class="modal-box">
                <span class="close" onclick="closeAnnModal()">&times;</span>
                <h3>编辑公告</h3>
                <input id="ann_id" type="hidden">
                <div class="form-group"><label>标题</label><input id="ann_title"></div>
                <div class="form-group"><label>内容</label><textarea id="ann_content" rows="4" style="width:100%"></textarea></div>
                <div class="form-group">
                    <label>优先级</label>
                    <select id="ann_priority">
                        <option value="0">普通</option>
                        <option value="1">重要</option>
                        <option value="2">紧急</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>显示模式</label>
                    <select id="ann_mode">
                        <option value="banner">横幅</option>
                        <option value="popup">弹窗</option>
                    </select>
                </div>
                <div class="form-group"><label>开始时间 (留空=立即)</label><input id="ann_start" type="datetime-local"></div>
                <div class="form-group"><label>结束时间 (留空=永久)</label><input id="ann_end" type="datetime-local"></div>
                <div class="form-group"><label><input id="ann_dismiss" type="checkbox" checked> 用户可关闭</label></div>
                <div class="form-group"><label><input id="ann_active" type="checkbox" checked> 启用</label></div>
                <button class="btn btn-primary" onclick="saveAnn()">保存</button>
            </div>
        </div>

        <script>
        // ========== 工具函数 ==========
        function showMsg(s, isErr) {
            var el = document.getElementById("msg-toast");
            el.innerText = s;
            el.style.background = isErr ? "#c0392b" : "#4CAF50";
            el.style.display = "block";
            setTimeout(function() { el.style.display = "none"; }, 2500);
        }

        function apiPost(action, data, cb) {
            fetch("?admin_api=" + action, {
                method: "POST",
                headers: {"Content-Type": "application/json"},
                body: JSON.stringify(data || {})
            })
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (d.code === 401) { alert("登录已过期，请重新登录"); location.reload(); return; }
                if (cb) cb(d);
            })
            .catch(function(e) { alert("请求失败: " + e.message); });
        }

        function switchTab(name, el) {
            var tabs = document.querySelectorAll(".tab");
            for (var i = 0; i < tabs.length; i++) tabs[i].classList.remove("active");
            var contents = document.querySelectorAll(".tab-content");
            for (var i = 0; i < contents.length; i++) contents[i].classList.remove("active");
            if (el) el.classList.add("active");
            document.getElementById("tab-" + name).classList.add("active");
            if (name === "stats")    loadStats();
            if (name === "cards")    loadCards(1);
            if (name === "anns")     loadAnns();
            if (name === "logs")     loadLogs(1);
            if (name === "update")   loadConfig();
        }

        // ========== 统计 ==========
        function loadStats() {
            apiPost("stats", null, function(d) {
                if (!d.success) return;
                document.getElementById("tab-stats").innerHTML =
                    '<div class="stat-grid">' +
                    '<div class="stat-card"><div class="num">' + d.stats.total_cards + '</div><div class="label">总卡密</div></div>' +
                    '<div class="stat-card"><div class="num">' + d.stats.active_cards + '</div><div class="label">有效</div></div>' +
                    '<div class="stat-card"><div class="num">' + d.stats.online_devices + '</div><div class="label">在线</div></div>' +
                    '<div class="stat-card"><div class="num">' + d.stats.today_logins + '</div><div class="label">今日激活</div></div>' +
                    '<div class="stat-card"><div class="num">' + d.stats.total_users + '</div><div class="label">累计人次</div></div>' +
                    '</div>';
            });
        }

        // ========== 卡密列表 ==========
        function loadCards(page) {
            fetch("?admin_api=list_cards&page=" + page)
            .then(function(r) { return r.json(); })
            .then(function(d) {
                if (!d.success) return;
                var h = "";
                for (var i = 0; i < d.cards.length; i++) {
                    var c = d.cards[i];
                    var badge = "";
                    if (c.banned) badge = '<span class="badge badge-banned">封禁</span>';
                    else if (c.expire_at && new Date(c.expire_at) < new Date()) badge = '<span class="badge badge-expired">过期</span>';
                    else badge = '<span class="badge badge-active">正常</span>';
                    var acts = "";
                    if (c.banned)
                        acts = '<button class="btn btn-success" onclick="unbanCard(' + c.id + ')">解封</button> ';
                    else
                        acts = '<button class="btn btn-danger" onclick="banCard(' + c.id + ')">封禁</button> ';
                    acts += '<button class="btn btn-danger" onclick="deleteCard(' + c.id + ')">删除</button>';
                    var devShort = (c.device_id || "-").length > 12 ? (c.device_id || "-").substr(0, 12) + "..." : (c.device_id || "-");
                    h += "<tr>" +
                        "<td>" + c.id + "</td>" +
                        "<td><code>" + c.card_key + "</code></td>" +
                        "<td>" + (c.card_type === "public" ? "公用" : "设备") + "</td>" +
                        "<td>" + c.duration_label + "</td>" +
                        "<td>" + devShort + "</td>" +
                        "<td>" + (c.expire_at || "永久") + "</td>" +
                        "<td>" + badge + "</td>" +
                        "<td>" + acts + "</td>" +
                        "</tr>";
                }
                document.querySelector("#cards_table tbody").innerHTML = h;
                var totalPages = Math.ceil(d.total / 20);
                var p = "";
                for (var i = 1; i <= totalPages; i++) {
                    p += '<button class="btn ' + (i === page ? "btn-primary" : "") + '" onclick="loadCards(' + i + ')">' + i + '</button> ';
                }
                document.getElementById("cards_pager").innerHTML = p;
            });
        }

        function genCards() {
            var sel = document.getElementById("gen_duration").value;
            var parts = sel.split(",");
            apiPost("gen_cards", {
                action: "gen_cards",
                prefix: document.getElementById("gen_prefix").value,
                count: parseInt(document.getElementById("gen_count").value) || 1,
                duration_type: parts[0],
                duration_value: parseInt(parts[1]) || 0,
                card_type: document.getElementById("gen_type").value,
                max_concurrent: parseInt(document.getElementById("gen_max").value) || 1
            }, function(d) {
                if (d.success) {
                    document.getElementById("gen_result").style.display = "block";
                    document.getElementById("gen_result").innerText = d.keys.join("\n");
                    loadCards(1);
                }
            });
        }

        function banCard(id) {
            if (!confirm("确认封禁?")) return;
            apiPost("ban_card", {id: id}, function(d) { if (d.success) { showMsg("已封禁"); loadCards(1); } });
        }
        function unbanCard(id) {
            if (!confirm("确认解封?")) return;
            apiPost("unban_card", {id: id}, function(d) { if (d.success) { showMsg("已解封"); loadCards(1); } });
        }
        function deleteCard(id) {
            if (!confirm("确认删除?")) return;
            apiPost("delete_card", {id: id}, function(d) { if (d.success) { showMsg("已删除"); loadCards(1); } });
        }

        // ========== 公告 ==========
        function loadAnns() {
            apiPost("list_anns", null, function(d) {
                var h = "";
                for (var i = 0; i < d.announcements.length; i++) {
                    var a = d.announcements[i];
                    var prio = ["普通","重要","紧急"][a.priority] || "普通";
                    var timeStr = (a.start_time || "即时") + " ~ " + (a.end_time || "永久");
                    h += "<tr>" +
                        "<td>" + a.id + "</td>" +
                        "<td>" + escapeHtml(a.title) + "</td>" +
                        "<td>" + prio + "</td>" +
                        "<td>" + a.mode + "</td>" +
                        "<td>" + timeStr + "</td>" +
                        "<td>" + (a.active ? "🟢" : "🔴") + "</td>" +
                        "<td><button class=\"btn btn-primary\" onclick=\"editAnn(" + a.id + ")\">编辑</button> <button class=\"btn btn-danger\" onclick=\"deleteAnn(" + a.id + ")\">删除</button></td>" +
                        "</tr>";
                }
                document.querySelector("#anns_table tbody").innerHTML = h;
            });
        }

        function showAnnModal() {
            document.getElementById("ann_id").value = "";
            document.getElementById("ann_title").value = "";
            document.getElementById("ann_content").value = "";
            document.getElementById("ann_priority").value = "0";
            document.getElementById("ann_mode").value = "banner";
            document.getElementById("ann_start").value = "";
            document.getElementById("ann_end").value = "";
            document.getElementById("ann_dismiss").checked = true;
            document.getElementById("ann_active").checked = true;
            document.getElementById("ann_modal").classList.add("show");
        }
        function closeAnnModal() { document.getElementById("ann_modal").classList.remove("show"); }

        function editAnn(id) {
            // 简单实现: 弹窗提示输入 ID, 实际应加载数据
            alert("公告编辑功能: ID=" + id + "\n请手动在数据库或直接修改");
        }

        function saveAnn() {
            var data = {
                action: "save_ann",
                id: document.getElementById("ann_id").value || 0,
                title: document.getElementById("ann_title").value,
                content: document.getElementById("ann_content").value,
                priority: document.getElementById("ann_priority").value,
                mode: document.getElementById("ann_mode").value,
                start_time: document.getElementById("ann_start").value || null,
                end_time: document.getElementById("ann_end").value || null,
                dismissable: document.getElementById("ann_dismiss").checked ? 1 : 0,
                active: document.getElementById("ann_active").checked ? 1 : 0
            };
            apiPost("save_ann", data, function(d) { if (d.success) { closeAnnModal(); showMsg("保存成功"); loadAnns(); } });
        }

        function deleteAnn(id) {
            if (!confirm("确认删除?")) return;
            apiPost("delete_ann", {id: id}, function() { showMsg("已删除"); loadAnns(); });
        }

        // ========== 配置 ==========
        function loadConfig() {
            apiPost("get_config", null, function(d) {
                if (!d.success || !d.config) return;
                var c = d.config;
                document.getElementById("cfg_force_min").value = c.force_min_version || "0";
                document.getElementById("cfg_latest_ver").value = c.latest_version || "0";
                document.getElementById("cfg_ver_name").value = c.version_name || "";
                document.getElementById("cfg_dl_url").value = c.download_url || "";
                document.getElementById("cfg_notes").value = c.update_notes || "";
            });
        }

        function saveConfig() {
            var data = {
                action: "save_config",
                force_min_version: document.getElementById("cfg_force_min").value,
                latest_version: document.getElementById("cfg_latest_ver").value,
                version_name: document.getElementById("cfg_ver_name").value,
                download_url: document.getElementById("cfg_dl_url").value,
                update_notes: document.getElementById("cfg_notes").value
            };
            apiPost("save_config", data, function(d) { if (d.success) showMsg("配置已保存"); });
        }

        // ========== 日志 ==========
        function loadLogs(page) {
            fetch("?admin_api=list_logs&page=" + page)
            .then(function(r) { return r.json(); })
            .then(function(d) {
                var h = "";
                for (var i = 0; i < d.logs.length; i++) {
                    var l = d.logs[i];
                    var devShort = (l.device_id || "-").length > 16 ? (l.device_id || "-").substr(0, 16) + "..." : (l.device_id || "-");
                    h += "<tr>" +
                        "<td>" + l.id + "</td>" +
                        "<td>" + escapeHtml(l.card_key || "-") + "</td>" +
                        "<td>" + devShort + "</td>" +
                        "<td>" + (l.card_type || "-") + "</td>" +
                        "<td>" + l.action + "</td>" +
                        "<td>" + escapeHtml(l.detail || "-") + "</td>" +
                        "<td>" + l.created_at + "</td>" +
                        "</tr>";
                }
                document.querySelector("#logs_table tbody").innerHTML = h;
            });
        }

        // ========== 修改密码 ==========
        function changePass() {
            var data = {
                action: "change_password",
                old_password: document.getElementById("old_pass").value,
                new_password: document.getElementById("new_pass").value
            };
            apiPost("change_password", data, function(d) {
                if (d.success) alert("密码已修改");
                else alert(d.msg || "修改失败");
            });
        }

        // ========== 退出 ==========
        function doLogout() {
            apiPost("logout", {}, function() { location.reload(); });
        }

        // ========== 工具 ==========
        function escapeHtml(s) {
            var div = document.createElement("div");
            div.appendChild(document.createTextNode(s));
            return div.innerHTML;
        }

        // ========== 初始化 ==========
        loadStats();
        setInterval(function() {
            var statsTab = document.getElementById("tab-stats");
            if (statsTab && statsTab.classList.contains("active")) loadStats();
        }, 10000);
        </script>
    </body>
    </html>
    <?php
}
