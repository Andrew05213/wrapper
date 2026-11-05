"""Exercise the production dlopen curl wrapper against local HTTP/TLS fixtures."""
import http.server
import os
from pathlib import Path
import ssl
import subprocess
import tempfile
import threading
import unittest


class CurlOptionsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.work = Path(cls.temp.name)
        repo = Path(__file__).resolve().parents[1]
        cls.binary = cls.work / 'curl-options'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++11', '-O1',
                        '-ffunction-sections', '-fdata-sections', '-I' + str(repo / 'lite'),
                        '-I' + str(repo / 'build/_deps/cjson-src'),
                        str(repo / 'tests/curl_options.cpp'), '-Wl,--gc-sections',
                        '-ldl', '-pthread', '-o', str(cls.binary)], check=True)
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-keyout', str(cls.work / 'key.pem'), '-out', str(cls.work / 'cert.pem'),
                        '-days', '1', '-subj', '/CN=localhost',
                        '-addext', 'subjectAltName=DNS:localhost'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def request(self, tls=False, trusted=False, host='localhost', debug=False):
        class Handler(http.server.BaseHTTPRequestHandler):
            def handle(self):
                try:
                    super().handle()
                except (BrokenPipeError, ConnectionResetError):
                    pass  # expected when curl rejects this fixture's certificate

            def do_POST(self):
                body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
                self.send_response(201)
                reply = b'ok' if body == b'a\x00b' else b'wrong-length'
                self.send_header('Content-Length', str(len(reply)))
                self.end_headers()
                self.wfile.write(reply)

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        if tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(self.work / 'cert.pem', self.work / 'key.pem')
            server.socket = context.wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            env = os.environ.copy()
            env['SSL_CERT_FILE'] = str(self.work / ('cert.pem' if trusted else 'missing.pem'))
            url = f'{"https" if tls else "http"}://{host}:{server.server_port}/'
            result = subprocess.run([str(self.binary), '1' if debug else '0', url],
                                    env=env, text=True, capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            return result.stdout.strip()
        finally:
            server.shutdown()
            server.server_close()
            thread.join()

    def test_real_status_and_binary_post_length(self):
        self.assertEqual(self.request(), '1 201 ok')

    def test_untrusted_certificate_is_rejected(self):
        self.assertEqual(self.request(tls=True), '0 0')

    def test_trusted_certificate_and_hostname(self):
        self.assertEqual(self.request(tls=True, trusted=True), '1 201 ok')
        self.assertEqual(self.request(tls=True, trusted=True, host='127.0.0.1'), '0 0')

    def test_debug_remains_explicit_opt_out(self):
        self.assertEqual(self.request(tls=True, debug=True), '1 201 ok')


if __name__ == '__main__':
    unittest.main()
