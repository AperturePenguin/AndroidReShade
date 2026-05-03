# CI 云端打包指南

本项目已配置 GitHub Actions 自动构建，Push 代码后自动在云端编译 APK，无需本地安装 NDK / JDK。

---

## 一、首次推送到 GitHub

### 1. 在 GitHub 创建新仓库

1. 打开 https://github.com/new
2. 仓库名填 `AndroidReShade`
3. **不要**勾选 "Initialize with README"（本地已有代码）
4. 点 Create repository

### 2. 本地初始化并推送

```powershell
# 进入项目目录
cd C:\Users\Hao\WorkBuddy\Claw\AndroidReShade

# 初始化 git（如果还没有）
git init

# 添加所有文件（.gitignore 已配置，不会提交构建产物）
git add .

# 首次提交
git commit -m "Initial commit: AndroidReShade MVP"

# 关联远程仓库（把 YOUR_USERNAME 换成你的 GitHub 用户名）
git remote add origin https://github.com/YOUR_USERNAME/AndroidReShade.git

# 推送（首次需要输入 GitHub 账号密码 / Token）
git push -u origin main
```

> 如果默认分支名是 `master`，把上面 `main` 改成 `master`。

---

## 二、触发构建

推送后自动触发构建，无需额外操作：

```powershell
# 每次修改代码后推送即可触发
git add .
git commit -m "update: 描述你的改动"
git push
```

也支持**手动触发**：
1. 打开 GitHub 仓库页面
2. 点 **Actions** 标签
3. 选择 **Build AndroidReShade APK**
4. 点 **Run workflow** → 选分支 → **Run**

---

## 三、下载 APK

构建完成后：

1. 打开 GitHub 仓库页面 → **Actions**
2. 点击最近一次构建记录
3. 滚动到底部 **Artifacts** 区域
4. 下载：
   - `AndroidReShade-debug-<run_number>.zip` — Debug APK
   - `AndroidReShade-release-<run_number>.zip` — Release APK

> Artifacts 保留时间：debug 30 天，release 90 天

---

## 四、发布正式版本（可选）

打 Tag 即自动创建 GitHub Release 并附上 APK：

```powershell
git tag v1.0.0
git push origin v1.0.0
```

Actions 会自动：
1. 构建 Release APK
2. 创建 GitHub Release `v1.0.0`
3. 把 APK 附加到 Release 中

---

## 五、构建状态徽章

在 README.md 顶部加上构建状态徽章（把 `YOUR_USERNAME` 替换成你的用户名）：

```markdown
![Build APK](https://github.com/YOUR_USERNAME/AndroidReShade/actions/workflows/build-apk.yml/badge.svg)
```

---

## 六、常见问题

### Q: 构建失败，提示 `gradle-wrapper.jar` 不存在？
A: 确保以下文件已提交到 git：
```
gradlew
gradlew.bat
gradle/wrapper/gradle-wrapper.properties
```
`gradle-wrapper.jar` **不需要**提交（已在 .gitignore 中排除），CI 会自动下载。

### Q: 构建失败，提示 `libs.versions.toml` 找不到？
A: 确认 `gradle/libs.versions.toml` 文件已提交。

### Q: 构建超时？
A: NDK 编译比较耗时，首次构建可能需要 15~25 分钟。确保 GitHub Actions 超时设置 ≥ 60 分钟（workflow 中已配置 `timeout-minutes: 60`）。

### Q: 只想打 Debug 包，不想打 Release？
A: 编辑 `.github/workflows/build-apk.yml`，删掉 `Build Release APK` 和 `Upload Release APK` 两个 step。
