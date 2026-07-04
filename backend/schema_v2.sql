-- =========================================================
-- ImGuiOverlay 卡密验证系统 v3.0 — 数据库结构
-- 支持: SQLite (开发/小规模) 和 MySQL (生产/腾讯云)
-- =========================================================

-- 卡密表 (完全重构)
CREATE TABLE cards (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    card_key        TEXT NOT NULL UNIQUE,        -- 卡密字符串
    prefix          TEXT DEFAULT '',              -- 前缀 (如 VIP/MONTH)
    card_type       TEXT DEFAULT 'device',        -- 'device' 设备绑定 / 'public' 公用卡
    device_id       TEXT DEFAULT NULL,            -- 绑定设备ID (device卡)
    hwid            TEXT DEFAULT NULL,            -- 硬件指纹
    token           TEXT DEFAULT NULL,            -- 当前会话 token (device卡)
    banned          INTEGER DEFAULT 0,            -- 是否封禁
    active          INTEGER DEFAULT 1,            -- 是否启用 (可用于禁用)
    
    -- 时长系统
    duration_type   TEXT DEFAULT 'permanent',     -- hour/day/week/month/year/permanent/custom
    duration_value  INTEGER DEFAULT 0,            -- 时长数值 (0=永久, 1/3/6/12/24等)
    duration_label  TEXT DEFAULT '永久',           -- 显示标签
    
    -- 时间字段
    expire_at       TEXT DEFAULT NULL,            -- 到期时间 (NULL=永久)
    activated_at    TEXT DEFAULT NULL,            -- 首次激活时间
    last_heart      TEXT DEFAULT NULL,            -- 最后心跳时间
    created_at      TEXT DEFAULT (datetime('now','localtime')),
    created_by      TEXT DEFAULT 'admin',         -- 创建者
    
    -- 公用卡字段
    max_concurrent  INTEGER DEFAULT 1,            -- 最大并发使用数 (0=不限制)
    concurrent_count INTEGER DEFAULT 0,           -- 当前并发数
    first_user_at   TEXT DEFAULT NULL,            -- 首次使用时间
    last_user_at    TEXT DEFAULT NULL,             -- 最近使用时间
    total_users     INTEGER DEFAULT 0             -- 累计使用人次
);

-- 公用卡会话追踪表
CREATE TABLE public_sessions (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    card_id         INTEGER NOT NULL,
    device_id       TEXT NOT NULL,
    device_name     TEXT DEFAULT '',              -- 设备名/备注
    token           TEXT DEFAULT NULL,
    ip              TEXT DEFAULT NULL,
    started_at      TEXT DEFAULT (datetime('now','localtime')),
    last_heart      TEXT DEFAULT NULL,
    heartbeat_count INTEGER DEFAULT 0,
    active          INTEGER DEFAULT 1,
    FOREIGN KEY (card_id) REFERENCES cards(id)
);
CREATE INDEX idx_ps_card ON public_sessions(card_id, active);

-- 公告表 (增强版)
CREATE TABLE announcements (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    title           TEXT DEFAULT '',              -- 公告标题
    content         TEXT NOT NULL,                -- 公告正文
    priority        INTEGER DEFAULT 0,            -- 0=普通 1=重要 2=紧急
    mode            TEXT DEFAULT 'banner',        -- 'banner' 横幅 / 'popup' 弹窗强制
    start_time      TEXT DEFAULT NULL,            -- 生效开始时间
    end_time        TEXT DEFAULT NULL,             -- 生效结束时间
    active          INTEGER DEFAULT 1,            -- 是否启用
    dismissable     INTEGER DEFAULT 1,            -- 是否可关闭 (0=不可关闭/popup模式)
    sort_order      INTEGER DEFAULT 0,            -- 排序权重
    created_at      TEXT DEFAULT (datetime('now','localtime')),
    updated_at      TEXT DEFAULT (datetime('now','localtime'))
);

-- 公告已读记录 (客户端维度)
CREATE TABLE announcement_reads (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    ann_id          INTEGER NOT NULL,
    device_id       TEXT NOT NULL,
    dismissed       INTEGER DEFAULT 1,            -- 是否已关闭
    read_at         TEXT DEFAULT (datetime('now','localtime')),
    UNIQUE(ann_id, device_id),
    FOREIGN KEY (ann_id) REFERENCES announcements(id)
);

-- 版本管理表 (增强版)
CREATE TABLE versions (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    version_code    INTEGER NOT NULL,             -- 数字版本号 (如 238)
    version_name    TEXT NOT NULL,                -- 显示版本名 (如 v2.38)
    force_min_ver   INTEGER DEFAULT 0,            -- 强制更新最低版本号 (低于此强制更新)
    download_url    TEXT DEFAULT '',              -- 下载链接
    md5             TEXT DEFAULT '',              -- 文件校验
    changelog       TEXT DEFAULT '',              -- 更新日志
    file_size       INTEGER DEFAULT 0,            -- 文件大小 (bytes)
    platform        TEXT DEFAULT 'android',       -- 平台
    active          INTEGER DEFAULT 1,
    created_at      TEXT DEFAULT (datetime('now','localtime'))
);

-- 当前有效版本配置 (单行)
CREATE TABLE version_config (
    key             TEXT PRIMARY KEY,             -- 'force_min', 'latest', 'url', 'notes'
    value           TEXT NOT NULL
);

-- 操作日志表 (增强版)
CREATE TABLE logs (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    card_id         INTEGER DEFAULT NULL,
    card_key        TEXT DEFAULT NULL,            -- 冗余卡密前缀
    device_id       TEXT DEFAULT NULL,
    card_type       TEXT DEFAULT NULL,            -- device/public
    action          TEXT NOT NULL,                -- login/heartbeat/logout/ban/unban/activate/gen
    detail          TEXT DEFAULT NULL,            -- 附加信息
    ip              TEXT DEFAULT NULL,
    user_agent      TEXT DEFAULT NULL,
    created_at      TEXT DEFAULT (datetime('now','localtime'))
);
CREATE INDEX idx_logs_card ON logs(card_id);
CREATE INDEX idx_logs_device ON logs(device_id);
CREATE INDEX idx_logs_time ON logs(created_at);

-- 管理员账号表
CREATE TABLE admins (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL UNIQUE,
    password_hash   TEXT NOT NULL,                -- SHA256 hash
    role            TEXT DEFAULT 'admin',         -- admin/superadmin
    last_login      TEXT DEFAULT NULL,
    login_ip        TEXT DEFAULT NULL,
    active          INTEGER DEFAULT 1,
    created_at      TEXT DEFAULT (datetime('now','localtime'))
);

-- 初始超级管理员密码: admin / admin888
INSERT OR IGNORE INTO admins (username, password_hash, role)
VALUES ('admin', '5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8', 'superadmin');

-- 配置表 (任意键值对)
CREATE TABLE config (
    key             TEXT PRIMARY KEY,
    value           TEXT NOT NULL,
    updated_at      TEXT DEFAULT (datetime('now','localtime'))
);

-- 默认配置
INSERT OR IGNORE INTO config VALUES ('force_min_version', '0', datetime('now','localtime'));
INSERT OR IGNORE INTO config VALUES ('latest_version', '0', datetime('now','localtime'));
INSERT OR IGNORE INTO config VALUES ('version_name', '', datetime('now','localtime'));
INSERT OR IGNORE INTO config VALUES ('download_url', '', datetime('now','localtime'));
INSERT OR IGNORE INTO config VALUES ('update_notes', '', datetime('now','localtime'));
INSERT OR IGNORE INTO config VALUES ('app_name', 'ImGuiOverlay', datetime('now','localtime'));

-- 插入示例卡密
INSERT OR IGNORE INTO cards (card_key, prefix, card_type, duration_type, duration_label)
VALUES ('DEMO-KEY-001', 'DEMO', 'device', 'permanent', '永久');
INSERT OR IGNORE INTO cards (card_key, prefix, card_type, duration_type, duration_label)
VALUES ('PUBLIC-TEST-001', 'PUBLIC', 'public', 'day', '1天');
