const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

(async () => {
  let operations = 0;
  const requests = [];
  const token = 'a'.repeat(64);
  const context = vm.createContext({
    console: { log() {}, error() {} }, Date, Number, Promise, setTimeout, AbortSignal,
    fetch: async (url, options) => { requests.push({ url, ...options }); return { ok: true }; },
    chrome: {
      storage: { local: { setAccessLevel: async () => {}, get: async () => ({ bridgeToken: token }) }, onChanged: { addListener() {} } },
      alarms: { create() {}, onAlarm: { addListener() {} } },
      runtime: { onMessage: { addListener() {} } },
      tabs: { query: async () => { operations++; return []; } }
    }
  });
  const source = fs.readFileSync(path.join(__dirname, '../chrome_extension/background.js'), 'utf8');
  vm.runInContext(source.replace(/pollLoop\(\);\s*$/, ''), context);
  await Promise.resolve();
  await vm.runInContext('handleCommand({ id: "expired", type: "list_tabs", deadline_ms: Date.now() - 1 })', context);
  assert.equal(operations, 0, 'Expired command must not reach Chrome');
  assert.match(JSON.parse(requests[0].body).error, /expired/);
  assert.equal(requests[0].headers.Authorization, `Bearer ${token}`);
  await vm.runInContext('handleCommand({ id: "valid", type: "list_tabs", deadline_ms: Date.now() + 10000 })', context);
  assert.equal(operations, 1);
  assert.equal(JSON.parse(requests[1].body).error, null);
  console.log('Extension pairing header and expiry checks passed');
})().catch(error => { console.error(error); process.exitCode = 1; });
