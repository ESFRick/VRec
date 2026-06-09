(function () {
  "use strict";

  var STRINGS = {
    en: {
      "nav.obs": "OBS", "nav.overlay": "Overlay", "nav.advanced": "Advanced", "nav.diagnostics": "Diagnostics",

      "obs.pageDesc": "Controls OBS recording over obs-websocket v5.",
      "obs.setupGuide": "Setup guide",
      "obs.sectionConn": "Connection",
      "obs.host": "Host", "obs.port": "Port", "obs.password": "Password",
      "obs.test": "Test connection",
      "obs.err.host": "Host can't be empty.", "obs.err.port": "Port must be between 1 and 65535.",
      "obs.state.disconnected": "Disconnected", "obs.state.connecting": "Connecting",
      "obs.state.connected": "Connected", "obs.state.tested": "Verified", "obs.state.error": "Error",
      "obs.testSuccess": "Connected to OBS successfully.",
      "obs.testFailed": "Could not connect to OBS: ",
      "obs.fixFields": "Fix the fields above.",

      "overlay.pageDesc": "Control where the VR hand panel appears.",
      "overlay.sectionPlacement": "Placement",
      "overlay.hand": "Overlay hand", "overlay.left": "Left", "overlay.right": "Right",
      "overlay.placement": "Placement preset", "overlay.placementDesc": "Base position of the panel relative to the controller.",
      "overlay.underController": "Under controller", "overlay.wristOutside": "Wrist outside",

      "advanced.pageDesc": "Interface, diagnostics, and reset options.",
      "advanced.sectionInterface": "Interface",
      "advanced.language": "Language", "advanced.english": "English", "advanced.russian": "Russian",
      "advanced.theme": "Theme", "advanced.dark": "Dark", "advanced.light": "Light",
      "advanced.logLevel": "Log level", "advanced.logLevelDesc": "Use debug only when reporting a problem.",
      "advanced.info": "Info", "advanced.debug": "Debug",
      "advanced.closeToTray": "Close to tray",
      "advanced.sectionReset": "Reset",
      "advanced.resetTitle": "Reset all settings", "advanced.resetDesc": "Restore every setting to its default value.",
      "advanced.resetBtn": "Reset", "advanced.resetDone": "Settings reset to defaults.",
      "advanced.resetFailed": "Could not reset settings.",
      "advanced.resetConfirm": "Reset all settings to defaults?",

      "diag.pageDesc": "Technical events and support information.",
      "diag.sectionAbout": "About", "diag.appVersion": "App version", "diag.buildType": "Build type",
      "diag.supportTitle": "Support report", "diag.supportText": "Attach this when you report a bug on GitHub.",
      "diag.export": "Export report", "diag.exportDone": "Report saved.", "diag.exportFailed": "Could not save report.",
      "diag.sectionLog": "Log", "diag.logEmpty": "No log entries yet.",

      "status.idle": "Idle", "status.recording": "Recording",

      "a11y.minimize": "Minimize", "a11y.close": "Close", "a11y.sections": "Sections",
      "a11y.showPassword": "Show password", "a11y.hidePassword": "Hide password",
      "a11y.help": "Help", "a11y.diagnosticLog": "Diagnostic log"
    },
    ru: {
      "nav.obs": "OBS", "nav.overlay": "Оверлей", "nav.advanced": "Дополнительно", "nav.diagnostics": "Диагностика",

      "obs.pageDesc": "Управление записью OBS через obs-websocket v5.",
      "obs.setupGuide": "Инструкция по подключению",
      "obs.sectionConn": "Подключение",
      "obs.host": "Хост", "obs.port": "Порт", "obs.password": "Пароль",
      "obs.test": "Проверить подключение",
      "obs.err.host": "Хост не может быть пустым.", "obs.err.port": "Порт должен быть от 1 до 65535.",
      "obs.state.disconnected": "Отключено", "obs.state.connecting": "Подключение",
      "obs.state.connected": "Подключено", "obs.state.tested": "Проверено", "obs.state.error": "Ошибка",
      "obs.testSuccess": "Успешное подключение к OBS.",
      "obs.testFailed": "Не удалось подключиться к OBS: ",
      "obs.fixFields": "Исправьте поля выше.",

      "overlay.pageDesc": "Настройте, где появляется VR-панель на руке.",
      "overlay.sectionPlacement": "Положение",
      "overlay.hand": "Рука с оверлеем", "overlay.left": "Левая", "overlay.right": "Правая",
      "overlay.placement": "Пресет положения", "overlay.placementDesc": "Базовое положение панели относительно контроллера.",
      "overlay.underController": "Под контроллером", "overlay.wristOutside": "Снаружи запястья",

      "advanced.pageDesc": "Интерфейс, диагностика и сброс.",
      "advanced.sectionInterface": "Интерфейс",
      "advanced.language": "Язык", "advanced.english": "English", "advanced.russian": "Русский",
      "advanced.theme": "Тема", "advanced.dark": "Тёмная", "advanced.light": "Светлая",
      "advanced.logLevel": "Уровень логов", "advanced.logLevelDesc": "Отладочный — только при поиске проблемы.",
      "advanced.info": "Обычный", "advanced.debug": "Отладка",
      "advanced.closeToTray": "Закрывать в трей",
      "advanced.sectionReset": "Сброс",
      "advanced.resetTitle": "Сбросить все настройки", "advanced.resetDesc": "Вернуть все параметры к значениям по умолчанию.",
      "advanced.resetBtn": "Сбросить", "advanced.resetDone": "Настройки сброшены к умолчанию.",
      "advanced.resetFailed": "Не удалось сбросить настройки.",
      "advanced.resetConfirm": "Сбросить все настройки к значениям по умолчанию?",

      "diag.pageDesc": "Технические события и информация для поддержки.",
      "diag.sectionAbout": "О приложении", "diag.appVersion": "Версия приложения", "diag.buildType": "Тип сборки",
      "diag.supportTitle": "Отчёт поддержки", "diag.supportText": "Приложите его, когда сообщаете об ошибке на GitHub.",
      "diag.export": "Экспорт отчёта", "diag.exportDone": "Отчёт сохранён.", "diag.exportFailed": "Не удалось сохранить отчёт.",
      "diag.sectionLog": "Лог", "diag.logEmpty": "Пока нет записей лога.",

      "status.idle": "Ожидание", "status.recording": "Запись",

      "a11y.minimize": "Свернуть", "a11y.close": "Закрыть", "a11y.sections": "Разделы",
      "a11y.showPassword": "Показать пароль", "a11y.hidePassword": "Скрыть пароль",
      "a11y.help": "Справка", "a11y.diagnosticLog": "Диагностический журнал"
    }
  };

  var HELP = {
    "overlay.placement": {
      en: "Selects the base position of the VR panel relative to the chosen controller.",
      ru: "Выбирает базовое положение VR-панели относительно выбранного контроллера."
    },
    "advanced.logLevel": {
      en: "Info keeps normal logs. Debug records more technical detail and is useful when reporting bugs.",
      ru: "Обычный уровень оставляет стандартные логи. Отладка пишет больше технических деталей и полезна при отчётах об ошибках."
    }
  };

  var settings = {
    language: "en",
    obs: { host: "127.0.0.1", port: 4455, password: "" },
    overlay: { hand: "right", placement: "wristOutside" },
    advanced: { logLevel: "info", closeToTray: true }
  };
  var status = { obsConnState: "disconnected", lastError: "", recorderState: "idle", recordingSeconds: 0, log: [] };
  var theme = "dark";       // UI-only preference (no native field) — persisted in localStorage
  var recTimer = null;
  var settingsSaveTimer = null;
  var testPending = false;
  var renderedLog = [];
  var primingFromHost = false;

  var $  = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
  function t(key) { return (STRINGS[settings.language] && STRINGS[settings.language][key]) || STRINGS.en[key] || key; }

  var canSend = !!(window.chrome && window.chrome.webview);
  function send(type, payload) {
    if (canSend) window.chrome.webview.postMessage(JSON.stringify({ type: type, payload: payload == null ? null : payload }));
  }

  function onHostMessage(msg) {
    if (!msg || !msg.type) return;
    switch (msg.type) {
      case "settings": applyServerSettings(msg.payload); break;
      case "status": window.vrec.onStatus(msg.payload || {}); break;
      case "testResult": {
        var tr = msg.payload || {};
        testPending = false;
        renderStatus();
        if (tr.ok) showResult($("#obsResult"), t("obs.testSuccess"), "success");
        else showResult($("#obsResult"), t("obs.testFailed") + (tr.error || ""), "error");
        break;
      }
      case "exportResult": {
        var ex = msg.payload || {};
        showResult($("#diagResult"), ex.ok ? t("diag.exportDone") : t("diag.exportFailed"), ex.ok ? "success" : "error");
        break;
      }
      case "resetResult": {
        var reset = msg.payload || {};
        showResult($("#resetResult"), reset.ok ? t("advanced.resetDone") : t("advanced.resetFailed"), reset.ok ? "success" : "error");
        break;
      }
      case "about": applyAbout(msg.payload); break;
    }
  }

  function applyServerSettings(s) {
    if (!s) return;
    primingFromHost = true;
    showResult($("#obsResult"), "", "");
    settings = {
      language: s.language || "en",
      obs: {
        host: s.obs ? s.obs.host : "127.0.0.1",
        port: s.obs ? s.obs.port : 4455,
        password: s.obs ? s.obs.password : ""
      },
      overlay: {
        hand: s.overlay ? s.overlay.hand : "right",
        placement: s.overlay ? s.overlay.placement : "wristOutside"
      },
      advanced: {
        logLevel: s.advanced ? s.advanced.logLevel : "info",
        closeToTray: !s.advanced || s.advanced.closeToTray !== false
      }
    };
    $("#obsHost").value = settings.obs.host;
    $("#obsPort").value = settings.obs.port;
    $("#obsPass").value = settings.obs.password;
    $("#ovPlace").value = settings.overlay.placement;
    $("#advLog").value = settings.advanced.logLevel;
    $("#advCloseToTray").checked = settings.advanced.closeToTray;
    applyLanguage(settings.language);
    validateHost(); validatePort();
    primingFromHost = false;
  }

  function applyAbout(a) {
    if (!a) return;
    if (a.version) $("#diagVer").textContent = a.version;
    if (a.build) $("#diagBuild").textContent = a.build;
  }

  function applyI18n() {
    document.documentElement.setAttribute("lang", settings.language);
    document.documentElement.setAttribute("data-lang", settings.language);
    $$("[data-i18n]").forEach(function (el) { el.textContent = t(el.getAttribute("data-i18n")); });
    $$("[data-i18n-aria-label]").forEach(function (el) { el.setAttribute("aria-label", t(el.getAttribute("data-i18n-aria-label"))); });
    $$("[data-i18n-title]").forEach(function (el) { el.setAttribute("title", t(el.getAttribute("data-i18n-title"))); });
    $("#obsPassToggle").setAttribute("aria-label", t($("#obsPassToggle").getAttribute("aria-pressed") === "true" ? "a11y.hidePassword" : "a11y.showPassword"));
    $("#advLog").value = settings.advanced.logLevel;
    $("#advCloseToTray").checked = settings.advanced.closeToTray;
    $("#ovPlace").value = settings.overlay.placement;
    syncSeg("#advLang", settings.language);
    syncSeg("#advTheme", theme);
    syncSeg("#ovHand", settings.overlay.hand);
    renderStatus();
    if (!$("#popover").hidden) hidePopover();
  }

  function pad(n) { return (n < 10 ? "0" : "") + n; }
  function fmtTime(s) { return pad(Math.floor(s / 60)) + ":" + pad(s % 60); }

  function renderStatus() {
    var chip = $("#obsStatus");
    var obsState = status.obsConnState;
    chip.setAttribute("data-state", obsState);
    $(".status__txt", chip).textContent = t("obs.state." + obsState);

    var rc = $("#titleRec");
    var rs = status.recorderState;
    if (rs === "recording") {
      rc.hidden = false;
      $(".title__rectxt", rc).textContent = t("status." + rs);
      var time = $("#titleRecTime");
      time.hidden = false;
      time.textContent = fmtTime(status.recordingSeconds);
    } else { rc.hidden = true; }

    var connecting = status.obsConnState === "connecting";
    ["#obsHost", "#obsPort", "#obsPass"].forEach(function (s) { $(s).disabled = connecting || testPending; });
    $("#obsTest").disabled = connecting || testPending;

    renderLog();
  }

  function makeLogLine(raw) {
    var lvl = /\[WARN\]/.test(raw) ? "warn" : /\[ERROR\]/.test(raw) ? "error" : "info";
    var m = raw.match(/^(\[[^\]]+\])\s*(\[[A-Z]+\])?\s*(.*)$/);
    var line = document.createElement("div");
    line.className = "logline logline--" + lvl;
    line.dataset.raw = raw;

    var ts = document.createElement("span");
    ts.className = "logline__t";
    ts.textContent = m ? m[1] : "";
    line.appendChild(ts);
    line.appendChild(document.createTextNode(" "));

    var level = document.createElement("span");
    level.className = "logline__lv";
    level.textContent = m && m[2] ? m[2] : "";
    line.appendChild(level);
    line.appendChild(document.createTextNode(" " + (m ? m[3] : raw)));
    return line;
  }

  function logOverlap(previous, next) {
    var max = Math.min(previous.length, next.length);
    for (var count = max; count > 0; count -= 1) {
      var matches = true;
      for (var i = 0; i < count; i += 1) {
        if (previous[previous.length - count + i] !== next[i]) { matches = false; break; }
      }
      if (matches) return count;
    }
    return 0;
  }

  function renderLog() {
    var box = $("#console"), lines = $("#consoleLines");
    var next = status.log || [];
    if (next.length === renderedLog.length && next.every(function (line, i) { return line === renderedLog[i]; })) return;

    var nearBottom = box.scrollHeight - box.scrollTop - box.clientHeight <= 24;
    var oldTop = box.scrollTop;
    if (!next.length) {
      box.classList.remove("has-lines");
      lines.textContent = "";
      renderedLog = [];
      return;
    }

    box.classList.add("has-lines");
    var overlap = logOverlap(renderedLog, next);
    var removedHeight = 0;

    if (!overlap && renderedLog.length) {
      lines.textContent = "";
      next.forEach(function (raw) { lines.appendChild(makeLogLine(raw)); });
    } else {
      var removeCount = renderedLog.length - overlap;
      for (var i = 0; i < removeCount && lines.firstElementChild; i += 1) {
        removedHeight += lines.firstElementChild.offsetHeight;
        lines.removeChild(lines.firstElementChild);
      }
      for (var j = overlap; j < next.length; j += 1) {
        lines.appendChild(makeLogLine(next[j]));
      }
    }

    renderedLog = next.slice();
    if (nearBottom) {
      box.scrollTop = box.scrollHeight;
    } else if (overlap) {
      box.scrollTop = Math.max(0, oldTop - removedHeight);
    } else {
      box.scrollTop = Math.min(oldTop, Math.max(0, box.scrollHeight - box.clientHeight));
    }
  }

  function startRecTimer() { stopRecTimer(); recTimer = setInterval(function () { status.recordingSeconds += 1; if (status.recorderState === "recording") $("#titleRecTime").textContent = fmtTime(status.recordingSeconds); }, 1000); }
  function stopRecTimer() { if (recTimer) { clearInterval(recTimer); recTimer = null; } }

  function applyTheme(next) {
    theme = next;
    document.documentElement.setAttribute("data-theme", next);
    syncSeg("#advTheme", next);
    try { localStorage.setItem("vrec.theme", next); } catch (e) {}
  }
  function applyLanguage(next) {
    settings.language = next;
    applyI18n();
    try { localStorage.setItem("vrec.lang", next); } catch (e) {}
  }

  function goPage(page, focusPanel) {
    var activePanel = null;
    $$(".nav__item").forEach(function (b) {
      var on = b.getAttribute("data-page") === page;
      b.classList.toggle("is-active", on);
      b.setAttribute("aria-selected", String(on));
      b.tabIndex = on ? 0 : -1;
    });
    $$(".page").forEach(function (p) {
      var on = p.getAttribute("data-page") === page;
      p.hidden = !on; p.classList.toggle("is-active", on);
      if (on) activePanel = p;
    });
    hidePopover();
    try { localStorage.setItem("vrec.page", page); } catch (e) {}
    if (focusPanel && activePanel) activePanel.focus();
  }

  function handleTabKeydown(e) {
    var tabs = $$(".nav__item");
    var current = tabs.indexOf(document.activeElement);
    if (current < 0) return;

    var next = current;
    if (e.key === "ArrowDown" || e.key === "ArrowRight") next = (current + 1) % tabs.length;
    else if (e.key === "ArrowUp" || e.key === "ArrowLeft") next = (current + tabs.length - 1) % tabs.length;
    else if (e.key === "Home") next = 0;
    else if (e.key === "End") next = tabs.length - 1;
    else return;

    e.preventDefault();
    goPage(tabs[next].getAttribute("data-page"));
    tabs[next].focus();
  }

  function syncSeg(sel, val) {
    var seg = $(sel); if (!seg) return;
    $$(".seg__btn", seg).forEach(function (b) {
      var on = b.getAttribute("data-val") === val;
      b.classList.toggle("is-on", on);
      b.setAttribute("aria-checked", String(on));
      b.tabIndex = on ? 0 : -1;
    });
  }
  function segKeydown(seg, onPick) {
    seg.addEventListener("keydown", function (e) {
      var btns = $$(".seg__btn", seg);
      var current = btns.indexOf(document.activeElement);
      if (current < 0) current = btns.findIndex(function (b) { return b.classList.contains("is-on"); });
      var next = current;
      if (e.key === "ArrowRight" || e.key === "ArrowDown") next = (current + 1) % btns.length;
      else if (e.key === "ArrowLeft" || e.key === "ArrowUp") next = (current + btns.length - 1) % btns.length;
      else if (e.key === "Home") next = 0;
      else if (e.key === "End") next = btns.length - 1;
      else return;
      e.preventDefault();
      onPick(btns[next].getAttribute("data-val"));
      btns[next].focus();
    });
  }

  function showPopover(btn) {
    var key = btn.getAttribute("data-help"), pop = $("#popover");
    $("#popoverBody").textContent = (HELP[key] && HELP[key][settings.language]) || "";
    pop.style.visibility = "hidden"; pop.hidden = false;
    var win = $("#window").getBoundingClientRect(), b = btn.getBoundingClientRect();
    var pw = pop.offsetWidth, ph = pop.offsetHeight;
    var left = Math.max(12, Math.min(b.left - win.left + b.width / 2 - 22, win.width - pw - 12));
    var top = b.bottom - win.top + 9;
    if (top + ph > win.height - 8) top = b.top - win.top - ph - 9;
    pop.style.left = left + "px"; pop.style.top = top + "px";
    $(".popover__arrow", pop).style.left = Math.max(10, Math.min((b.left - win.left + b.width / 2) - left - 4, pw - 20)) + "px";
    pop.style.visibility = ""; btn.setAttribute("aria-expanded", "true"); pop._owner = btn;
  }
  function hidePopover() { var pop = $("#popover"); if (pop.hidden) return; pop.hidden = true; if (pop._owner) { pop._owner.setAttribute("aria-expanded", "false"); pop._owner = null; } }

  function validateHost() {
    var el = $("#obsHost"), err = $("#obsHostErr"), ok = el.value.trim().length > 0;
    el.classList.toggle("is-invalid", !ok); el.setAttribute("aria-invalid", String(!ok)); err.hidden = ok; return ok;
  }
  function validatePort() {
    var el = $("#obsPort"), err = $("#obsPortErr"), v = parseInt(el.value, 10);
    var ok = el.value.trim() !== "" && !isNaN(v) && v >= 1 && v <= 65535;
    el.classList.toggle("is-invalid", !ok); el.setAttribute("aria-invalid", String(!ok)); err.hidden = ok; return ok;
  }

  function readSettingsForm() {
    settings.obs.host = $("#obsHost").value;
    settings.obs.port = parseInt($("#obsPort").value, 10) || settings.obs.port;
    settings.obs.password = $("#obsPass").value;
    settings.overlay.placement = $("#ovPlace").value;
    settings.advanced.logLevel = $("#advLog").value;
    settings.advanced.closeToTray = $("#advCloseToTray").checked;
  }

  function pushSettings() {
    if (primingFromHost) return;
    readSettingsForm();
    send("applySettings", settings);
  }

  function scheduleSettings() {
    if (primingFromHost) return;
    if (settingsSaveTimer) clearTimeout(settingsSaveTimer);
    settingsSaveTimer = setTimeout(function () {
      settingsSaveTimer = null;
      pushSettings();
    }, 350);
  }

  function invalidateObsTestResult() {
    showResult($("#obsResult"), "", "");
    renderStatus();
  }

  window.vrec = {
    getSettings: function () { return JSON.parse(JSON.stringify(settings)); },
    applySettings: function (next) { settings = Object.assign(settings, next); send("applySettings", settings); },

    testConnection: function () {
      if (!validateHost() || !validatePort()) { showResult($("#obsResult"), t("obs.fixFields"), "error"); return; }
      readSettingsForm();
      testPending = true;
      renderStatus();
      showResult($("#obsResult"), "", "pending");
      send("testConnection", {
        host: settings.obs.host,
        port: settings.obs.port,
        password: settings.obs.password
      });
    },

    exportSupportReport: function () { showResult($("#diagResult"), "", "pending"); send("exportSupportReport"); },

    resetSettings: function () { showResult($("#resetResult"), "", "pending"); send("resetSettings"); },

    setTheme: function (next) { applyTheme(next); },
    setLanguage: function (next) { applyLanguage(next); pushSettings(); },
    windowMinimize: function () { send("windowMinimize"); },
    windowClose: function () { send("windowClose"); },
    openRepo: function () { send("openRepo"); },

    onStatus: function (next) {
      var was = status.recorderState === "recording";
      status = Object.assign(status, next);
      renderStatus();
      if (status.recorderState === "recording" && !was) startRecTimer();
      if (status.recorderState !== "recording" && was) stopRecTimer();
    }
  };

  function showResult(el, msg, kind) { el.textContent = msg || ""; el.className = "result" + (kind ? " is-" + kind : ""); }
  function init() {
    try {
      var tH = localStorage.getItem("vrec.theme"); if (tH) theme = tH;
      var lN = localStorage.getItem("vrec.lang"); if (lN) settings.language = lN;
    } catch (e) {}

    applyTheme(theme);
    applyI18n();
    var startPage = "obs"; try { startPage = localStorage.getItem("vrec.page") || "obs"; } catch (e) {}
    goPage(startPage);

    $$(".nav__item").forEach(function (b) { b.addEventListener("click", function () { goPage(b.getAttribute("data-page")); }); });
    $(".nav").addEventListener("keydown", handleTabKeydown);

    $("#winMin").addEventListener("click", function () { window.vrec.windowMinimize(); });
    $("#winClose").addEventListener("click", function () { window.vrec.windowClose(); });
    wireDrag();

    $("#obsHost").addEventListener("input", function () { validateHost(); invalidateObsTestResult(); scheduleSettings(); });
    $("#obsPort").addEventListener("input", function () { validatePort(); invalidateObsTestResult(); scheduleSettings(); });
    $("#obsPass").addEventListener("input", function () { invalidateObsTestResult(); scheduleSettings(); });
    $("#obsPassToggle").addEventListener("click", function () {
      var inp = $("#obsPass"), p = this.getAttribute("aria-pressed") === "true";
      this.setAttribute("aria-pressed", String(!p));
      this.setAttribute("aria-label", t(!p ? "a11y.hidePassword" : "a11y.showPassword"));
      inp.type = !p ? "text" : "password";
    });
    $("#obsTest").addEventListener("click", function () { window.vrec.testConnection(); });
    $("#obsHelp").addEventListener("click", function () { window.vrec.openRepo(); });

    $("#ovHand").addEventListener("click", function (e) { var b = e.target.closest(".seg__btn"); if (b) { settings.overlay.hand = b.getAttribute("data-val"); syncSeg("#ovHand", settings.overlay.hand); pushSettings(); } });
    segKeydown($("#ovHand"), function (v) { settings.overlay.hand = v; syncSeg("#ovHand", v); pushSettings(); });
    $("#ovPlace").addEventListener("change", pushSettings);

    $("#advLang").addEventListener("click", function (e) { var b = e.target.closest(".seg__btn"); if (b) window.vrec.setLanguage(b.getAttribute("data-val")); });
    segKeydown($("#advLang"), function (v) { window.vrec.setLanguage(v); });
    $("#advTheme").addEventListener("click", function (e) { var b = e.target.closest(".seg__btn"); if (b) applyTheme(b.getAttribute("data-val")); });
    segKeydown($("#advTheme"), function (v) { applyTheme(v); });
    $("#advLog").addEventListener("change", pushSettings);
    $("#advCloseToTray").addEventListener("change", pushSettings);
    $("#advReset").addEventListener("click", function () {
      if (window.confirm(t("advanced.resetConfirm"))) window.vrec.resetSettings();
    });

    $("#diagExport").addEventListener("click", function () { window.vrec.exportSupportReport(); });

    $$(".help").forEach(function (h) { h.addEventListener("click", function (e) { e.stopPropagation(); if (h.getAttribute("aria-expanded") === "true") hidePopover(); else { hidePopover(); showPopover(h); } }); });
    document.addEventListener("click", function (e) { if (!$("#popover").hidden && !e.target.closest("#popover") && !e.target.closest(".help")) hidePopover(); });
    document.addEventListener("keydown", function (e) { if (e.key === "Escape" && !$("#popover").hidden) hidePopover(); });

    if (canSend) window.chrome.webview.addEventListener("message", function (e) {
      var data = e.data;
      if (typeof data === "string") { try { data = JSON.parse(data); } catch (err) { return; } }
      onHostMessage(data);
    });
    validateHost(); validatePort();
    send("getSettings");
  }

  function wireDrag() {
    var bar = $(".titlebar");
    if (!bar) return;
    bar.addEventListener("pointerdown", function (e) {
      if (e.button !== 0) return;
      if (e.target.closest(".caption")) return;
      send("windowDragStart");
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
})();
