"""Black-box tests of the built binary; all profiles and ports are disposable."""
import http.client
import json
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


def free_port():
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class MCP:
    def __init__(self, *args):
        self.process = subprocess.Popen([BINARY, *map(str, args)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding='utf-8',
            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.responses = queue.Queue()
        self.counter = 0
        def read():
            for line in self.process.stdout:
                self.responses.put(json.loads(line))
        self.reader = threading.Thread(target=read, daemon=True)
        self.reader.start()

    def send(self, value):
        self.process.stdin.write(json.dumps(value) + '\n')
        self.process.stdin.flush()

    def call(self, method, params=None, timeout=15):
        self.counter += 1
        self.send({'jsonrpc': '2.0', 'id': self.counter, 'method': method, 'params': params or {}})
        result = self.responses.get(timeout=timeout)
        assert result['id'] == self.counter, result
        return result

    def tool(self, name, **arguments):
        return self.call('tools/call', {'name': name, 'arguments': arguments})['result']

    def close(self):
        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=5)
            raise AssertionError('Bridge did not stop after stdin EOF')
        self.reader.join(timeout=2)
        self.process.stdout.close()


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.token_file = Path(self.directory.name) / 'pairing-token'
        result = subprocess.run([BINARY, '--init-token', str(self.token_file)], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.token = self.token_file.read_text()
        self.assertNotIn(self.token.encode(), result.stdout + result.stderr)
        self.port, cdp_port = free_port(), free_port()
        self.mcp = MCP('--token-file', self.token_file, '--extension-id', 'a' * 32,
            '--bridge-port', self.port, '--cdp-port', cdp_port)
        self.addCleanup(self.mcp.close)
        deadline = time.monotonic() + 5
        while True:
            try:
                if self.http('GET', '/api/ext/status')[0] == 200:
                    break
            except OSError:
                pass
            if time.monotonic() > deadline:
                self.fail('Extension listener did not start')
            time.sleep(0.03)

    def http(self, method, path, body=None, token=None, headers=None):
        client = http.client.HTTPConnection('127.0.0.1', self.port, timeout=3)
        fields = {'Authorization': 'Bearer ' + (self.token if token is None else token), 'Content-Type': 'application/json'}
        fields.update(headers or {})
        try:
            client.request(method, path, json.dumps(body) if body is not None else None, fields)
            response = client.getresponse()
            data = response.read()
            return response.status, dict(response.getheaders()), json.loads(data) if data else None
        finally:
            client.close()

    def test_private_token_is_not_overwritten(self):
        result = subprocess.run([BINARY, '--init-token', str(self.token_file)], capture_output=True, timeout=5)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.token_file.read_text(), self.token)
        if os.name != 'nt':
            self.assertEqual(self.token_file.stat().st_mode & 0o777, 0o600)

    def test_auth_and_origin_boundary(self):
        self.assertEqual(self.http('GET', '/api/ext/poll', token='wrong')[0], 401)
        self.assertEqual(self.http('GET', '/api/ext/poll', headers={'Host': 'evil.example'})[0], 403)
        for origin in ['https://evil.example', 'null', 'chrome-extension://' + 'b' * 32]:
            code, headers, _ = self.http('GET', '/api/ext/poll', headers={'Origin': origin})
            self.assertEqual(code, 403)
            self.assertNotIn('Access-Control-Allow-Origin', headers)
        origin = 'chrome-extension://' + 'a' * 32
        code, headers, _ = self.http('OPTIONS', '/api/ext/result', token='wrong', headers={'Origin': origin})
        self.assertEqual(code, 204)
        self.assertEqual(headers['Access-Control-Allow-Origin'], origin)

    def test_malformed_results_and_unknown_ids(self):
        for body in [[], None, {'id': 1}]:
            self.assertEqual(self.http('POST', '/api/ext/result', body)[0], 400)
        self.assertEqual(self.http('POST', '/api/ext/result', {'id': 'missing', 'result': {}})[0], 409)

    def test_protocol_errors_do_not_terminate_server(self):
        for value in [None, [], {'jsonrpc': '1.0', 'id': 1, 'method': 'ping'},
            {'jsonrpc': '2.0', 'id': 1, 'method': 7}, {'jsonrpc': '2.0', 'id': 1, 'method': 'ping', 'params': []}]:
            self.mcp.send(value)
            self.assertIn('error', self.mcp.responses.get(timeout=3))
        self.assertEqual(self.mcp.call('ping')['result'], {})
        self.assertEqual(len(self.mcp.call('tools/list')['result']['tools']), 22)
        self.assertTrue(self.mcp.tool('chrome_navigate', tab_id=123, url='about:blank')['isError'])

    def test_extension_roundtrip_and_error_propagation(self):
        self.http('GET', '/api/ext/poll')
        for error in [None, 'Synthetic browser failure']:
            self.mcp.counter += 1
            self.mcp.send({'jsonrpc': '2.0', 'id': self.mcp.counter, 'method': 'tools/call',
                'params': {'name': 'chrome_create_tab', 'arguments': {'url': 'about:blank'}}})
            deadline = time.monotonic() + 3
            command = None
            while not command and time.monotonic() < deadline:
                command = self.http('GET', '/api/ext/poll')[2]['command']
                time.sleep(0.01)
            self.assertIsNotNone(command)
            self.assertGreater(command['deadline_ms'], time.time() * 1000)
            self.assertEqual(self.http('POST', '/api/ext/result', {'id': command['id'],
                'result': {'success': True}, 'error': error})[0], 200)
            result = self.mcp.responses.get(timeout=3)['result']
            self.assertEqual(result['isError'], error is not None)
            self.assertEqual(self.http('POST', '/api/ext/result', {'id': command['id'], 'result': {}})[0], 409)

    def test_batch_prevalidates_every_command(self):
        self.http('GET', '/api/ext/poll')
        for invalid in [None, {'tool': 'missing'}, {'tool': 'chrome_batch', 'arguments': {'commands': '[]'}},
                {'tool': 'chrome_navigate', 'arguments': {'tab_id': 1, 'url': 'about:blank'}}]:
            commands = [{'tool': 'chrome_create_tab', 'arguments': {'url': 'about:blank'}}, invalid]
            result = self.mcp.tool('chrome_batch', commands=json.dumps(commands))
            self.assertTrue(result['isError'])
            self.assertIsNone(self.http('GET', '/api/ext/poll')[2]['command'])
        self.assertTrue(self.mcp.tool('chrome_batch', commands=json.dumps([{'tool': 'chrome_status'}] * 65))['isError'])

    def test_queued_timeout_discards_command(self):
        self.http('GET', '/api/ext/poll')
        result = self.mcp.tool('chrome_create_tab', url='about:blank')
        self.assertTrue(result['isError'])
        content = json.loads(result['content'][0]['text'])
        self.assertEqual(content['delivery_state'], 'not_delivered')
        self.assertIsNone(self.http('GET', '/api/ext/poll')[2]['command'])

    def test_dispatched_timeout_is_uncertain(self):
        self.http('GET', '/api/ext/poll')
        self.mcp.counter += 1
        self.mcp.send({'jsonrpc': '2.0', 'id': self.mcp.counter, 'method': 'tools/call',
            'params': {'name': 'chrome_create_tab', 'arguments': {'url': 'about:blank'}}})
        deadline = time.monotonic() + 3
        command = None
        while not command and time.monotonic() < deadline:
            command = self.http('GET', '/api/ext/poll')[2]['command']
            time.sleep(0.01)
        self.assertIsNotNone(command)
        result = self.mcp.responses.get(timeout=13)['result']
        self.assertTrue(result['isError'])
        self.assertEqual(json.loads(result['content'][0]['text'])['delivery_state'], 'uncertain')
        self.assertEqual(self.http('POST', '/api/ext/result', {'id': command['id'], 'result': {}})[0], 409)


class RealChromeTests(unittest.TestCase):
    def test_disposable_headless_profile(self):
        browser = os.getenv('CHROME_BINARY') or shutil.which('google-chrome') or shutil.which('chromium')
        if not browser:
            candidate = Path(r'C:\Program Files\Google\Chrome\Application\chrome.exe')
            browser = str(candidate) if candidate.is_file() else None
        if not browser:
            self.skipTest('Chrome is not installed; set CHROME_BINARY to enable real-browser verification')
        with tempfile.TemporaryDirectory() as directory:
            profile = Path(directory) / 'profile'
            chrome = subprocess.Popen([browser, '--headless=new', '--no-first-run', '--no-default-browser-check',
                '--disable-gpu', '--remote-debugging-port=0', '--user-data-dir=' + str(profile), 'about:blank'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            mcp = None
            try:
                active_port = profile / 'DevToolsActivePort'
                deadline = time.monotonic() + 15
                while not active_port.exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                self.assertTrue(active_port.exists(), 'Headless Chrome did not start')
                port = int(active_port.read_text().splitlines()[0])
                mcp = MCP('--cdp-port', port)
                def call(name, **arguments):
                    result = mcp.tool(name, **arguments)
                    self.assertFalse(result['isError'], result)
                    return json.loads(result['content'][0]['text'])
                tab = call('chrome_create_tab', url='about:blank')['tab']['id']
                call('chrome_evaluate_js', tab_id=tab, js_code="document.body.innerHTML='<input id=field><button id=button>Go</button>'; document.querySelector('#button').onclick=()=>document.title=document.querySelector('#field').value")
                call('chrome_type_text', tab_id=tab, selector='#field', text='bridge verification')
                call('chrome_click', tab_id=tab, selector='#button')
                self.assertEqual(call('chrome_evaluate_js', tab_id=tab, js_code='document.title')['value'], 'bridge verification')
                call('chrome_close_tab', tab_id=tab)
                result = mcp.tool('chrome_evaluate_js', tab_id=tab, js_code='1')
                self.assertTrue(result['isError'])
            finally:
                if mcp:
                    mcp.close()
                # Only the process launched with this temporary profile is stopped.
                chrome.terminate()
                chrome.wait(timeout=10)
                # Chrome children may take a moment to release profile files.
                time.sleep(0.5)


if __name__ == '__main__':
    unittest.main()
