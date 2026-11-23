# BladeScope 个人邮箱授权

Windows 授权版在首次进入「分析 → 录波分析」时验证个人邮箱。「分析 → 授权管理」可查看离线截止时间或申请验证码续期。基础时序图、原有工程读写和普通曲线分析可继续使用。现场录波和工程数据留在本机。

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

运行 `.venv/bin/python auth/server.py --data /var/lib/bladescope-auth public-key` 获取公开的 RSA `modulus` 和 `exponent`。在打包环境设置 `BLADESCOPE_AUTH_URL`（HTTPS 域名，末尾无斜杠）、`BLADESCOPE_AUTH_RSA_N`（modulus）、`BLADESCOPE_AUTH_RSA_E`（exponent）。也可以通过 GitHub Actions 同名仓库变量传入。设置这三项后构建授权版。三项都为空时构建普通版。安装包中只包含 URL 和公钥。授权缓存在 Windows 当前用户下由 DPAPI 保护，并在使用时检查签名、设备和到期时间。离线期限依赖设备时间，适合常规账号授权管理。

正式分发前请在一台新 Windows 电脑使用实际的验证域名和发件服务，验收收码、在线激活、断网重启、到期续验和停用。独立授权服务的开发测试可用 `python -m unittest discover -s auth -v` 执行，采用模拟发件。
