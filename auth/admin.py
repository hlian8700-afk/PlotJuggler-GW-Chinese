"""Private browser management for the BladeScope authorization database."""
from contextlib import closing
from html import escape
from http import HTTPStatus
from http.cookies import SimpleCookie
import hmac
import re
import secrets
import threading
import time
from urllib.parse import parse_qs, urlencode

COOKIE = "__Host-bladescope-admin"
SESSION_SECONDS = 3600
PAGE_SIZE = 50

STYLE = """
:root{color-scheme:light;font-family:system-ui,sans-serif;background:#eff4f7;color:#172532}
body{margin:0;padding:24px}main{max-width:850px;margin:4vh auto}
header{display:flex;align-items:center;justify-content:space-between;gap:16px}
h1{font-size:clamp(24px,5vw,34px);margin:0 0 20px}h2{font-size:20px;margin:0 0 14px}
section{background:#fff;border:1px solid #d6e2e8;border-radius:12px;padding:20px;margin-bottom:16px}
form{margin:0}label{display:block;margin-bottom:8px;font-weight:600}
input,button{font:inherit;min-height:42px;border-radius:7px;border:1px solid #91a7b5;padding:8px 12px;box-sizing:border-box}
input{min-width:0;width:100%}button{cursor:pointer;background:#075369;border-color:#075369;color:white;white-space:nowrap}
button.secondary{background:white;color:#075369}.row{display:flex;gap:8px;align-items:center}
.email{overflow-wrap:anywhere;min-width:0}.account{display:grid;grid-template-columns:minmax(0,1fr) 70px auto;gap:8px;align-items:center;padding:12px 0;border-bottom:1px solid #e0e9ee}
.message{background:#dbeef3;padding:12px;border-radius:7px}.pages{display:flex;gap:18px;margin-top:16px}a{color:#075369}
@media(max-width:450px){body{padding:12px}section{padding:14px}.account{grid-template-columns:minmax(0,1fr) auto}.email{grid-column:1/-1}}
"""


class Admin:
    def __init__(self, app, connect, set_account):
        self.app = app
        self.connect = connect
        self.set_account = set_account
        self.sessions = {}
        self.lock = threading.Lock()

    def respond(self, start, status, body="", headers=()):
        data = body.encode("utf-8")
        start(f"{status} {HTTPStatus(status).phrase}", [
            ("Content-Type", "text/html; charset=utf-8"), ("Content-Length", str(len(data))),
            ("Cache-Control", "no-store"), ("X-Content-Type-Options", "nosniff"),
            ("Referrer-Policy", "no-referrer"),
            ("Content-Security-Policy", "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; base-uri 'none'; frame-ancestors 'none'"),
            *headers,
        ])
        return [data]

    def page(self, content):
        return ('<!doctype html><html lang="zh-CN"><meta charset="utf-8">'
                '<meta name="viewport" content="width=device-width,initial-scale=1">'
                f'<title>BladeScope 授权管理</title><style>{STYLE}</style>'
                f'<main>{content}</main></html>')

    def cookie(self, value, age):
        return ("Set-Cookie", f"{COOKIE}={value}; Path=/; Max-Age={age}; Secure; HttpOnly; SameSite=Strict")

    def login_page(self, start, status=200, message=""):
        note = f'<p class="message" role="status">{escape(message)}</p>' if message else ""
        return self.respond(start, status, self.page(
            f'<h1>BladeScope 授权管理</h1><section>{note}<form method="post" action="/admin/login">'
            '<label for="admin-key">管理员密钥</label><div class="row">'
            '<input id="admin-key" name="key" type="password" autocomplete="current-password" '
            'required maxlength="256"><button>登录</button></div></form></section>'))

    def session(self, environ, now):
        jar = SimpleCookie()
        try:
            jar.load(environ.get("HTTP_COOKIE", ""))
        except ValueError:
            return "", ""
        token = jar[COOKIE].value if COOKIE in jar else ""
        with self.lock:
            expired = [key for key, (expiry, _) in self.sessions.items() if expiry <= now]
            for key in expired:
                del self.sessions[key]
            match = self.sessions.get(token)
        return token, match[1] if match else ""

    def allow_login(self, environ, now):
        with closing(self.connect(self.app.directory)) as db:
            db.execute("BEGIN IMMEDIATE")
            try:
                allowed = self.app.allow(db, "admin-login:" + environ.get("REMOTE_ADDR", ""), now, 20)
                db.commit()
                return allowed
            except Exception:
                db.rollback()
                raise

    def __call__(self, environ, start, secret):
        if not re.fullmatch(r"[0-9a-f]{64}", secret):
            return self.respond(start, 404)
        path = environ["PATH_INFO"]
        method = environ["REQUEST_METHOD"]
        if path not in ("/admin", "/admin/login", "/admin/logout", "/admin/accounts"):
            return self.respond(start, 404)
        now = int(time.time())
        token, csrf = self.session(environ, now)
        if method == "GET" and path == "/admin":
            return self.dashboard(environ, start, csrf) if csrf else self.login_page(start)
        if method != "POST" or path == "/admin":
            return self.respond(start, 405)
        if environ.get("HTTP_SEC_FETCH_SITE") not in (None, "same-origin"):
            return self.respond(start, 403)
        try:
            length = int(environ.get("CONTENT_LENGTH") or 0)
            if not 0 < length <= 4096:
                raise ValueError("invalid form size")
            fields = parse_qs(environ["wsgi.input"].read(length).decode("utf-8"), max_num_fields=6)
        except (ValueError, UnicodeError):
            return self.respond(start, 400)
        if path == "/admin/login":
            supplied = fields.get("key", [""])[0].encode()
            if not self.allow_login(environ, now):
                return self.login_page(start, 429, "登录尝试过多，请稍后再试。")
            if not hmac.compare_digest(supplied, secret.encode()):
                return self.login_page(start, 401, "管理员密钥错误。")
            token = secrets.token_urlsafe(32)
            with self.lock:
                if len(self.sessions) >= 64:
                    self.sessions.pop(next(iter(self.sessions)))
                self.sessions[token] = (now + SESSION_SECONDS, secrets.token_urlsafe(32))
            return self.respond(start, 303, headers=(("Location", "/admin"), self.cookie(token, SESSION_SECONDS)))
        supplied_csrf = fields.get("csrf", [""])[0].encode()
        if not csrf or not hmac.compare_digest(supplied_csrf, csrf.encode()):
            return self.respond(start, 403)
        if path == "/admin/logout":
            with self.lock:
                self.sessions.pop(token, None)
            return self.respond(start, 303, headers=(("Location", "/admin"), self.cookie("", 0)))
        action = fields.get("action", [""])[0]
        if action not in ("enable", "disable"):
            return self.respond(start, 400)
        try:
            self.set_account(self.app.directory, fields.get("email", [""])[0], action == "enable")
        except ValueError:
            return self.dashboard(environ, start, csrf, "请输入有效的电子邮箱。", 400)
        location = "/admin?" + urlencode({"result": action})
        return self.respond(start, 303, headers=(("Location", location),))

    def dashboard(self, environ, start, csrf, message="", status=200):
        try:
            query = parse_qs(environ.get("QUERY_STRING", ""), max_num_fields=8)
            page = min(100000, max(1, int(query.get("page", ["1"])[0])))
        except ValueError:
            return self.respond(start, 400)
        search = query.get("q", [""])[0].strip().lower()[:254]
        with closing(self.connect(self.app.directory)) as db:
            accounts = db.execute(
                "SELECT email, enabled FROM account WHERE instr(email, ?) > 0 ORDER BY email LIMIT ? OFFSET ?",
                (search if search else "", PAGE_SIZE + 1, (page - 1) * PAGE_SIZE),
            ).fetchall()
        hidden = f'<input type="hidden" name="csrf" value="{escape(csrf)}">'
        rows = []
        for address, enabled in accounts[:PAGE_SIZE]:
            safe = escape(address)
            state = "已开通" if enabled else "已停用"
            action, label = ("disable", "停用") if enabled else ("enable", "恢复")
            rows.append(f'<div class="account"><span class="email">{safe}</span><span>{state}</span>'
                        f'<form method="post" action="/admin/accounts">{hidden}'
                        f'<input type="hidden" name="email" value="{safe}">'
                        f'<button class="secondary" name="action" value="{action}">{label}</button></form></div>')
        result = query.get("result", [""])[0]
        message = message or {"enable": "授权已开通。用户可在 BladeScope 中获取验证码。",
                              "disable": "授权已停用。"}.get(result, "")
        note = f'<p class="message" role="status">{escape(message)}</p>' if message else ""
        previous = self.page_link(search, page - 1, "上一页") if page > 1 else ""
        next_page = self.page_link(search, page + 1, "下一页") if len(accounts) > PAGE_SIZE else ""
        return self.respond(start, status, self.page(
            '<header><h1>BladeScope 授权管理</h1>'
            f'<form method="post" action="/admin/logout">{hidden}<button class="secondary">退出</button></form></header>'
            f'{note}<section><h2>开通账号</h2><form method="post" action="/admin/accounts">{hidden}'
            '<label for="email">用户邮箱</label><div class="row"><input id="email" type="email" name="email" '
            'inputmode="email" maxlength="254" required>'
            + '<button name="action" value="enable">开通</button></div></form></section>'
            + '<section><h2>授权名单</h2><form method="get" action="/admin">'
            + '<label for="q">按邮箱搜索</label><div class="row">'
            + f'<input id="q" type="search" name="q" value="{escape(search)}" maxlength="254">'
            + '<button class="secondary">查找</button></div></form>'
            + ("".join(rows) if rows else '<p>没有匹配的账号。</p>')
            + f'<nav class="pages">{previous}{next_page}</nav></section>'))

    @staticmethod
    def page_link(search, page, label):
        query = escape(urlencode({"q": search, "page": page}))
        return f'<a href="/admin?{query}">{label}</a>'
