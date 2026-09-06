const BRIDGE_URL = "http://127.0.0.1:13371";
const POLL_INTERVAL = 500;
const MAX_HISTORY = 20;

let polling = false;
let startTime = Date.now();
let commandHistory = [];
let commandCount = 0;
let lastError = 'Pairing required';
let token = '';
let connected = false;
chrome.storage.local.setAccessLevel({ accessLevel: 'TRUSTED_CONTEXTS' });
chrome.storage.local.get('bridgeToken').then(v => { token = v.bridgeToken || ''; });
chrome.storage.onChanged.addListener(changes => { if (changes.bridgeToken) token = changes.bridgeToken.newValue || ''; });

function addToHistory(type) {
  commandHistory.unshift({ type, timestamp: Date.now() });
  if (commandHistory.length > MAX_HISTORY) {
    commandHistory.pop();
  }
  commandCount++;
}

async function postResult(id, result, error) {
  try {
    const response = await fetch(`${BRIDGE_URL}/api/ext/result`, {
      method: "POST",
      headers: { "Content-Type": "application/json", Authorization: `Bearer ${token}` },
      signal: AbortSignal.timeout(5000),
      body: JSON.stringify({ id, result: result ?? null, error: error ?? null }),
    });
    if (!response.ok) throw new Error(`Result rejected (${response.status}); inspect state before retrying`);
  } catch (e) {
    lastError = e.message;
    connected = false;
    throw e;
  }
}

async function handleCommand(cmd) {
  const { id, type } = cmd;
  addToHistory(type);

  try {
    if (!Number.isFinite(cmd.deadline_ms) || Date.now() >= cmd.deadline_ms) throw new Error("Command expired before execution");
    let result;

    switch (type) {
      case "list_tabs": {
        const tabs = await chrome.tabs.query({});
        result = { tabs: tabs.map((t) => ({
          id: t.id,
          url: t.url,
          title: t.title,
          active: t.active,
          windowId: t.windowId,
        }))};
        break;
      }

      case "eval_js": {
        const injection = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: async (code) => {
            try {
              const res = eval(code);
              const val = res instanceof Promise ? await res : res;
              return { value: val, error: null };
            } catch (e) {
              return { value: null, error: e.message };
            }
          },
          args: [cmd.js_code || cmd.code || ""],
          world: "MAIN",
        });
        result = injection[0]?.result;
        break;
      }

      case "capture_tab": {
        const tab = cmd.tab_id
          ? await chrome.tabs.get(cmd.tab_id)
          : (await chrome.tabs.query({ active: true }))[0];
        const options = {};
        if (cmd.format === "jpeg") {
          options.format = "jpeg";
          if (cmd.quality) {
            options.quality = cmd.quality;
          }
        } else {
          options.format = "png";
        }
        const dataUrl = await chrome.tabs.captureVisibleTab(tab.windowId, options);
        result = { dataUrl };
        break;
      }

      case "navigate": {
        await chrome.tabs.update(cmd.tab_id, { url: cmd.url });
        result = { success: true };
        break;
      }

      case "create_tab": {
        const newTab = await chrome.tabs.create({ url: cmd.url || "about:blank" });
        result = { id: newTab.id, url: newTab.url, title: newTab.title };
        break;
      }

      case "close_tab": {
        await chrome.tabs.remove(cmd.tab_id);
        result = { success: true };
        break;
      }

      case "reload": {
        await chrome.tabs.reload(cmd.tab_id, { bypassCache: !!cmd.ignore_cache });
        result = { success: true };
        break;
      }

      case "query_selector": {
        const qsResult = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: (selector) => {
            const els = document.querySelectorAll(selector);
            const out = [];
            const limit = Math.min(els.length, 50);
            for (let i = 0; i < limit; i++) {
              const el = els[i];
              const rect = el.getBoundingClientRect();
              out.push({
                tag: el.tagName.toLowerCase(),
                id: el.id || null,
                className: el.className || null,
                textContent: (el.textContent || "").substring(0, 200),
                href: el.href || null,
                src: el.src || null,
                value: el.value !== undefined ? el.value : null,
                type: el.type || null,
                checked: el.checked !== undefined ? el.checked : null,
                disabled: el.disabled !== undefined ? el.disabled : null,
                rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
              });
            }
            return out;
          },
          args: [cmd.selector],
        });
        result = qsResult[0]?.result;
        break;
      }

      case "click_element": {
        const clickResult = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: (selector) => {
            const el = document.querySelector(selector);
            if (!el) return { success: false, error: "Element not found: " + selector };
            el.click();
            return { success: true };
          },
          args: [cmd.selector],
        });
        result = clickResult[0]?.result;
        break;
      }

      case "type_text": {
        const typeResult = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: (selector, text) => {
            const el = document.querySelector(selector);
            if (!el) return { success: false, error: "Element not found: " + selector };
            el.value = text;
            el.dispatchEvent(new Event("input", { bubbles: true }));
            el.dispatchEvent(new Event("change", { bubbles: true }));
            return { success: true };
          },
          args: [cmd.selector, cmd.text],
        });
        result = typeResult[0]?.result;
        break;
      }

      case "get_page_content": {
        const contentResult = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: (mode) => {
            let content;
            if (mode === "text") {
              content = document.body.innerText;
            } else {
              content = document.documentElement.outerHTML;
            }
            return content.substring(0, 5000000);
          },
          args: [cmd.mode || "html"],
        });
        result = contentResult[0]?.result;
        break;
      }

      case "inject_css": {
        await chrome.scripting.insertCSS({
          target: { tabId: cmd.tab_id },
          css: cmd.css,
        });
        result = { success: true };
        break;
      }

      case "wait_for": {
        const timeout = cmd.timeout_ms || 5000;
        const waitResult = await chrome.scripting.executeScript({
          target: { tabId: cmd.tab_id },
          func: (selector, timeoutMs) => {
            return new Promise((resolve) => {
              const start = Date.now();
              const check = () => {
                const el = document.querySelector(selector);
                if (el) {
                  const rect = el.getBoundingClientRect();
                  resolve({
                    found: true,
                    tag: el.tagName.toLowerCase(),
                    id: el.id || null,
                    rect: { x: rect.x, y: rect.y, width: rect.width, height: rect.height },
                  });
                } else if (Date.now() - start >= timeoutMs) {
                  resolve({ found: false, error: "Timeout waiting for: " + selector });
                } else {
                  setTimeout(check, 100);
                }
              };
              check();
            });
          },
          args: [cmd.selector, timeout],
        });
        result = waitResult[0]?.result;
        break;
      }

      case "get_cookies": {
        let url = cmd.url;
        if (!url) {
          const tab = (await chrome.tabs.query({ active: true }))[0];
          url = tab ? tab.url : "";
        }
        if (!url) {
          throw new Error("No URL provided and no active tab found");
        }
        const cookies = await chrome.cookies.getAll({ url });
        result = cookies;
        break;
      }

      case "set_cookie": {
        const cookieDetails = {
          url: cmd.url,
          name: cmd.name,
          value: cmd.value,
        };
        if (cmd.domain) cookieDetails.domain = cmd.domain;
        if (cmd.path) cookieDetails.path = cmd.path;
        const cookie = await chrome.cookies.set(cookieDetails);
        result = { success: true, cookie };
        break;
      }

      case "delete_cookie": {
        await chrome.cookies.remove({ url: cmd.url, name: cmd.name });
        result = { success: true };
        break;
      }

      case "reload_extension": {
        chrome.runtime.reload();
        result = { success: true };
        break;
      }

      default:
        await postResult(id, null, `Unknown command type: ${type}`);
        return;
    }

    await postResult(id, result, null);
    lastError = null;
  } catch (e) {
    lastError = e.message;
    console.error(`[bridge] Command ${type} failed:`, e);
    await postResult(id, null, e.message);
  }
}

async function pollLoop() {
  if (polling) return;
  polling = true;

  while (true) {
    try {
      if (!token) throw new Error("Pairing required");
      const resp = await fetch(`${BRIDGE_URL}/api/ext/poll`, {
        headers: { Authorization: `Bearer ${token}` },
        signal: AbortSignal.timeout(5000),
      });
      if (!resp.ok) throw new Error(`Bridge rejected connection (${resp.status})`);
      connected = true;
      if (resp.ok) {
      const data = await resp.json();
      const cmd = data.command;
        if (cmd && cmd.id && cmd.type) {
          await handleCommand(cmd);
        }
        lastError = null;
      }
    } catch (e) {
      connected = false;
      lastError = e.message;
    }

    await new Promise((r) => setTimeout(r, POLL_INTERVAL));
  }
}

// Keepalive alarm
chrome.alarms.create("keepalive", { periodInMinutes: 0.5 });
chrome.alarms.onAlarm.addListener((alarm) => {
  if (alarm.name === "keepalive") {
    // Keeps the service worker alive
  }
});

// Status message handler
chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (msg.type === "get_status") {
    sendResponse({
      connected,
      startTime,
      commandCount,
      commandHistory: commandHistory.slice(0, 20),
      lastError,
    });
  }
  return true;
});

// Start polling
pollLoop();
