"""BladeScope personal-email authorizations. Python 3.11+."""
import argparse
import base64
from contextlib import closing
import hashlib
import hmac
import json
import os
from pathlib import Path
import re
import secrets
import smtplib
import sqlite3
import ssl
import time
from email.message import EmailMessage
from urllib.request import HTTPRedirectHandler, Request, build_opener

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa

from admin import Admin

OFFLINE_SECONDS = 15 * 86400
CODE_SECONDS = 600
SCOPE = "bladescope.waveform"
EMAIL = re.compile(r"[a-z0-9.!#$%&'*+/=?^_`{|}~-]+@[a-z0-9-]+(?:\.[a-z0-9-]+)+")
DEVICE = re.compile(r"[a-f0-9]{64}")
BASE64 = re.compile(r"[A-Za-z0-9_-]+")


def b64(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def unb64(value):
    if not BASE64.fullmatch(value):
        raise ValueError("invalid base64url")
    return base64.urlsafe_b64decode(value + "=" * (-len(value) % 4))


def email_address(value):
    if not isinstance(value, str):
        raise ValueError("invalid email")
    email = value.strip().lower()
    if len(email) > 254 or not EMAIL.fullmatch(email):
        raise ValueError("invalid email")
    return email


def device_id(value):
    if not isinstance(value, str) or not DEVICE.fullmatch(value):
        raise ValueError("invalid device")
    return value


def connect(directory):
    db = sqlite3.connect(directory / "accounts.sqlite3", timeout=5, isolation_level=None)
    db.execute("PRAGMA foreign_keys=ON")
    return db


def init(directory):
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    for name, create in (
        ("signing.pem", lambda: rsa.generate_private_key(65537, 3072).private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()
        )),
        ("codes.key", lambda: secrets.token_bytes(32)),
    ):
        path = directory / name
        if not path.exists():
            with os.fdopen(os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600), "wb") as out:
                out.write(create())
    with closing(connect(directory)) as db:
        db.executescript("""
            CREATE TABLE IF NOT EXISTS account (
                email TEXT PRIMARY KEY, enabled INTEGER NOT NULL, revision INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS challenge (
                email TEXT NOT NULL REFERENCES account(email), device TEXT NOT NULL,
                digest BLOB NOT NULL, issued INTEGER NOT NULL, remaining INTEGER NOT NULL,
                PRIMARY KEY(email, device)
            );
            CREATE TABLE IF NOT EXISTS limits (
                bucket BLOB NOT NULL, window INTEGER NOT NULL, count INTEGER NOT NULL,
                PRIMARY KEY(bucket, window)
            );
        """)


def signing_key(directory):
    return serialization.load_pem_private_key((directory / "signing.pem").read_bytes(), password=None)


def set_account(directory, email, enabled):
    email = email_address(email)
    with closing(connect(directory)) as db:
        db.execute("BEGIN IMMEDIATE")
        if enabled:
            db.execute("""
                INSERT INTO account VALUES (?, 1, 1)
                ON CONFLICT(email) DO UPDATE SET enabled=1, revision=revision+1 WHERE enabled=0
            """, (email,))
        else:
            db.execute("UPDATE account SET enabled=0, revision=revision+1 WHERE email=? AND enabled=1", (email,))
            db.execute("DELETE FROM challenge WHERE email=?", (email,))
        db.commit()


class RejectMailRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def send_mail(address, code):
    subject = "BladeScope 授权验证码"
    body = f"BladeScope 验证码：{code}\n验证码在 10 分钟内有效，仅可使用一次。"
    if os.getenv("BLADESCOPE_MAIL_TRANSPORT", "smtp") == "resend":
        request = Request(
            "https://api.resend.com/emails",
            data=json.dumps({
                "from": os.environ["BLADESCOPE_RESEND_FROM"], "to": [address],
                "subject": subject, "text": body,
            }).encode(),
            headers={"Authorization": "Bearer " + os.environ["BLADESCOPE_RESEND_API_KEY"],
                     "Content-Type": "application/json", "User-Agent": "BladeScope-Authorization/1.0"},
            method="POST",
        )
        with build_opener(RejectMailRedirects()).open(request, timeout=8) as response:
            if response.status not in (200, 201, 202):
                raise OSError("Mail API rejected the request")
        return

    message = EmailMessage()
    message["Subject"] = subject
    message["From"] = os.environ["BLADESCOPE_SMTP_FROM"]
    message["To"] = address
    message.set_content(body)
    with smtplib.SMTP_SSL(os.environ["BLADESCOPE_SMTP_HOST"],
                         int(os.getenv("BLADESCOPE_SMTP_PORT", "465")),
                         timeout=10, context=ssl.create_default_context()) as smtp:
        smtp.login(os.environ["BLADESCOPE_SMTP_USER"], os.environ["BLADESCOPE_SMTP_PASSWORD"])
        smtp.send_message(message)


class Authorization:
    def __init__(self, directory, mail=send_mail, clock=time.time):
        self.directory = directory
        self.key = signing_key(directory)
        self.code_key = (directory / "codes.key").read_bytes()
        self.mail = mail
        self.clock = clock
        self.admin = Admin(self, connect, set_account)

    def digest(self, *parts):
        return hmac.digest(self.code_key, "\0".join(parts).encode(), "sha256")

    def allow(self, db, key, now, maximum):
        window = now // 600
        db.execute("DELETE FROM limits WHERE window < ?", (window - 1,))
        bucket = self.digest(key)
        prior = db.execute("SELECT count FROM limits WHERE bucket=? AND window=?", (bucket, window)).fetchone()
        if prior and prior[0] >= maximum:
            return False
        db.execute("""
            INSERT INTO limits VALUES (?, ?, 1)
            ON CONFLICT(bucket, window) DO UPDATE SET count=count+1
        """, (bucket, window))
        return True

    def issue(self, email, device, revision, now):
        payload = json.dumps(dict(email=email, device=device, revision=revision,
                                  scope=SCOPE, iat=now, exp=now+OFFLINE_SECONDS),
                             separators=(",", ":")).encode()
        signature = self.key.sign(payload, padding.PKCS1v15(), hashes.SHA256())
        return b64(payload) + "." + b64(signature)

    def parse(self, token, device, now):
        if not isinstance(token, str) or len(token) > 3000:
            raise ValueError("invalid token")
        body, signature = map(unb64, token.split("."))
        self.key.public_key().verify(signature, body, padding.PKCS1v15(), hashes.SHA256())
        data = json.loads(body)
        if data["scope"] != SCOPE or data["device"] != device or not data["iat"] <= now < data["exp"]:
            raise ValueError("invalid token")
        return data

    def dispatch(self, route, data, ip):
        device = device_id(data.get("device"))
        now = int(self.clock())
        if route == "/v1/refresh":
            try:
                claims = self.parse(data.get("token"), device, now)
            except (ValueError, InvalidSignature):
                return 401, {"error": "invalid_token"}
            with closing(connect(self.directory)) as db:
                account = db.execute("SELECT enabled, revision FROM account WHERE email=?",
                                     (claims["email"],)).fetchone()
            if account != (1, claims["revision"]):
                return 403, {"error": "revoked"}
            return 200, {"token": self.issue(claims["email"], device, account[1], now)}

        email = email_address(data.get("email"))
        with closing(connect(self.directory)) as db:
            db.execute("BEGIN IMMEDIATE")
            account = db.execute("SELECT enabled, revision FROM account WHERE email=?", (email,)).fetchone()
            if route == "/v1/code":
                allowed_ip = self.allow(db, "request:ip:" + ip, now, 20)
                allowed_email = self.allow(db, "request:email:" + email, now, 3)
                if not allowed_ip or not allowed_email:
                    db.commit()
                    return 429, {"error": "rate_limited"}
                db.execute("DELETE FROM challenge WHERE issued <= ?", (now - CODE_SECONDS,))
                recent = db.execute("SELECT MAX(issued) FROM challenge WHERE email=?", (email,)).fetchone()[0]
                if not account or not account[0] or (recent is not None and now - recent < 60):
                    db.commit()
                    return 202, {}
                code = f"{secrets.randbelow(1000000):06d}"
                digest = self.digest(email, device, code)
                db.execute("INSERT OR REPLACE INTO challenge VALUES (?, ?, ?, ?, 5)",
                           (email, device, digest, now))
                db.commit()
            else:
                allowed_ip = self.allow(db, "verify:ip:" + ip, now, 40)
                allowed_email = self.allow(db, "verify:email:" + email, now, 12)
                if not allowed_ip or not allowed_email:
                    db.commit()
                    return 429, {"error": "rate_limited"}
                code = data.get("code")
                row = db.execute("SELECT digest, issued, remaining FROM challenge WHERE email=? AND device=?",
                                 (email, device)).fetchone()
                if not account or not account[0] or not row or now - row[1] >= CODE_SECONDS or not row[2]:
                    db.commit()
                    return 401, {"error": "invalid_code"}
                if not isinstance(code, str) or not re.fullmatch(r"[0-9]{6}", code) or not hmac.compare_digest(
                        row[0], self.digest(email, device, code)):
                    db.execute("UPDATE challenge SET remaining=remaining-1 WHERE email=? AND device=?", (email, device))
                    db.commit()
                    return 401, {"error": "invalid_code"}
                token = self.issue(email, device, account[1], now)
                db.execute("DELETE FROM challenge WHERE email=? AND device=?", (email, device))
                db.commit()
                return 200, {"token": token}
        try:
            self.mail(email, code)
        except (OSError, smtplib.SMTPException, KeyError, ValueError):
            with closing(connect(self.directory)) as db:
                db.execute("DELETE FROM challenge WHERE email=? AND device=? AND digest=?", (email, device, digest))
            return 503, {"error": "mail_unavailable"}
        return 202, {}

    def __call__(self, environ, respond):
        route = environ["PATH_INFO"]
        if route == "/admin" or route.startswith("/admin/"):
            return self.admin(environ, respond, os.getenv("BLADESCOPE_ADMIN_KEY", ""))
        if route == "/healthz" and environ["REQUEST_METHOD"] == "GET":
            with closing(connect(self.directory)) as db:
                db.execute("SELECT 1 FROM account LIMIT 1").fetchone()
            status, result = 200, {"status": "ok"}
        elif route == "/v1/public-key" and environ["REQUEST_METHOD"] == "GET":
            status, result = 200, public_config(self.directory, key=self.key)
        elif route not in ("/v1/code", "/v1/verify", "/v1/refresh"):
            status, result = 404, {"error": "not_found"}
        elif environ["REQUEST_METHOD"] != "POST":
            status, result = 405, {"error": "method_not_allowed"}
        else:
            try:
                length = int(environ.get("CONTENT_LENGTH") or 0)
                if not 0 < length <= 4096:
                    raise ValueError("invalid body length")
                body = json.loads(environ["wsgi.input"].read(length))
                if not isinstance(body, dict):
                    raise ValueError("invalid body")
                status, result = self.dispatch(route, body, environ.get("REMOTE_ADDR", "unknown"))
            except (ValueError, UnicodeError):
                status, result = 400, {"error": "invalid_request"}
        from http import HTTPStatus
        payload = json.dumps(result).encode()
        respond(f"{status} {HTTPStatus(status).phrase}",
                [("Content-Type", "application/json"), ("Content-Length", str(len(payload))),
                 ("Cache-Control", "no-store")])
        return [payload]


def public_config(directory, *, key=None):
    if key is None:
        key = signing_key(directory)
    numbers = key.public_key().public_numbers()
    def encode(n):
        return b64(n.to_bytes((n.bit_length() + 7) // 8, "big"))
    return {"modulus": encode(numbers.n), "exponent": encode(numbers.e)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", required=True, type=Path)
    commands = parser.add_subparsers(dest="action", required=True)
    for action in ("init", "bootstrap", "public-key", "list", "serve"):
        commands.add_parser(action)
    for action in ("enable", "disable"):
        commands.add_parser(action).add_argument("email")
    args = parser.parse_args()
    os.umask(0o077)
    if args.action == "init":
        init(args.data)
    elif args.action == "bootstrap":
        initial_email = os.getenv("BLADESCOPE_INITIAL_EMAIL")
        if initial_email:
            try:
                address = email_address(initial_email)
            except ValueError:
                raise SystemExit("BLADESCOPE_INITIAL_EMAIL is invalid") from None
            with closing(connect(args.data)) as db:
                db.execute("INSERT OR IGNORE INTO account VALUES (?, 1, 1)", (address,))
    elif args.action == "public-key":
        print(json.dumps(public_config(args.data)))
    elif args.action in ("enable", "disable"):
        set_account(args.data, args.email, args.action == "enable")
    elif args.action == "list":
        with closing(connect(args.data)) as db:
            for email, enabled in db.execute("SELECT email, enabled FROM account ORDER BY email"):
                print(f"{'enabled' if enabled else 'disabled'}\t{email}")
    else:
        app = Authorization(args.data)
        from waitress import serve
        serve(app, host=os.getenv("BLADESCOPE_BIND", "127.0.0.1"),
              port=int(os.getenv("PORT") or os.getenv("BLADESCOPE_PORT", "8765")),
              max_request_body_size=4096, channel_timeout=30)


if __name__ == "__main__":
    main()
