<?php
/**
 * ImGuiOverlay 卡密验证服务端
 * 部署: 上传到 PHP 虚拟主机 (需 MySQL)
 * 数据库: 见 schema.sql
 */

header('Content-Type: text/plain');
define('AES_KEY', hex2bin('2b7e151628aed2a6abf7158809cf4f3c'));
define('AES_IV',  hex2bin('000102030405060708090a0b0c0d0e0f'));

// AES-128-CBC 解密客户端请求, 加密响应
function decrypt($hex) {
    $data = hex2bin($hex);
    $dec = openssl_decrypt($data, 'AES-128-CBC', AES_KEY, OPENSSL_RAW_DATA, AES_IV);
    return $dec;
}
function encrypt($plain) {
    $enc = openssl_encrypt($plain, 'AES-128-CBC', AES_KEY, OPENSSL_RAW_DATA, AES_IV);
    return bin2hex($enc);
}

// 数据库连接
$db = new mysqli('localhost', 'root', 'your_password', 'overlay');
if ($db->connect_error) die(encrypt(json_encode(['ok'=>false,'msg'=>'DB error'])));

$path = $_SERVER['REQUEST_URI'];
$path = parse_url($path, PHP_URL_PATH);
$body = file_get_contents('php://input');
$data = json_decode(decrypt($body), true) ?: [];

// ========== API 路由 ==========

if ($path === '/api/verify') {
    // 卡密验证
    $key = $db->real_escape_string($data['key'] ?? '');
    $dev = $db->real_escape_string($data['device_id'] ?? '');
    $hw  = $db->real_escape_string($data['hwid'] ?? '');
    $ver = (int)($data['version'] ?? 0);

    $row = $db->query("SELECT * FROM cards WHERE card_key='$key' LIMIT 1")->fetch_assoc();
    if (!$row) respond(false, '卡密不存在');
    if ($row['banned'])    respond(false, '卡密已被封禁');
    if ($row['expire_at'] && strtotime($row['expire_at']) < time()) respond(false, '卡密已过期');

    // 设备绑定
    if ($row['device_id'] && $row['device_id'] !== $dev) {
        respond(false, '该卡密已绑定其他设备');
    }
    if (!$row['device_id']) {
        $db->query("UPDATE cards SET device_id='$dev', hwid='$hw', activated_at=NOW() WHERE id={$row['id']}");
    }

    // 生成 token
    $token = bin2hex(random_bytes(16));
    $expire = $row['expire_at'] ?: date('Y-m-d H:i:s', time() + 86400*365);
    $db->query("UPDATE cards SET token='$token', last_heart=NOW() WHERE id={$row['id']}");
    $db->query("INSERT INTO logs (card_id, device_id, action, ip) VALUES ({$row['id']},'$dev','login','{$_SERVER['REMOTE_ADDR']}')");

    respond(true, 'ok', ['token'=>$token, 'expire'=>$expire]);
}

elseif ($path === '/api/heartbeat') {
    // 心跳
    $dev = $db->real_escape_string($data['device_id'] ?? '');
    $tok = $db->real_escape_string($data['token'] ?? '');
    $row = $db->query("SELECT * FROM cards WHERE device_id='$dev' AND token='$tok' LIMIT 1")->fetch_assoc();
    if (!$row) respond(false, 'invalid', ['banned'=>true]);
    if ($row['banned']) respond(false, 'banned', ['banned'=>true]);
    $db->query("UPDATE cards SET last_heart=NOW() WHERE id={$row['id']}");
    respond(true, 'ok');
}

elseif ($path === '/api/version') {
    // 版本检测
    $current = (int)($data['version'] ?? 0);
    $row = $db->query("SELECT * FROM versions ORDER BY id DESC LIMIT 1")->fetch_assoc();
    if ($row && (int)$row['version'] > $current) {
        respond(true, 'update', [
            'available'=>true, 'version'=>(int)$row['version'],
            'url'=>$row['url'], 'md5'=>$row['md5'], 'changelog'=>$row['changelog']
        ]);
    }
    respond(true, 'ok', ['available'=>false]);
}

elseif ($path === '/api/command') {
    // 远程指令
    $dev = $db->real_escape_string($data['device_id'] ?? '');
    $row = $db->query("SELECT * FROM commands WHERE device_id='$dev' AND executed=0 ORDER BY id LIMIT 1")->fetch_assoc();
    if ($row) {
        $db->query("UPDATE commands SET executed=1 WHERE id={$row['id']}");
        respond(true, 'cmd', ['cmd'=>$row['cmd'], 'params'=>json_decode($row['params']??'{}',true)]);
    }
    respond(true, 'ok', ['cmd'=>'']);
}

else {
    respond(false, 'unknown endpoint');
}

function respond($ok, $msg, $extra=[]) {
    global $db;
    echo encrypt(json_encode(array_merge(['ok'=>$ok,'msg'=>$msg], $extra)));
    $db->close();
    exit;
}
