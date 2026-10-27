"use strict";

(() => {
  const status = document.getElementById("status");
  const button = document.getElementById("return");
  const manual = document.getElementById("manual");
  const manualUrl = document.getElementById("manual-url");
  const incoming = new URL(window.location.href);
  // QUrlQuery treats '+' literally. URLSearchParams normally treats it as a
  // space, so protect literal plus signs before decoding the browser query.
  const parameters = new URLSearchParams(incoming.search.slice(1).replace(/\+/g, "%2B"));
  const forwarded = ["state", "code", "device_id", "error"];
  const duplicate = forwarded.some(key => parameters.getAll(key).length > 1);
  const hasError = parameters.has("error");
  const valid = incoming.pathname === "/callback" && !incoming.href.includes("#") && !duplicate
    && !!parameters.get("state")
    && (hasError
      ? !parameters.has("code") && !parameters.has("device_id")
      : !!parameters.get("code") && !!parameters.get("device_id"));

  if (!valid) {
    status.textContent = "Нет корректного ответа VK ID. Начните вход заново в приложении.";
    return;
  }

  // The destination cannot be supplied by the query. Exchange happens only in
  // the app holding the PKCE verifier; neither tokens nor ext_id are forwarded.
  const target = new URL("vkm://auth/callback");
  for (const key of forwarded) {
    if (parameters.has(key)) target.searchParams.set(key, parameters.get(key));
  }
  // Encode spaces as %20 as well: Qt does not perform form-style '+' decoding.
  target.search = target.search.replace(/\+/g, "%20");
  // Retain a usable HTTPS callback after removing the query from the address
  // bar. Manual paste in the app validates this registered HTTPS redirect.
  const fallback = new URL(incoming.origin + incoming.pathname);
  fallback.search = target.search;
  manualUrl.value = fallback.href;
  manual.hidden = false;
  button.href = target.href;
  button.hidden = false;
  status.textContent = "Открываем VK. Если переход не сработал, нажмите «Вернуться в приложение».";
  // Keep the button usable after removing credentials from the visible URL.
  // If history manipulation is unavailable, automatic return still works.
  try { window.history.replaceState(null, "", incoming.pathname); } catch (_) {}
  window.setTimeout(() => {
    try { window.location.assign(target.href); }
    catch (_) { status.textContent = "Нажмите «Вернуться в приложение», чтобы продолжить вход."; }
  }, 100);
})();
