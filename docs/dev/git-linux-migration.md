# Git 身份与配置：从本机迁移到 Linux

本文记录 **当前开发机（Windows）上与本仓库相关的 Git 配置快照**，以及在 **另一台 Linux** 上绑定同一 Git 账号时的推荐做法。

> **安全**：不要把含密码、token、私钥的内容提交进仓库。本文仅记录**非密钥**配置项名称与示例命令。

---

## 1. 本机快照（生成于迁移说明编写时）

### 1.1 全局配置（`git config --global` 可见部分）

| 项 | 当前值 | 说明 |
|----|--------|------|
| `user.name` | `zhuzhengqing` | 提交作者名，Linux 上建议保持一致 |
| `user.email` | `zhuzhengqing@58znkj.com` | 提交邮箱，需与托管平台账号/公司规范一致 |
| `core.autocrlf` | `true` | **Windows 专用**；Linux 上不要设为 `true` |
| `http.postbuffer` | `524288000` | 大仓库推送时可保留 |
| `http.followredirects` | `true` | 可按需保留 |
| `http.autoreferer` | `true` | 可按需保留 |
| `http.proxy` / `https.proxy` | `http://127.0.0.1:7890` | **本机代理**；Linux 机器若无同名代理，**不要照抄** |
| `credential.http://<内网-git-host>/...` | `generic` | 仅当你在公司内网 HTTP Git 上需要；Linux 上对该 host 单独配置，勿照搬 Windows 路径 |
| `difftool.*` / `mergetool.*`（Sourcetree） | 若干 | **SourceTree/Windows 向**；Linux 可忽略或改用 `vimdiff` 等 |

### 1.2 本仓库本地配置（仅 `libRoboFlow`）

| 项 | 值 |
|----|-----|
| `remote.origin.url` | `https://github.com/honeyyaya/libRoboFlow.git` |
| 默认分支跟踪 | `main` → `origin/main` |

克隆 Linux 后 `origin` 以你实际 clone 的 URL 为准（HTTPS 或 SSH）。

---

## 2. Linux 上建议做的最小配置

在 Linux 终端执行（把邮箱/姓名换成你的，若与上表一致可直接用）：

```bash
git config --global user.name "zhuzhengqing"
git config --global user.email "zhuzhengqing@58znkj.com"
```

**行尾与换行（不要沿用 Windows 的 `core.autocrlf=true`）：**

```bash
git config --global core.autocrlf input
# 若团队统一不要求 CRLF 转换，也可用：
# git config --global core.autocrlf false
```

**可选（与大推送相关，与本机一致）：**

```bash
git config --global http.postBuffer 524288000
git config --global http.followRedirects true
```

---

## 3. 不要无脑复制的内容

| 配置 | 原因 |
|------|------|
| `http(s).proxy=http://127.0.0.1:7890` | 指向本机环回地址，Linux 上通常无效；若需代理，改为 Linux 上实际代理地址或环境变量 `HTTP_PROXY`/`HTTPS_PROXY` |
| `core.autocrlf=true` | 适合 Windows；Linux 常用 `input` 或 `false` |
| Windows 下 `credential` 助手路径 | Linux 使用 `cache`、`store` 或 SSH，机制不同 |
| SourceTree 的 difftool/mergetool | Linux 无对应 GUI 时可删或另配 |

---

## 4. 与托管平台「绑定账号」的两种方式

### 4.1 HTTPS + 凭据（GitHub/GitLab 等）

- **GitHub**：建议使用 **Personal Access Token (PAT)** 代替账户密码；首次 `git push` 按提示输入，或配置 credential helper：

```bash
git config --global credential.helper cache
# 或超时更长：git config --global credential.helper 'cache --timeout=28800'
```

- **内网 HTTP Git**：若本机存在 `credential.http://<内网主机>.provider=generic` 一类项，在 Linux 上需对该 host 单独配置 credential（与内网认证方式一致）。

### 4.2 SSH（推荐长期使用）

在 **Linux** 上生成密钥（若尚无）：

```bash
ssh-keygen -t ed25519 -C "zhuzhengqing@58znkj.com"
```

将 `~/.ssh/id_ed25519.pub` 内容添加到 GitHub（或公司 Git）的 SSH keys。然后将远程改为 SSH：

```bash
git remote set-url origin git@github.com:honeyyaya/libRoboFlow.git
```

（路径以平台显示为准。）

---

## 5. 从本机复制「全局配置文件」是否可行？

- Windows 全局文件一般在：`%USERPROFILE%\.gitconfig`
- Linux 全局文件一般在：`~/.gitconfig`

可以 **对照合并**：只把 `user.name`、`user.email` 以及你确认在 Linux 仍有效的项拷过去；**代理、credential、autocrlf** 按上文单独处理。

---

## 6. 校验

```bash
git config --global --list
git config user.name
git config user.email
ssh -T git@github.com
# 最后一行在配置 SSH 且托管为 GitHub 时使用
```

---

## 7. 变更记录

| 日期 | 说明 |
|------|------|
| 2026-04-24 | 初稿：根据本仓库所在 Windows 环境 `git config` 快照整理 Linux 迁移步骤 |
