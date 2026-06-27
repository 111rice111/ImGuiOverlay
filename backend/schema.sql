-- ImGuiOverlay 卡密系统数据库
-- 在 MySQL 中执行: mysql -u root -p < schema.sql

CREATE DATABASE IF NOT EXISTS overlay CHARACTER SET utf8mb4;
USE overlay;

-- 卡密表
CREATE TABLE cards (
    id INT AUTO_INCREMENT PRIMARY KEY,
    card_key VARCHAR(64) NOT NULL UNIQUE,     -- 卡密
    device_id VARCHAR(64) DEFAULT NULL,        -- 绑定设备
    hwid VARCHAR(128) DEFAULT NULL,            -- 硬件指纹
    token VARCHAR(64) DEFAULT NULL,            -- 会话 token
    banned TINYINT DEFAULT 0,                  -- 封禁标记
    expire_at DATETIME DEFAULT NULL,           -- 到期时间 (NULL=永久)
    activated_at DATETIME DEFAULT NULL,        -- 激活时间
    last_heart DATETIME DEFAULT NULL,          -- 最后心跳
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    INDEX idx_key (card_key),
    INDEX idx_device (device_id)
) ENGINE=InnoDB;

-- 版本表
CREATE TABLE versions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    version INT NOT NULL,                      -- 版本号
    url VARCHAR(512) DEFAULT '',               -- 下载地址
    md5 VARCHAR(64) DEFAULT '',                -- 文件校验
    changelog TEXT,                            -- 更新日志
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 远程指令表
CREATE TABLE commands (
    id INT AUTO_INCREMENT PRIMARY KEY,
    device_id VARCHAR(64) DEFAULT '*',         -- * 表示全部设备
    cmd VARCHAR(64) NOT NULL,                  -- 指令名
    params JSON DEFAULT NULL,                  -- 参数
    executed TINYINT DEFAULT 0,                -- 是否已执行
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 操作日志
CREATE TABLE logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    card_id INT DEFAULT NULL,
    device_id VARCHAR(64) DEFAULT NULL,
    action VARCHAR(32) NOT NULL,               -- login/heartbeat/banned
    ip VARCHAR(64) DEFAULT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- 插入示例卡密
INSERT INTO cards (card_key, expire_at) VALUES ('DEMO-KEY-001', DATE_ADD(NOW(), INTERVAL 30 DAY));
INSERT INTO cards (card_key) VALUES ('VIP-UNLIMITED-2024');
