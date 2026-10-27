"use strict";

const { test } = require("node:test");
const assert = require("node:assert/strict");
const { readFileSync } = require("node:fs");
const { join } = require("node:path");
const vm = require("node:vm");
const script = readFileSync(join(__dirname, "../web/login-return/callback.js"), "utf8");

function run(query, { blocked = false, historyBlocked = false } = {}) {
  const elements = { status: { textContent: "" }, return: { hidden: true }, manual: { hidden: true }, "manual-url": { value: "" } };
  const navigations = [], replacements = [], timers = [];
  const window = {
    location: {
      href: "https://153-51-114-47.sslip.io/callback" + query,
      assign(url) { if (blocked) throw new Error("gesture required"); navigations.push(url); },
    },
    history: { replaceState(_state, _title, url) { if (historyBlocked) throw new Error("blocked"); replacements.push(url); } },
    setTimeout(fn) { timers.push(fn); },
  };
  vm.runInNewContext(script, { URL, URLSearchParams, window, document: { getElementById: id => elements[id] } });
  for (const timer of timers) timer();
  return { elements, navigations, replacements };
}

test("automatic return uses a fixed URL and forwards only OAuth response fields", () => {
  const result = run("?state=fixture-state&code=fixture-code&device_id=fixture-device&ext_id=private&redirect_uri=https://other.test&access_token=secret&code_verifier=verifier");
  assert.equal(result.navigations.length, 1);
  const url = new URL(result.navigations[0]);
  assert.equal(url.protocol, "vkm:"); assert.equal(url.host, "auth"); assert.equal(url.pathname, "/callback");
  assert.deepEqual([...url.searchParams.keys()], ["state", "code", "device_id"]);
  assert.equal(url.searchParams.get("code"), "fixture-code");
  assert.equal(result.elements.return.href, url.href);
  assert.equal(result.elements.return.hidden, false);
  assert.deepEqual(result.replacements, ["/callback"]);
  const manualUrl = new URL(result.elements["manual-url"].value);
  assert.equal(manualUrl.origin, "https://153-51-114-47.sslip.io");
  assert.equal(manualUrl.pathname, "/callback");
  assert.equal(manualUrl.search, url.search);
  assert.equal(result.elements.manual.hidden, false);
});

test("preserves encoded and literal plus, percent escapes, spaces and Unicode for QUrlQuery", () => {
  const result = run("?state=state&code=code+%2B%2F%252B%26%3D%25%20%D1%8F&device_id=device%2B%25");
  const url = new URL(result.navigations[0]);
  assert.equal(url.searchParams.get("code"), "code++/%2B&=% я");
  assert.equal(url.searchParams.get("device_id"), "device+%");
  assert.ok(!url.search.includes("+")); // Qt does not turn '+' into a space.
});

test("blocked automatic navigation leaves a usable manual return button", () => {
  const result = run("?state=s&code=c&device_id=d", { blocked: true });
  assert.equal(result.navigations.length, 0);
  assert.equal(result.elements.return.hidden, false);
  assert.equal(new URL(result.elements.return.href).searchParams.get("code"), "c");
  assert.match(result.elements.status.textContent, /Нажмите/);
  assert.equal(new URL(result.elements["manual-url"].value).searchParams.get("code"), "c");
});

test("history restrictions do not prevent return", () => {
  assert.equal(run("?state=s&code=c&device_id=d", { historyBlocked: true }).navigations.length, 1);
});

test("VK refusal returns its state and error without a code", () => {
  const result = run("?state=s&error=access_denied&error_description=private");
  const url = new URL(result.navigations[0]);
  assert.equal(url.searchParams.get("error"), "access_denied");
  assert.deepEqual([...url.searchParams.keys()], ["state", "error"]);
});

for (const query of ["", "?code=c&device_id=d", "?state=s&code=c", "?state=s&device_id=d",
  "?state=s&code=c&device_id=d&state=other", "?state=s&code=c&code=other&device_id=d",
  "?state=s&code=c&device_id=d&device_id=other", "?state=s&error=denied&error=other",
  "?state=s&code=c&device_id=d&error=denied", "?state=s&code=c&device_id=d#fragment", "?state=s&code=c&device_id=d#"]) {
  test("rejects malformed response " + query, () => {
    const result = run(query);
    assert.equal(result.navigations.length, 0);
    assert.equal(result.elements.return.hidden, true);
    assert.ok(!result.elements.return.href);
    assert.equal(result.elements.manual.hidden, true);
    assert.equal(result.elements["manual-url"].value, "");
  });
}
