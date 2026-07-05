# AI 智能体发版指南

> 本文件供未来的 AI 智能体（如 CodeBuddy/WorkBuddy）阅读。
> 发版时必须严格遵守以下流程，否则会导致用户收不到更新日志。

## ★ 必须推送的文件清单

每次发版推送到手机时，**以下文件缺一不可**：

| 文件 | 推送路径 | 权限 | 说明 |
|------|----------|------|------|
| 二进制 `overlay_user` | `/data/local/bin/overlay_user` | 777 | 主程序 |
| 二进制副本 | `/data/local/bin/overlay_user_v{版本代码}` | 777 | 版本备份 |
| `version-latest.txt` | `/data/local/bin/version-latest.txt` | 666 | 版本信息 |
| **更新日志** `changelog_v{版本代码}.txt` | `/data/local/bin/changelog_v{版本代码}.txt` | 666 | **★ 必须推送！** |

## ★ 推送命令模板

```bash
# 1. 创建更新日志文件（项目根目录）
# 文件名格式: 更新日志_v{版本号}_2026-MM-DD.txt
# 内容: 版本号、MD5、日期、版本代码、修复内容

# 2. 推送到手机（完整流程）
adb push overlay-v{版本号}-stable-user /sdcard/overlay_user_tmp
adb push overlay-v{版本号}-stable-user /sdcard/overlay_user_v{版本代码}
adb push version-latest.txt /sdcard/version-latest.txt
adb push 更新日志_v{版本号}_2026-MM-DD.txt /sdcard/changelog_v{版本代码}.txt

adb shell "su -c '
  cp /sdcard/overlay_user_tmp /data/local/bin/overlay_user && chmod 777 /data/local/bin/overlay_user &&
  cp /sdcard/overlay_user_v{版本代码} /data/local/bin/overlay_user_v{版本代码} && chmod 777 /data/local/bin/overlay_user_v{版本代码} &&
  cp /sdcard/version-latest.txt /data/local/bin/version-latest.txt && chmod 666 /data/local/bin/version-latest.txt &&
  cp /sdcard/changelog_v{版本代码}.txt /data/local/bin/changelog_v{版本代码}.txt && chmod 666 /data/local/bin/changelog_v{版本代码}.txt &&
  rm /sdcard/overlay_user_tmp /sdcard/overlay_user_v{版本代码} /sdcard/version-latest.txt /sdcard/changelog_v{版本代码}.txt
'"

# 3. 验证
adb shell "su -c 'ls -la /data/local/bin/overlay_user /data/local/bin/overlay_user_v{版本代码} /data/local/bin/version-latest.txt /data/local/bin/changelog_v{版本代码}.txt'"
```

## ★ 常见错误

1. **忘记推送更新日志** — 用户在手机上看不到更新内容
2. **更新日志文件名不一致** — 手机上用 `changelog_v{版本代码}.txt`（如 `changelog_v243.txt`），不要用中文名
3. **忘记 chmod** — 二进制必须 777，文本文件 666
4. **直接 adb push 到 /data/local/bin/** — 不行，必须先 push 到 /sdcard/ 再 su cp

## ★ version-latest.txt 格式

```
v{版本号}
MD5: {32位MD5}
日期: {YYYY-MM-DD}
版本代码: {整数版本代码}
```
