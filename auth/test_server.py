import concurrent.futures
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

from server import Authorization, OFFLINE_SECONDS, init, public_config, send_mail, set_account


class AuthorizationTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        init(self.directory)
        self.mail = []
        self.now = 1800000000
        self.app = Authorization(self.directory, lambda *mail: self.mail.append(mail), lambda: self.now)
        self.email = "user@example.net"
        self.device = "a" * 64

    def post(self, resource, **fields):
        body = json.dumps(dict(device=self.device, email=self.email, **fields)).encode()
        status = []
        response = self.app({
            "PATH_INFO": "/v1/" + resource, "REQUEST_METHOD": "POST",
            "CONTENT_LENGTH": str(len(body)), "wsgi.input": io.BytesIO(body),
            "REMOTE_ADDR": "192.0.2.1",
        }, lambda line, headers: status.append(int(line[:3])))
        return status[0], json.loads(b"".join(response))

    def activate(self):
        set_account(self.directory, self.email, True)
        self.assertEqual(self.post("code"), (202, {}))
        return self.mail[-1][1]

    def test_account_offline_expiry_and_revocation(self):
        self.assertEqual(self.post("code"), (202, {}))
        self.assertEqual(self.mail, [])
        self.now += 600
        code = self.activate()
        status, response = self.post("verify", code=code)
        self.assertEqual(status, 200)
        token = response["token"]
        claims = self.app.parse(token, self.device, self.now)
        self.assertEqual(claims["exp"] - claims["iat"], OFFLINE_SECONDS)
        self.assertEqual(len(public_config(self.directory)["modulus"]), 512)
        self.assertEqual(self.post("verify", code=code)[0], 401)
        self.now += 86400
        self.assertEqual(self.post("refresh", token=token)[0], 200)
        self.assertEqual(self.post("refresh", token=token[:-4] + "AAAA")[0], 401)
        set_account(self.directory, self.email, False)
        self.assertEqual(self.post("refresh", token=token), (403, {"error": "revoked"}))
        set_account(self.directory, self.email, True)
        self.assertEqual(self.post("refresh", token=token)[0], 403)
        self.now = claims["exp"]
        self.assertEqual(self.post("refresh", token=token)[0], 401)

    def test_code_single_use_even_with_concurrent_requests(self):
        code = self.activate()
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            statuses = list(pool.map(lambda _: self.post("verify", code=code)[0], range(2)))
        self.assertEqual(sorted(statuses), [200, 401])

    def test_device_bound_and_attempt_limited(self):
        code = self.activate()
        self.device = "b" * 64
        self.assertEqual(self.post("verify", code=code)[0], 401)
        self.device = "a" * 64
        wrong = "000000" if code != "000000" else "111111"
        for _ in range(5):
            self.assertEqual(self.post("verify", code=wrong)[0], 401)
        self.assertEqual(self.post("verify", code=code)[0], 401)

    def test_mail_failure_and_expired_code(self):
        set_account(self.directory, self.email, True)
        original = self.app.mail
        def fail(*_):
            raise OSError("simulated")
        self.app.mail = fail
        self.assertEqual(self.post("code")[0], 503)
        self.app.mail = original
        code = self.activate()
        self.now += 600
        self.assertEqual(self.post("verify", code=code)[0], 401)

    def test_request_limits(self):
        self.activate()
        self.assertEqual(self.post("code")[0], 202)
        self.assertEqual(self.post("code")[0], 202)
        self.assertEqual(self.post("code")[0], 429)
        self.assertEqual(len(self.mail), 1)


    def test_bootstrap_does_not_restore_disabled_account(self):
        script = Path(__file__).with_name("server.py")
        command = [sys.executable, str(script), "--data", str(self.directory)]
        env = dict(os.environ, BLADESCOPE_INITIAL_EMAIL=self.email)
        subprocess.run([*command, "bootstrap"], env=env, check=True, capture_output=True)
        set_account(self.directory, self.email, False)
        subprocess.run([*command, "bootstrap"], env=env, check=True, capture_output=True)
        listing = subprocess.check_output([*command, "list"], text=True)
        self.assertEqual(listing.strip(), f"disabled\t{self.email}")

    def test_resend_mail_transport(self):
        opener = MagicMock()
        opener.open.return_value.__enter__.return_value.status = 200
        config = {
            "BLADESCOPE_MAIL_TRANSPORT": "resend",
            "BLADESCOPE_RESEND_FROM": "BladeScope <onboarding@resend.dev>",
            "BLADESCOPE_RESEND_API_KEY": "test-placeholder",
        }
        with patch.dict(os.environ, config), patch("server.build_opener", return_value=opener):
            send_mail(self.email, "012345")
        request = opener.open.call_args.args[0]
        self.assertEqual(request.full_url, "https://api.resend.com/emails")
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.get_header("Authorization"), "Bearer test-placeholder")
        self.assertEqual(json.loads(request.data)["to"], [self.email])
        self.assertIn("012345", json.loads(request.data)["text"])
