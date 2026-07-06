#!/data/data/com.termux/files/usr/bin/bash
echo "[1/3] 复制文件..."
cp /sdcard/server.py ~/server.py
cp /sdcard/overlay.db ~/overlay.db
echo "[2/3] 检查文件..."
ls -la ~/server.py ~/overlay.db
echo "[3/3] 启动服务器..."
cd ~
python server.py
