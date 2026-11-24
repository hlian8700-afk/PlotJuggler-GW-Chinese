# BladeScope 个人邮箱授权

Windows 授权版启动时先验证个人邮箱。有效的离线授权也可进入主界面，进入后可使用基础时序、录波分析及工程读写。取消验证将退出软件。「分析 → 授权管理」可查看离线截止时间或申请验证码续期。现场录波和工程数据留在本机。

## 部署

在你管理的主机上使用独立低权限服务账号，安装 Python 3.11 或更新版本和依赖：

```bash
python3 -m venv .venv
.venv/bin/pip install -r auth/requirements.txt
.venv/bin/python auth/server.py --data /var/lib/bladescope-auth init
```

让服务账号独占数据目录。使用主机的服务管理器运行下面的命令，并通过受信任的 HTTPS 反向代理将验证域名转发到本机 8765 端口。代理不要记录 API 请求正文，并按真实来源 IP 做边缘限流。应用本身默认按直接对端限流。普通反代下，多个用户可能共用应用的 IP 配额，因此多人同时登录前需配置仅信任明确指定代理的真实 IP 转发策略。

```bash
.venv/bin/python auth/server.py --data /var/lib/bladescope-auth serve
```

从服务管理器的私密配置提供 `BLADESCOPE_SMTP_HOST`、`BLADESCOPE_SMTP_PORT`（默认 465，隐式 TLS）、`BLADESCOPE_SMTP_USER`、`BLADESCOPE_SMTP_PASSWORD` 和 `BLADESCOPE_SMTP_FROM`。服务端签名密钥、验证码密钥和 SQLite 账号库应备份到受限位置。

## 管理账号

以下邮箱为示例。通过服务器本地管理访问执行：

```bash
.venv/bin/python auth/server.py --data /var/lib/bladescope-auth enable user@example.net
.venv/bin/python auth/server.py --data /var/lib/bladescope-auth list
.venv/bin/python auth/server.py --data /var/lib/bladescope-auth disable user@example.net
```

再次执行 `enable` 可恢复账号。停用后，联网客户端下次成功联系服务端时会清除授权，离线副本最多使用到此前签发的截止时间。已打开的录波页面可继续完成当前操作；随后再次进入会检查授权。

## 构建 Windows 授权版

运行 `.venv/bin/python auth/server.py --data /var/lib/bladescope-auth public-key` 获取公开的 RSA `modulus` 和 `exponent`。在打包环境设置 `BLADESCOPE_AUTH_URL`（HTTPS 域名，末尾无斜杠）、`BLADESCOPE_AUTH_RSA_N`（modulus）、`BLADESCOPE_AUTH_RSA_E`（exponent）。也可以通过 GitHub Actions 同名仓库变量传入。设置这三项后构建授权版。三项都为空时构建普通版。安装包中只包含 URL 和公钥。授权 HTTPS 使用 Windows WinHTTP，沿用系统证书验证。授权缓存在 Windows 当前用户下由 DPAPI 保护，并在使用时检查签名、设备和到期时间。离线期限依赖设备时间，适合常规账号授权管理。

正式分发前请在一台新 Windows 电脑使用实际的验证域名和发件服务，验收收码、在线激活、断网重启、到期续验和停用。独立授权服务的开发测试可用 `python -m unittest discover -s auth -v` 执行，采用模拟发件。

## Railway 单账号试运行

创建 Web 服务时选择本仓库的 `feature/email-authorization` 分支，Root Directory 填 `/auth`，将健康检查路径设为 `/healthz`。挂载持久卷到 `/data`，保持一个实例，并生成服务自带的 HTTPS 域名。配置以下服务变量。可以先部署和检查服务，再加发信密钥启用验证码。

| 变量 | 值 |
|---|---|
| `PORT` | `8080` |
| `BLADESCOPE_INITIAL_EMAIL` | 本次批准开通的第一个个人邮箱 |
| `BLADESCOPE_RESEND_API_KEY` | Resend 的发送专用密钥，存入 Railway 私密变量 |
| `BLADESCOPE_RESEND_FROM` | 可省略，试运行默认使用 `onboarding@resend.dev` |

使用 Resend 测试发件地址时，只能向该 Resend 账号自己的邮箱发信。首个账号应与 Resend 注册邮箱一致。以后开放给其他收件人时，先在 Resend 验证发件域名，再设置发件地址。

部署后检查 `GET /healthz`。首次启动把指定邮箱加到账号库。已停用的账号保持停用，重新部署时也不会恢复。后续通过 `railway ssh` 进入服务容器，在容器里使用 `python /app/server.py --data /data list`、`enable` 或 `disable` 管理账号。通过 HTTPS 访问 `/v1/public-key`，把返回的公钥和当前的 Railway HTTPS 域名配置到 Windows 授权版构建变量。私钥和账号库随持久卷保留。

## 网页管理台

在 Railway 的 bladescope-auth 服务中添加私密变量 `BLADESCOPE_ADMIN_KEY`。使用以下 PowerShell 命令在管理员自己的电脑上生成 256 位随机密钥：

```powershell
$bytes = [byte[]]::new(32)
$rng = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try { $rng.GetBytes($bytes) } finally { $rng.Dispose() }
-join ($bytes | ForEach-Object { $_.ToString('x2') })
```

把生成的 64 位小写十六进制值保存到密码管理器，并直接填入 Railway 变量，应用部署后访问授权服务 HTTPS 域名下的 `/admin`。用同一个密钥登录，可开通、查找、停用或恢复已有账号。退出会结束当前会话，登录会话最多一小时。密钥为空或格式不正确时，管理入口返回 404。管理密钥独立于 Resend 发信密钥。
