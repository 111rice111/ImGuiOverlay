# AI 智能体发版指南

> 当用户说「发版」「推送」「部署」时，AI 必须先读完本文件，再按顺序执行。
> 任何步骤失败，停下来报告，不要继续。

---

## ★ 变量定义

每个 Bash 代码块开头定义了 `$VERSION_FULL` 等变量，**AI 先替换成实际值再执行**。

非 Bash 步骤（Edit/Write 工具）中出现的 `VERSION_FULL` / `VERSION_CODE` 等也需替换。

```
VERSION_FULL   = "v2.46-stable"       # 例: v2.46-stable
VERSION_CODE   = 246                  # 例: 246
VERSION_SHORT  = "v2.46"              # 例: v2.46
DATE_TODAY     = "2026-07-07"         # 当天日期
CHANGE_SUMMARY = "修复触摸偏移"        # 一句话描述
```

**固定路径：**
```
PROJECT_ROOT   = E:/ImGuiOverlay-User
ARCHIVE_BIN    = E:/ImGuiOverlay-User/archive/历史版本二进制
ARCHIVE_LOG    = E:/ImGuiOverlay-User/archive/更新日志归档
CMAKELISTS     = E:/ImGuiOverlay-User/app/src/main/cpp/CMakeLists.txt
VERSION_FILE   = E:/ImGuiOverlay-User/version-latest.txt
CHANGELOG_MD   = E:/ImGuiOverlay-User/CHANGELOG.md
```

---

## ★ 双版本架构

一次编译产出两个，差异仅 `AUTH_SERVER` 宏：

| | overlay | overlay_user |
|---|---|---|
| 联网验证 | ❌ | ✅ |
| 编译宏 | — | AUTH_SERVER |

---

## ★ 步骤 1：更新版本号 + 连接检查

**1a.** 用 Edit 工具改 `CMAKELISTS` 顶部 3 行：
```cmake
set(OVERLAY_VERSION "VERSION_FULL")
set(OVERLAY_VERSION_CODE VERSION_CODE)
set(OVERLAY_VERSION_STR "VERSION_SHORT")
```

**1b.** 检查手机连接：
```bash
adb devices
```
应看到 device 字样。没有就报错停止。

---

## ★ 步骤 2：编译

用 PowerShell（不用 Git Bash，Gradle 在 PowerShell 更稳）：

```powershell
cd E:\ImGuiOverlay-User
.\gradlew clean assembleRelease
```

等 `BUILD SUCCESSFUL`。

---

## ★ 步骤 3：定位 + 自检 + 记录（一次 Bash）

**以下全部内容在同一个 Bash 调用中执行（变量需要跨命令共享）：**

```bash
# ⚠️ 先定义变量（替换为实际值）
VERSION_FULL="v2.46-stable"
VERSION_CODE=246
VERSION_SHORT="v2.46"
DATE_TODAY="2026-07-07"
CHANGE_SUMMARY="修复触摸偏移"

# === 3a: 定位两个二进制 ===
OVERLAY_DEV=$(find E:/ImGuiOverlay-User/app/build -name overlay -type f 2>/dev/null | grep arm64 | head -1)
OVERLAY_USER=$(find E:/ImGuiOverlay-User/app/build -name overlay_user -type f 2>/dev/null | grep arm64 | head -1)

echo "开发版: $OVERLAY_DEV"
echo "用户版: $OVERLAY_USER"

[ -z "$OVERLAY_DEV" ] && echo "错误: 找不到 overlay" && exit 1
[ -z "$OVERLAY_USER" ] && echo "错误: 找不到 overlay_user" && exit 1

# === 3b: 自检 ===
echo ""
echo "=== ARM64 检查 ==="
file "$OVERLAY_USER"
# 期望: ELF 64-bit LSB ... ARM aarch64

echo ""
echo "=== 版本号检查 ==="
grep -ao 'v2\.[0-9][0-9]' "$OVERLAY_USER" | head -5
# 期望: 能搜到版本号

# === MD5 并写文件（步骤 4 读取）===
DEV_MD5=$(md5sum "$OVERLAY_DEV" | awk '{print $1}')
USER_MD5=$(md5sum "$OVERLAY_USER" | awk '{print $1}')
echo "开发版 MD5: $DEV_MD5"
echo "用户版 MD5: $USER_MD5"

echo "$DEV_MD5"  > E:/ImGuiOverlay-User/.dev_md5_tmp
echo "$USER_MD5" > E:/ImGuiOverlay-User/.user_md5_tmp
echo "$OVERLAY_DEV"  > E:/ImGuiOverlay-User/.dev_path_tmp
echo "$OVERLAY_USER" > E:/ImGuiOverlay-User/.user_path_tmp
```

如果 grep 找不到版本号，回去检查步骤 1。

---

## ★ 步骤 4：本地归档

**4a. 复制二进制（一次 Bash）**

```bash
VERSION_FULL="v2.46-stable"

DEV_MD5=$(cat E:/ImGuiOverlay-User/.dev_md5_tmp)
USER_MD5=$(cat E:/ImGuiOverlay-User/.user_md5_tmp)
OVERLAY_DEV=$(cat E:/ImGuiOverlay-User/.dev_path_tmp)
OVERLAY_USER=$(cat E:/ImGuiOverlay-User/.user_path_tmp)

cp "$OVERLAY_DEV"  "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL"
cp "$OVERLAY_USER" "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL-user"

echo "$DEV_MD5  overlay-$VERSION_FULL"       > "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL.md5"
echo "$USER_MD5  overlay-$VERSION_FULL-user" > "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL-user.md5"
```

**4b. 创建更新日志**

先读取 MD5 值（Bash）：
```bash
cat E:/ImGuiOverlay-User/.dev_md5_tmp
cat E:/ImGuiOverlay-User/.user_md5_tmp
```

然后用 Write 工具创建文件：
```
E:/ImGuiOverlay-User/archive/更新日志归档/更新日志_VERSION_SHORT_DATE_TODAY.txt
```

内容（把 MD5 替换成上面读到的值）：
```
VERSION_FULL — CHANGE_SUMMARY

版本: VERSION_FULL
版本代码: VERSION_CODE
日期: DATE_TODAY
开发版 MD5: <步骤 4b 读到的 DEV_MD5>
用户版 MD5: <步骤 4b 读到的 USER_MD5>

=== 改动 ===
- (逐条列出)
```

**4c. 更新根目录文件**

`VERSION_FILE`（覆盖写入，MD5 用步骤 4b 读到的 USER_MD5）：
```
VERSION_FULL
VERSION_CODE
<USER_MD5>
```

`CHANGELOG_MD`：在标题后追加：
```markdown
## VERSION_FULL (DATE_TODAY)
- xxx
```

**所有归档完成，清理临时文件：**
```bash
rm E:/ImGuiOverlay-User/.dev_md5_tmp E:/ImGuiOverlay-User/.user_md5_tmp E:/ImGuiOverlay-User/.dev_path_tmp E:/ImGuiOverlay-User/.user_path_tmp
```

---

## ★ 步骤 5：推送到手机

**以下所有命令在同一 Bash 调用中执行（变量共享）：**

```bash
VERSION_FULL="v2.46-stable"
VERSION_CODE=246
VERSION_SHORT="v2.46"
DATE_TODAY="2026-07-07"

# === adb push ===
adb push "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL" /sdcard/overlay_dev_tmp

adb push "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL-user" /sdcard/overlay_user_tmp

adb push "E:/ImGuiOverlay-User/archive/历史版本二进制/overlay-$VERSION_FULL-user" /sdcard/overlay_user_v$VERSION_CODE

adb push E:/ImGuiOverlay-User/version-latest.txt /sdcard/version-latest.txt

adb push "E:/ImGuiOverlay-User/archive/更新日志归档/更新日志_${VERSION_SHORT}_${DATE_TODAY}.txt" /sdcard/changelog_v${VERSION_CODE}.txt

# === su cp + chmod ===
adb shell "su -c 'cp /sdcard/overlay_dev_tmp /data/local/bin/overlay && chmod 777 /data/local/bin/overlay'"

adb shell "su -c 'cp /sdcard/overlay_user_tmp /data/local/bin/overlay_user && chmod 777 /data/local/bin/overlay_user'"

adb shell "su -c \"cp /sdcard/overlay_user_v$VERSION_CODE /data/local/bin/overlay_user_v$VERSION_CODE && chmod 777 /data/local/bin/overlay_user_v$VERSION_CODE\""

adb shell "su -c 'cp /sdcard/version-latest.txt /data/local/bin/version-latest.txt && chmod 666 /data/local/bin/version-latest.txt'"

adb shell "su -c \"cp /sdcard/changelog_v$VERSION_CODE.txt /data/local/bin/changelog_v$VERSION_CODE.txt && chmod 666 /data/local/bin/changelog_v$VERSION_CODE.txt\""

# === 清理 sdcard ===
adb shell "su -c \"rm /sdcard/overlay_dev_tmp /sdcard/overlay_user_tmp /sdcard/overlay_user_v$VERSION_CODE /sdcard/version-latest.txt /sdcard/changelog_v$VERSION_CODE.txt\""
```

---

## ★ 步骤 6：验证

```bash
VERSION_CODE=246
adb shell "su -c \"ls -la /data/local/bin/overlay /data/local/bin/overlay_user /data/local/bin/overlay_user_v$VERSION_CODE /data/local/bin/version-latest.txt /data/local/bin/changelog_v$VERSION_CODE.txt\""
```

5 个文件，overlay/overlay_user 权限 `-rwxrwxrwx`。

---

## ★ 步骤 7：Git

```bash
VERSION_FULL="v2.46-stable"
CHANGE_SUMMARY="修复触摸偏移"

cd E:/ImGuiOverlay-User
git add -A
git commit -m "$VERSION_FULL: $CHANGE_SUMMARY"
git tag -a "$VERSION_FULL" -m "$VERSION_FULL: $CHANGE_SUMMARY"
git push origin main
git push origin "$VERSION_FULL"
```

---

## ★ 步骤 8：版本号改 dev

Edit `CMAKELISTS`：
```cmake
set(OVERLAY_VERSION "v2.47-dev")       # SHORT+0.01, -stable→-dev
set(OVERLAY_VERSION_CODE 247)          # CODE+1
set(OVERLAY_VERSION_STR "v2.47")       # SHORT+0.01
```

---

## ★ 回退

```bash
git checkout VERSION_FULL
```
如果 archive 有该版本二进制 → adb push 它。没有 → 重新编译。

---

## ★ 铁律

| ❌ | ✅ |
|----|----|
| 硬编码 `6u3a2a4x` 路径 | `find` 动态查找 |
| 人眼看 MD5 再手输 | 写临时文件再读 |
| 不查 adb 就推 | 步骤 1b 先查 |
| adb push 到 /data/local/bin/ | 先到 /sdcard/ 再 su cp |
| 用 /data/local/tmp/ | 只用 /data/local/bin/ |
| 只推一个版本 | overlay + overlay_user 都推 |
| 只 commit 不打 tag | commit + tag + push |

---

## ★ 检查清单

- [ ] 1a: CMakeLists.txt 版本号更新
- [ ] 1b: adb devices 有设备
- [ ] 2: 编译 BUILD SUCCESSFUL
- [ ] 3a: find 找到两个二进制
- [ ] 3b: ARM64 ✓ 版本号 ✓
- [ ] 4a: 两个二进制已归档 + MD5
- [ ] 4b: 更新日志已创建
- [ ] 4c: CHANGELOG.md + version-latest.txt 已更新
- [ ] 5: 5 个文件已推送
- [ ] 6: 手机端验证通过
- [ ] 7: git commit + tag + push
- [ ] 8: CMakeLists.txt 改 dev
