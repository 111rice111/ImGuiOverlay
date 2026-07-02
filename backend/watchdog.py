"""
ImGuiOverlay 自动恢复看门狗
============================
独立进程, 监控并自动恢复 server.py + bore.exe
启动: python watchdog.py
"""

import os, sys, time, subprocess, signal, atexit
from datetime import datetime

SCRIPT_DIR  = os.path.dirname(os.path.abspath(__file__))
SERVER_PY   = os.path.join(SCRIPT_DIR, "server.py")
BORE_EXE    = os.path.join(SCRIPT_DIR, "..", "bore.exe")
BORE_PORT   = "3699"
LOCAL_PORT  = "8080"
CHECK_SEC   = 30
MAX_RESTART = 5          # 1小时内最多重启次数
RESTART_WINDOW = 3600    # 1小时窗口

PYTHON_EXE  = sys.executable

processes = {}
restart_count = 0
restart_window_start = time.time()
log_file = os.path.join(SCRIPT_DIR, "watchdog.log")


def log(msg: str):
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    line = f"[{ts}] {msg}"
    print(line)
    with open(log_file, "a", encoding="utf-8") as f:
        f.write(line + "\n")


def start_process(name: str, cmd: list, cwd: str = None) -> subprocess.Popen:
    """启动子进程 (不等待)"""
    try:
        p = subprocess.Popen(
            cmd,
            cwd=cwd or SCRIPT_DIR,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0
        )
        log(f"✓ 启动 {name} (PID={p.pid})")
        return p
    except Exception as e:
        log(f"✗ 启动 {name} 失败: {e}")
        return None


def stop_process(name: str):
    """停止子进程"""
    p = processes.get(name)
    if p and p.poll() is None:
        try:
            p.terminate()
            time.sleep(1)
            if p.poll() is None:
                p.kill()
            log(f"✗ 停止 {name}")
        except Exception:
            pass
    processes[name] = None


def check_health(name: str, url: str, timeout: int = 5) -> bool:
    """HTTP 健康检查"""
    import urllib.request
    try:
        req = urllib.request.Request(url, method="GET")
        resp = urllib.request.urlopen(req, timeout=timeout)
        return resp.status == 200
    except Exception:
        return False


def can_restart() -> bool:
    """检查是否在重启频率限制内"""
    global restart_count, restart_window_start
    now = time.time()
    if now - restart_window_start > RESTART_WINDOW:
        restart_count = 0
        restart_window_start = now
    if restart_count >= MAX_RESTART:
        log(f"⚠ 1小时内重启 {restart_count} 次, 超过上限 {MAX_RESTART}, 暂停自动恢复")
        return False
    restart_count += 1
    return True


def main():
    log("========== 看门狗启动 ==========")
    atexit.register(lambda: log("========== 看门狗退出 =========="))

    # 启动服务
    processes["server"] = start_process("server.py", [PYTHON_EXE, SERVER_PY])
    time.sleep(3)

    if os.path.exists(BORE_EXE):
        processes["bore"] = start_process(
            "bore.exe",
            [BORE_EXE, "local", LOCAL_PORT, "--to", "bore.pub", "--port", BORE_PORT],
            cwd=os.path.dirname(BORE_EXE)
        )
    else:
        log(f"⚠ bore.exe 未找到: {BORE_EXE}")

    while True:
        time.sleep(CHECK_SEC)

        # 检查 server.py
        server_ok = False
        p = processes.get("server")
        if p and p.poll() is not None:
            log(f"⚠ server.py 进程退出 (exitcode={p.returncode})")
            server_ok = False
        else:
            server_ok = check_health("server", f"http://127.0.0.1:{LOCAL_PORT}/api/status")

        if not server_ok and can_restart():
            log("🔄 正在重启 server.py...")
            stop_process("server")
            time.sleep(2)
            # 清理残留端口
            try: subprocess.run(f"netstat -ano | findstr :{LOCAL_PORT}", shell=True, timeout=5)
            except: pass
            processes["server"] = start_process("server.py", [PYTHON_EXE, SERVER_PY])
            time.sleep(3)

        # 检查 bore.exe
        if os.path.exists(BORE_EXE):
            bore_ok = False
            p = processes.get("bore")
            if p and p.poll() is not None:
                log(f"⚠ bore.exe 进程退出 (exitcode={p.returncode})")
            else:
                bore_ok = check_health("bore", f"http://bore.pub:{BORE_PORT}/api/status")

            if not bore_ok and can_restart():
                log("🔄 正在重启 bore.exe...")
                stop_process("bore")
                time.sleep(2)
                processes["bore"] = start_process(
                    "bore.exe",
                    [BORE_EXE, "local", LOCAL_PORT, "--to", "bore.pub", "--port", BORE_PORT],
                    cwd=os.path.dirname(BORE_EXE)
                )


if __name__ == "__main__":
    main()
