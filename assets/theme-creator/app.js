/* RiptOPL Theme Creator: the page. Everything stays in this browser tab: pictures and fonts are read
 * from the files you pick and written into the zip or tar you download. Nothing is uploaded. */
(function () {
  "use strict";
  var S = window.RTC_SCHEMA, C = window.RTC_CFG, R = window.RTC_RENDER, IO = window.RTC_IO;
  var $ = function (s, r) { return (r || document).querySelector(s); };
  var $$ = function (s, r) { return Array.prototype.slice.call((r || document).querySelectorAll(s)); };
  function el(tag, attrs, kids) {
    var e = document.createElement(tag);
    if (attrs) Object.keys(attrs).forEach(function (k) {
      if (k === "text") e.textContent = attrs[k];
      else if (k === "html") e.innerHTML = attrs[k];
      else if (k.slice(0, 2) === "on") e.addEventListener(k.slice(2), attrs[k]);
      else if (attrs[k] !== undefined && attrs[k] !== null && attrs[k] !== false) e.setAttribute(k, attrs[k]);
    });
    (kids || []).forEach(function (k) { if (k != null) e.appendChild(typeof k === "string" ? document.createTextNode(k) : k); });
    return e;
  }
  var BASE = "assets/thm/";
  var LS_KEY = "riptopl-theme-creator-v1";

  // ---- State -------------------------------------------------------------------------------------------
  var st = {
    name: "MyTheme", map: new Map(), notes: [],
    images: new Map(),   // name -> { data: Uint8Array, img, w, h, url }
    fonts: new Map(),    // file name -> { data, family }
    extras: new Map(),   // path -> Uint8Array (sound/ and anything else carried through)
    editView: "", editFamily: "main", selected: null, hd: false,
    pv: { view: "list", cover: 3, family: "main", page: "main", wide: false, alt: false, desc: false, art: true, discArt: true, selStyle: 7, dim: false, saveIcons: false, sel: 2, frame: 0 },
    builtinCfg: null, isBuiltin: false
  };
  var builtinImgs = {};

  // ---- Assets --------------------------------------------------------------------------------------------
  function loadImg(url) {
    return new Promise(function (res) { var i = new Image(); i.onload = function () { res(i); }; i.onerror = function () { res(null); }; i.src = url; });
  }
  function builtin(name) {
    if (!S.BUILTIN_ALL[name] || S.BUILTIN_EMPTY[name]) return null; // only names OPL has a picture for
    var file = S.BUILTIN_FILE[name] || name;
    var b = builtinImgs[file];
    if (b === undefined) {
      builtinImgs[file] = null;
      loadImg(BASE + encodeURIComponent(file) + ".png").then(function (i) { builtinImgs[file] = i ? { img: i, w: i.naturalWidth, h: i.naturalHeight } : false; schedule(); });
      return null;
    }
    return b || null;
  }
  var assets = {
    user: function (name) { var a = st.images.get(name); return a && a.img ? a : null; },
    builtin: builtin
  };
  function isPng(u8) { return u8[0] === 0x89 && u8[1] === 0x50 && u8[2] === 0x4E && u8[3] === 0x47; }
  // Any picture the browser can open becomes a PNG; a PNG is kept byte for byte.
  async function toPng(file, data) {
    if (isPng(data)) return data;
    var url = URL.createObjectURL(new Blob([data])), img = await loadImg(url); URL.revokeObjectURL(url);
    if (!img) throw new Error("\"" + file + "\" is not a picture this browser can open.");
    var c = document.createElement("canvas"); c.width = img.naturalWidth; c.height = img.naturalHeight;
    c.getContext("2d").drawImage(img, 0, 0);
    var blob = await new Promise(function (r) { c.toBlob(r, "image/png"); });
    return new Uint8Array(await blob.arrayBuffer());
  }
  async function addImage(name, data) {
    var url = URL.createObjectURL(new Blob([data], { type: "image/png" }));
    var img = await loadImg(url);
    var old = st.images.get(name); if (old && old.url) URL.revokeObjectURL(old.url);
    st.images.set(name, { data: data, img: img, w: img ? img.naturalWidth : 0, h: img ? img.naturalHeight : 0, url: url });
  }
  function stem(path) { return path.split("/").pop().replace(/\.[^.]+$/, ""); }
  async function addFont(file, data) {
    var fam = "rtcfont-" + file.replace(/[^A-Za-z0-9]/g, "_");
    try { var ff = new FontFace(fam, data.buffer.slice(data.byteOffset, data.byteOffset + data.byteLength)); await ff.load(); document.fonts.add(ff); }
    catch (e) { toast("The browser could not open the font \"" + file + "\"; it is still packed into the theme, but the preview uses a stand-in.", "warn"); }
    st.fonts.set(file, { data: data, family: fam });
  }

  // ---- Map edits ----------------------------------------------------------------------------------------
  function setKey(k, v) { if (v === "" || v == null) st.map.delete(k); else st.map.set(k, String(v)); changed(); }
  function blockKeys(b) { return Array.from(st.map.keys()).filter(function (k) { return k.indexOf(b + "_") === 0 && C.blockOf(k) === b; }); }
  function blockName(view, fam, n) { return (view ? view + "." : "") + fam + n; }
  function renameBlock(from, to) {
    var moved = blockKeys(from).map(function (k) { return [k, st.map.get(k)]; });
    moved.forEach(function (kv) { st.map.delete(kv[0]); });
    moved.forEach(function (kv) { st.map.set(to + kv[0].slice(from.length), kv[1]); });
  }
  function slotsOf(view, fam) {
    var nums = [];
    st.map.forEach(function (v, k) { var b = C.blockOf(k); if (!b) return; var m = S.BLOCK_RE.exec(b); if ((m[1] || "") === view && m[2] === fam) { var n = +m[3]; if (nums.indexOf(n) < 0) nums.push(n); } });
    return nums.sort(function (a, b) { return a - b; });
  }

  // ---- Change handling -----------------------------------------------------------------------------------
  var pending = false;
  function schedule() { if (!pending) { pending = true; requestAnimationFrame(function () { pending = false; draw(); }); } }
  function changed(opts) {
    st.isBuiltin = false;
    save();
    refreshAll(opts);
  }
  function save() {
    try { localStorage.setItem(LS_KEY, JSON.stringify({ name: st.name, cfg: C.serialize(st.map) })); } catch (e) { /* private window or storage off */ }
  }
  function refreshAll(opts) {
    if (!opts || !opts.keepCfg) $("#cfg-text").value = C.serialize(st.map, "RiptOPL theme: " + st.name);
    renderTheme(); renderElements(); renderImages(); renderFonts(); renderCheck(); schedule();
  }

  // ---- Preview -----------------------------------------------------------------------------------------
  var hits = [], lastBuilt = null;
  function built() {
    return C.build(st.map, { view: st.pv.view, family: st.pv.family, wide: st.pv.wide, desc: st.pv.desc, alt: st.pv.alt, tmpl: st.builtinCfg, isBuiltin: st.isBuiltin });
  }
  function draw() {
    var cv = $("#pv-canvas");
    var b = built(); lastBuilt = b;
    var fonts = {}; st.fonts.forEach(function (f, n) { fonts[n] = f.family; });
    var lct = b.globals.list_cfg_text !== undefined && parseInt(b.globals.list_cfg_text, 10) === 0;
    hits = R.render(cv, b, Object.assign({}, st.pv, { coverCount: st.pv.cover, fonts: fonts, isBuiltin: st.isBuiltin, listCfgHidden: lct && !st.pv.desc }), assets);
    cv.classList.toggle("wide", st.pv.wide);
    var note = b.viewNote;
    if (b.view !== st.pv.view && !note) note = "Showing " + S.VIEW_LABEL[b.view] + ".";
    $("#pv-note").textContent = note || "";
    $("#pv-note").hidden = !note;
    if (st.selected) {
      var h = hits.filter(function (x) { return x.block === st.selected; }).pop();
      if (h) { var c = cv.getContext("2d"); c.save(); c.strokeStyle = "#ffd54a"; c.lineWidth = 2; c.setLineDash([5, 3]); c.strokeRect(h.x, h.y, h.w, h.h); c.restore(); }
    }
  }
  function canvasPoint(ev) {
    var cv = $("#pv-canvas"), r = cv.getBoundingClientRect();
    return { x: (ev.clientX - r.left) * R.W / r.width, y: (ev.clientY - r.top) * R.H / r.height };
  }
  function hitAt(p) {
    for (var i = hits.length - 1; i >= 0; i--) { var h = hits[i]; if (h.type === "Background") continue; if (p.x >= h.x && p.x <= h.x + h.w && p.y >= h.y && p.y <= h.y + h.h) return h; }
    return null;
  }
  var drag = null;
  function initPreview() {
    var cv = $("#pv-canvas");
    cv.addEventListener("pointerdown", function (ev) {
      var p = canvasPoint(ev), h = hitAt(p);
      if (!h || /^\(/.test(h.block)) { if (!h) { st.selected = null; renderElements(); schedule(); } return; }
      if (st.selected !== h.block) { selectBlock(h.block); }
      var e = lastBuilt.elements.filter(function (x) { return x.block === h.block; })[0];
      if (!e) return;
      drag = { block: h.block, start: p, x0: num(e.props.x), y0: num(e.props.y), xs: e.props.x, ys: e.props.y, moved: false, type: e.type };
      cv.setPointerCapture(ev.pointerId);
    });
    cv.addEventListener("pointermove", function (ev) {
      if (!drag) return;
      var p = canvasPoint(ev), dx = Math.round(p.x - drag.start.x), dy = Math.round(p.y - drag.start.y);
      if (!drag.moved && Math.abs(dx) + Math.abs(dy) < 3) return;
      drag.moved = true;
      var t = S.TYPES[drag.type] || {};
      var bx = resolveAxis(drag.xs, t.x, R.W), by = resolveAxis(drag.ys, t.y, R.H);
      var nx = bx + dx, ny = by + dy;
      // keep an edge-anchored value anchored to the same edge
      if (drag.xs !== undefined && /^-/.test(String(drag.xs))) nx = nx - R.W;
      if (drag.ys !== undefined && /^-/.test(String(drag.ys))) ny = ny - R.H;
      var sfx = st.hd ? "_hd" : "";
      st.map.set(drag.block + "_x" + sfx, String(nx)); st.map.set(drag.block + "_y" + sfx, String(ny));
      st.isBuiltin = false;
      schedule();
    });
    cv.addEventListener("pointerup", function () { if (drag && drag.moved) changed(); drag = null; });
  }
  function resolveAxis(v, d, size) {
    if (v === undefined) return d === "MID" ? size >> 1 : (d < 0 ? size + d : d || 0);
    if (String(v).indexOf("POS_MID") === 0) return size >> 1;
    var n = parseInt(v, 10) || 0; return n < 0 ? size + n : n;
  }
  function num(v) { var n = parseInt(v, 10); return isNaN(n) ? 0 : n; }

  function initPreviewControls() {
    var bind = function (id, key, conv) {
      var e = $(id); if (!e) return;
      var get = function () { return e.type === "checkbox" ? e.checked : e.value; };
      e.addEventListener("change", function () { var v = get(); st.pv[key] = conv ? conv(v) : v; schedule(); });
    };
    $("#pv-view").addEventListener("change", function () {
      var v = this.value; st.pv.view = v.replace(/[35]$/, ""); st.pv.cover = /5$/.test(v) ? 5 : 3; schedule();
    });
    $("#pv-page").addEventListener("change", function () {
      var v = this.value; st.pv.family = v; st.pv.page = S.FAMILIES[v].page; schedule();
    });
    bind("#pv-wide", "wide"); bind("#pv-alt", "alt"); bind("#pv-desc", "desc"); bind("#pv-art", "art"); bind("#pv-disc", "discArt");
    bind("#pv-dim", "dim"); bind("#pv-save", "saveIcons");
    $$(".pv-sel").forEach(function (cb) { cb.addEventListener("change", function () {
      var v = 0; $$(".pv-sel").forEach(function (x) { if (x.checked) v |= +x.value; });
      if (!v) { cb.checked = true; v = +cb.value; } // RiptOPL keeps one on
      st.pv.selStyle = v; schedule();
    }); });
    $("#pv-prev").addEventListener("click", function () { st.pv.sel = Math.max(0, st.pv.sel - 1); schedule(); });
    $("#pv-next").addEventListener("click", function () { st.pv.sel = st.pv.sel + 1; if (st.pv.sel > 11) st.pv.sel = 11; schedule(); });
    $("#pv-png").addEventListener("click", function () { $("#pv-canvas").toBlob(function (b) { IO.download(b, "thm_" + safeName() + "-preview.png"); }); });
    setInterval(function () { st.pv.frame++; if (lastBuilt && lastBuilt.elements.some(function (e) { return e.type === "LoadingIcon"; })) schedule(); }, 180);
  }

  // ---- Tabs -------------------------------------------------------------------------------------------------
  function initTabs() {
    $$(".tc-tabs button").forEach(function (b) {
      b.addEventListener("click", function () { showTab(b.dataset.tab); });
    });
  }
  function showTab(name) {
    $$(".tc-tabs button").forEach(function (x) { x.setAttribute("aria-selected", x.dataset.tab === name ? "true" : "false"); });
    $$(".tc-panel").forEach(function (p) { p.hidden = p.id !== "tab-" + name; });
  }

  // ---- Start tab ---------------------------------------------------------------------------------------------
  var STARTERS = {
    blank: "use_default=1\nmain0:\n\ttype=Background\nmain1:\n\ttype=MenuText\n\tx=POS_MID\n\ty=24\nmain2:\n\ttype=ItemsList\n\tx=42\n\ty=70\n\twidth=360\n\theight=330\nmain3:\n\ttype=ItemCover\n\tdefault=cover\n\tx=520\n\ty=240\n\twidth=180\n\theight=256\nappsMain3:\n\ttype=ItemCover\n\tdefault=coverapp\n\tx=520\n\ty=240\n\twidth=180\n\theight=180\nmain4:\n\ttype=HintText\nmain5:\n\ttype=LoadingIcon\ninfo0:\n\ttype=Background\ninfo1:\n\ttype=AttributeText\n\tattribute=#Name\n\tx=POS_MID\n\ty=40\n\tdisplay=2\ninfo2:\n\ttype=ItemCover\n\tdefault=cover\n\tx=140\n\ty=220\n\twidth=180\n\theight=256\ninfo3:\n\ttype=AttributeText\n\tattribute=Developer\n\tx=280\n\ty=120\n\taligned=0\ninfo4:\n\ttype=AttributeText\n\tattribute=Genre\n\tx=280\n\ty=145\n\taligned=0\ninfo5:\n\ttype=AttributeText\n\tattribute=Release\n\tx=280\n\ty=170\n\taligned=0\ninfo6:\n\ttype=AttributeText\n\tattribute=Description\n\tx=280\n\ty=200\n\taligned=0\n\twidth=330\n\theight=180\n\twrap=1\ninfo7:\n\ttype=InfoHintText\n",
    coverflow: "views=coverflow,list\ndefault_view=coverflow\nmain0:\n\ttype=Background\nmain1:\n\ttype=MenuText\n\tx=POS_MID\n\ty=24\nmain2:\n\ttype=Coverflow\n\tdefault=cover\n\treflection=1\n\ty=233\n\twidth=184\n\theight=256\nappsMain2:\n\ttype=Coverflow\n\tdefault=coverapp\n\treflection=1\n\ty=239\n\twidth=184\n\theight=184\nvcdMain2:\n\ttype=Coverflow\n\tdefault=cover\n\treflection=1\n\ty=239\n\twidth=184\n\theight=184\nmain3:\n\ttype=ItemsList\n\tx=POS_MID\n\ty=60\n\twidth=420\n\theight=24\n\taligned=1\nmain4:\n\ttype=HintText\nmain5:\n\ttype=LoadingIcon\nlist.main2:\n\ttype=ItemCover\n\tdefault=cover\n\tx=520\n\ty=240\n\twidth=180\n\theight=256\nlist.appsMain2:\n\ttype=ItemCover\n\tdefault=coverapp\n\tx=520\n\ty=240\n\twidth=180\n\theight=180\nlist.vcdMain2:\n\ttype=ItemCover\n\tdefault=cover\n\tx=520\n\ty=240\n\twidth=180\n\theight=180\nlist.main3:\n\ttype=ItemsList\n\tx=42\n\ty=70\n\twidth=360\n\theight=330\n",
    grid: "views=grid,list\ndefault_view=grid\nmain0:\n\ttype=Background\nmain1:\n\ttype=Grid\n\tdefault=cover\n\trows=3\n\tcolumns=4\n\tspacing=12\n\ty=250\n\twidth=80\n\theight=111\n\tcoverless=name\nmain2:\n\ttype=ItemsList\n\ty=60\n\tx=POS_MID\n\taligned=1\n\twidth=420\n\theight=24\nmain3:\n\ttype=HintText\nlist.main1:\n\ttype=ItemCover\n\tdefault=cover\n\tx=520\n\ty=240\n\twidth=180\n\theight=256\nlist.main2:\n\ttype=ItemsList\n\tx=42\n\ty=70\n\twidth=360\n\theight=330\n",
    psbbn: "main0:\n\ttype=Background\n\tdefault=psbbn_bg\n\taligned=0\n\twidth=DIM_INF\n\theight=DIM_INF\nmain1:\n\ttype=PSBBNFlow\n\tdefault=cover\nappsMain1:\n\ttype=PSBBNFlow\n\tdefault=coverapp\nvcdMain1:\n\ttype=PSBBNFlow\n\tdefault=cover\nmain2:\n\ttype=ItemsList\n\tx=340\n\ty=114\n\twidth=260\n\theight=24\nmain3:\n\ttype=HintText\n"
  };
  async function startFrom(kind) {
    if (st.map.size && !confirm("Replace the theme you are editing? Download it first if you want to keep it.")) return;
    clearAll();
    if (kind === "builtin") {
      if (!st.builtinCfg) { toast("The built-in theme file did not load. Check your connection and reload the page.", "bad"); return; }
      st.map = new Map(st.builtinCfg); st.name = "MyRiptOPL";
      // A disk theme only gets OPL's own badge pictures with badge_set=ripps2 (themes.cpp thmLoad)
      if (!st.map.has("badge_set")) st.map.set("badge_set", "ripps2");
      // bezel_top/bottom and psbbn_bg are built in, so nothing needs copying
      toast("Loaded the built-in RiptOPL theme: four views in one file. Its pictures are OPL's own built-in ones until you replace them on the Images tab.");
    } else {
      st.map = C.parse(STARTERS[kind]).map; st.name = kind === "blank" ? "MyTheme" : "My" + S.VIEW_LABEL[kind].replace(/\s/g, "");
    }
    st.selected = null; st.editView = ""; st.editFamily = "main";
    if (kind !== "builtin" && kind !== "blank") { st.pv.view = kind; $("#pv-view").value = kind === "coverflow" ? "coverflow3" : kind; }
    else { st.pv.view = "list"; $("#pv-view").value = "list"; }
    $("#th-name").value = st.name;
    changed(); showTab("elements");
  }
  function clearAll() {
    st.images.forEach(function (a) { if (a.url) URL.revokeObjectURL(a.url); });
    st.map = new Map(); st.images = new Map(); st.fonts = new Map(); st.extras = new Map();
  }
  async function importFiles(list) {
    // list: [{path, data}] from a zip, tar or folder. Strip one enclosing folder.
    var cfgEntry = list.filter(function (f) { return /(^|\/)conf_theme\.cfg$/i.test(f.path); }).sort(function (a, b) { return a.path.length - b.path.length; })[0];
    if (!cfgEntry) throw new Error("No conf_theme.cfg in it. A theme folder holds conf_theme.cfg next to its pictures.");
    var root = cfgEntry.path.slice(0, cfgEntry.path.length - "conf_theme.cfg".length);
    clearAll();
    var folder = root.replace(/\/$/, "").split("/").pop() || "";
    st.name = folder.replace(/^thm_/i, "") || st.name;
    var parsed = C.parse(new TextDecoder().decode(cfgEntry.data));
    st.map = parsed.map;
    for (var i = 0; i < list.length; i++) {
      var f = list[i]; if (f.path.indexOf(root) !== 0 || f === cfgEntry) continue;
      var rel = f.path.slice(root.length);
      if (/^[^/]+\.png$/i.test(rel)) await addImage(stem(rel), f.data);
      else if (/^[^/]+\.(ttf|otf)$/i.test(rel)) await addFont(rel, f.data);
      else st.extras.set(rel, f.data);
    }
    $("#th-name").value = st.name;
    st.selected = null;
    changed();
    toast("Opened \"" + st.name + "\": " + st.images.size + " pictures, " + st.fonts.size + " fonts, " + st.extras.size + " other files.");
    showTab("elements");
  }
  function initStart() {
    $$("[data-start]").forEach(function (b) { b.addEventListener("click", function () { startFrom(b.dataset.start); }); });
    $("#imp-file").addEventListener("change", async function () {
      var f = this.files[0]; this.value = ""; if (!f) return;
      try {
        var buf = await f.arrayBuffer();
        if (/\.zip$/i.test(f.name)) await importFiles(await IO.unzip(buf));
        else if (/\.tar$/i.test(f.name)) await importFiles(IO.untar(buf).map(function (x) { return x; }).concat(/thm_/i.test(f.name) ? [] : []), f.name);
        else if (/\.cfg$/i.test(f.name)) await importFiles([{ path: "conf_theme.cfg", data: new Uint8Array(buf) }]);
        else throw new Error("Pick a .zip, a .tar or a conf_theme.cfg.");
        if (/\.tar$/i.test(f.name) && /^thm_/i.test(f.name)) { st.name = f.name.replace(/^thm_/i, "").replace(/\.tar$/i, ""); $("#th-name").value = st.name; save(); }
      } catch (e) { toast(e.message, "bad"); }
    });
    $("#imp-dir").addEventListener("change", async function () {
      var files = Array.prototype.slice.call(this.files); this.value = "";
      try {
        var list = [];
        for (var i = 0; i < files.length; i++) list.push({ path: files[i].webkitRelativePath || files[i].name, data: new Uint8Array(await files[i].arrayBuffer()) });
        await importFiles(list);
      } catch (e) { toast(e.message, "bad"); }
    });
  }

  // ---- Theme tab (global keys) ---------------------------------------------------------------------------
  function renderTheme() {
    var box = $("#theme-fields"); box.innerHTML = "";
    var groups = {};
    Object.keys(S.GLOBALS).forEach(function (k) { var G = S.GLOBALS[k]; if (G.extra) return; (groups[G.group] = groups[G.group] || []).push(k); });
    Object.keys(groups).forEach(function (gname) {
      var fs = el("fieldset", { "class": "tc-fs" }, [el("legend", { text: gname })]);
      groups[gname].forEach(function (k) { fs.appendChild(field(k, S.GLOBALS[k], st.map.get(k), function (v) { setKey(k, v); }, { global: true })); });
      box.appendChild(fs);
    });
  }

  // A form row for one key. spec.kind decides the input.
  function field(key, spec, value, onSet, ctx) {
    var id = "f-" + key.replace(/[^A-Za-z0-9]/g, "_");
    var label = el("label", { "for": id }, [spec.label || key, " ", el("code", { text: key })]);
    var row = el("div", { "class": "tc-row" + (value !== undefined ? " set" : "") }, [label]);
    var input, def = spec.def;
    var commit = function (v) { onSet(v); };
    switch (spec.kind) {
      case "bool":
        input = el("select", { id: id }, [el("option", { value: "", text: "(not set" + (def !== undefined ? ": " + (def === "1" ? "on" : "off") : "") + ")" }), el("option", { value: "1", text: "On (1)" }), el("option", { value: "0", text: "Off (0)" })]);
        input.value = value === undefined ? "" : (value === "0" ? "0" : "1");
        input.addEventListener("change", function () { commit(input.value); });
        break;
      case "enum":
        input = el("select", { id: id }, spec.values.map(function (v) { return el("option", { value: v, text: v === "" ? "(not set)" : (spec.names && spec.names[v] ? spec.names[v] + " (" + v + ")" : v) }); }));
        if (value !== undefined && spec.values.indexOf(value) < 0) input.appendChild(el("option", { value: value, text: value + " (unknown)" }));
        input.value = value === undefined ? "" : value;
        input.addEventListener("change", function () { commit(input.value); });
        break;
      case "type":
        input = el("select", { id: id }, S.TYPE_NAMES.map(function (v) { return el("option", { value: v, text: v }); }));
        input.value = value || "";
        input.addEventListener("change", function () { commit(input.value); });
        break;
      case "color":
        var txt = el("input", { id: id, type: "text", value: value || "", placeholder: def || "#RRGGBB", size: 9, spellcheck: "false" });
        var pick = el("input", { type: "color", value: /^#[0-9a-f]{6}$/i.test(value || def || "") ? (value || def) : "#808080", title: "Pick a colour" });
        pick.addEventListener("input", function () { txt.value = pick.value.toUpperCase(); });
        pick.addEventListener("change", function () { commit(pick.value.toUpperCase()); });
        txt.addEventListener("change", function () { commit(txt.value.trim()); });
        input = el("span", { "class": "tc-color" }, [pick, txt]);
        break;
      case "image":
        input = imageSelect(id, value, commit);
        break;
      case "font":
        input = el("select", { id: id }, [el("option", { value: "", text: "(not set: built-in font)" }), el("option", { value: "builtin", text: "builtin (OPL's own font)" }),
          el("option", { value: "builtin:roboto", text: "builtin:roboto" }), el("option", { value: "builtin:ripflow", text: "builtin:ripflow" }), el("option", { value: "builtin:ripflowbold", text: "builtin:ripflowbold" })]
          .concat(Array.from(st.fonts.keys()).map(function (f) { return el("option", { value: f, text: f + " (your font)" }); })));
        if (value && !Array.prototype.some.call(input.options, function (o) { return o.value === value; })) input.appendChild(el("option", { value: value, text: value + " (missing)" }));
        input.value = value || "";
        input.addEventListener("change", function () { commit(input.value); });
        break;
      case "attr":
        var listId = id + "-l";
        input = el("span", {}, [el("input", { id: id, type: "text", value: value || "", list: listId, size: 14 }), el("datalist", { id: listId }, (ctx && ctx.type === "AttributeImage" ? S.ATTR_IMAGE : S.ATTR_TEXT).map(function (v) { return el("option", { value: v }); }))]);
        input.firstChild.addEventListener("change", function () { commit(this.value.trim()); });
        break;
      case "list":
        input = el("span", { "class": "tc-checks" }, spec.values.map(function (v) {
          var on = String(value || "").split(/[, \t]+/).indexOf(v) >= 0;
          var cb = el("input", { type: "checkbox", value: v, checked: on ? "checked" : null });
          cb.addEventListener("change", function () {
            var vals = $$("input", input).filter(function (x) { return x.checked; }).map(function (x) { return x.value; });
            commit(vals.join(","));
          });
          return el("label", {}, [cb, " " + S.VIEW_LABEL[v]]);
        }));
        break;
      default: // int, pos, dim, str
        var ph = "";
        if (ctx && ctx.type && S.TYPES[ctx.type]) {
          var t = S.TYPES[ctx.type], d = key === "x" ? t.x : key === "y" ? t.y : key === "width" ? t.w : key === "height" ? t.h : undefined;
          if (d !== undefined) ph = d === null ? "picture's own size" : d === "MID" ? "POS_MID" : d === "INF" ? "DIM_INF" : String(d);
        }
        if (def !== undefined && !ph) ph = String(def);
        var opts = spec.kind === "pos" ? ["POS_MID"] : spec.kind === "dim" ? ["DIM_INF"] : (spec.suggest || []);
        var dl = opts.length ? el("datalist", { id: id + "-l" }, opts.map(function (o) { return el("option", { value: o }); })) : null;
        var inp = el("input", { id: id, type: "text", value: value || "", placeholder: ph, size: spec.kind === "str" ? 22 : 9, list: dl ? id + "-l" : null, spellcheck: "false", inputmode: spec.kind === "int" ? "numeric" : null });
        inp.addEventListener("change", function () { commit(inp.value.trim()); });
        input = el("span", {}, [inp, dl]);
    }
    row.appendChild(input);
    if (spec.help) row.appendChild(el("small", { text: spec.help }));
    if (value !== undefined) row.appendChild(el("button", { type: "button", "class": "tc-x", title: "Remove this key (use the default)", "aria-label": "Remove " + key, text: "×", onclick: function () { onSet(""); } }));
    return row;
  }
  function imageSelect(id, value, commit) {
    var sel = el("select", { id: id }, [el("option", { value: "", text: "(none)" })]);
    var mine = el("optgroup", { label: "Your pictures" });
    Array.from(st.images.keys()).sort().forEach(function (n) { mine.appendChild(el("option", { value: n, text: n + ".png" })); });
    if (mine.children.length) sel.appendChild(mine);
    Object.keys(S.BUILTIN).forEach(function (gname) {
      var og = el("optgroup", { label: "Built in: " + gname });
      S.BUILTIN[gname].forEach(function (n) { if (!st.images.has(n)) og.appendChild(el("option", { value: n, text: n + (S.BUILTIN_EMPTY[n] ? " (no built-in picture)" : "") })); });
      sel.appendChild(og);
    });
    if (value && !Array.prototype.some.call(sel.options, function (o) { return o.value === value; })) sel.appendChild(el("option", { value: value, text: value + " (missing!)" }));
    sel.value = value || "";
    sel.addEventListener("change", function () { commit(sel.value); });
    var up = el("input", { type: "file", accept: "image/*", hidden: "hidden" });
    up.addEventListener("change", async function () {
      var f = up.files[0]; up.value = ""; if (!f) return;
      try { var n = uniqueName(cleanName(stem(f.name))); await addImage(n, await toPng(f.name, new Uint8Array(await f.arrayBuffer()))); commit(n); }
      catch (e) { toast(e.message, "bad"); }
    });
    return el("span", { "class": "tc-img" }, [sel, el("button", { type: "button", "class": "tc-mini", text: "Add picture…", onclick: function () { up.click(); } }), up]);
  }
  function cleanName(n) { return n.replace(/[^A-Za-z0-9_#\- ]/g, "_").slice(0, 40) || "image"; }
  function uniqueName(n) { return n; } // same name = replace, which is what people want when re-adding a file
  function safeName() { return (st.name || "MyTheme").replace(/[^A-Za-z0-9 _\-.]/g, "_").trim() || "MyTheme"; }

  // ---- Elements tab -------------------------------------------------------------------------------------
  function renderElements() {
    var vSel = $("#el-view"), fSel = $("#el-family");
    vSel.value = st.editView; fSel.value = st.editFamily;
    var list = $("#el-list"); list.innerHTML = "";
    var b = C.build(st.map, { view: st.editView || "list", family: st.editFamily, wide: false, desc: false, alt: false, tmpl: null, isBuiltin: true });
    // build() with isBuiltin skips fallback synthesis, so this lists exactly what the file says.
    var famPrefix = st.editView ? st.editView + "." : "";
    var own = st.editFamily;
    if (!st.editView) b = buildBase(st.editFamily);
    b.elements.forEach(function (e) {
      var m = S.BLOCK_RE.exec(e.block), isOwn = (m[1] || "") === st.editView && m[2] === own;
      var from = isOwn ? "" : "from " + e.block;
      var li = el("li", { "class": "tc-el" + (st.selected === e.block ? " sel" : "") + (isOwn ? "" : " inherited") + (e.off ? " off" : ""), tabindex: "0" }, [
        el("span", { "class": "slot", text: String(e.slot) }),
        el("span", { "class": "ty", text: e.off ? "(hidden)" : e.type || "?" }),
        el("span", { "class": "sum", text: summary(e) }),
        el("span", { "class": "from", text: from })
      ]);
      li.addEventListener("click", function () { selectBlock(e.block); });
      li.addEventListener("keydown", function (ev) { if (ev.key === "Enter") selectBlock(e.block); });
      list.appendChild(li);
    });
    if (!b.elements.length) list.appendChild(el("li", { "class": "tc-empty", text: "No blocks here yet. Add an element below." }));
    renderProps();
  }
  function buildBase(fam) {
    // the base layout of one family: its own blocks over its fallback chain, no view prefix
    var chain = S.FAMILIES[fam].chain, out = [];
    for (var j = 0; j < 64; j++) {
      var bl = null;
      for (var c = 0; c < chain.length; c++) if (C.present(st.map, chain[c] + j)) { bl = chain[c] + j; break; }
      if (!bl) break;
      var props = {}; st.map.forEach(function (v, k) { if (k.indexOf(bl + "_") === 0 && C.blockOf(k) === bl) props[k.slice(bl.length + 1)] = v; });
      out.push({ block: bl, slot: j, type: props.type, props: props, off: props.enabled === "0" && !props.type });
    }
    return { elements: out };
  }
  function summary(e) {
    var p = e.props, bits = [];
    if (p["default"]) bits.push(p["default"]);
    if (p.value) bits.push("“" + p.value + "”");
    if (p.attribute) bits.push(p.attribute);
    if (p.x !== undefined || p.y !== undefined) bits.push((p.x || "·") + "," + (p.y || "·"));
    if (p.devices) bits.push("devices=" + p.devices);
    if (p.enabled === "0") bits.push("hidden");
    return bits.join("  ");
  }
  function selectBlock(b) {
    st.selected = b;
    var m = S.BLOCK_RE.exec(b);
    if (m) {
      var fam = m[2];
      // show the block in the list that contains it
      if (st.editView !== (m[1] || "") || !chainHas(st.editFamily, fam)) { st.editView = m[1] || ""; st.editFamily = fam; }
    }
    showTab("elements"); renderElements(); schedule();
    var s = $(".tc-el.sel"); if (s && s.scrollIntoView) s.scrollIntoView({ block: "nearest" });
  }
  function chainHas(fam, other) { return S.FAMILIES[fam].chain.indexOf(other) >= 0; }
  function renderProps() {
    var box = $("#el-props"); box.innerHTML = "";
    var b = st.selected;
    if (!b || !S.BLOCK_RE.test(b)) { box.appendChild(el("p", { "class": "tc-hint", text: "Pick an element in the list, or click it in the preview. Drag it in the preview to move it." })); return; }
    var m = S.BLOCK_RE.exec(b), type = st.map.get(b + "_type");
    var isOwn = (m[1] || "") === st.editView && m[2] === st.editFamily;
    var head = el("div", { "class": "tc-prophead" }, [el("h3", {}, [el("code", { text: b }), " ", type || "(hidden slot)"]), el("p", { "class": "tc-hint", text: (S.TYPES[type] || {}).desc || "" })]);
    box.appendChild(head);
    var acts = el("div", { "class": "tc-acts" });
    if (!isOwn) acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Change it only for " + famLabel(), title: "Copy this block to " + blockName(st.editView, st.editFamily, m[3]) + " so changes apply here only", onclick: function () {
      var to = blockName(st.editView, st.editFamily, m[3]);
      blockKeys(b).forEach(function (k) { st.map.set(to + k.slice(b.length), st.map.get(k)); });
      st.selected = to; changed();
    } }));
    acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: st.map.get(b + "_enabled") === "0" ? "Show" : "Hide", onclick: function () { setKey(b + "_enabled", st.map.get(b + "_enabled") === "0" ? "" : "0"); } }));
    if (isOwn) {
      acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Move up", onclick: function () { moveBlock(b, -1); } }));
      acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Move down", onclick: function () { moveBlock(b, 1); } }));
      acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Duplicate", onclick: function () { dupBlock(b); } }));
      acts.appendChild(el("button", { type: "button", "class": "tc-mini danger", text: "Delete", onclick: function () { deleteBlock(b); } }));
    }
    box.appendChild(acts);
    var hdl = el("label", { "class": "tc-hdtoggle" }, [el("input", { type: "checkbox", checked: st.hd ? "checked" : null, onchange: function () { st.hd = this.checked; if (st.hd && !st.pv.wide) { st.pv.wide = true; $("#pv-wide").checked = true; } renderProps(); schedule(); } }), " Edit the 16:9 values (", el("code", { text: "_hd" }), " keys; Widescreen on)"]);
    box.appendChild(hdl);
    if (!type && st.map.get(b + "_enabled") === "0") return;
    var keys = propsFor(type);
    var form = el("div", { "class": "tc-form" });
    keys.forEach(function (k) {
      var spec = (type === "Grid" && S.GRID_ONLY_NAMES[k]) || S.PROPS[k];
      var key = b + "_" + k + (st.hd && k !== "type" ? "_hd" : "");
      var v = st.map.get(key);
      var shown = spec;
      if (st.hd && k !== "type" && st.map.has(b + "_" + k)) shown = Object.assign({}, spec, { def: st.map.get(b + "_" + k), help: (spec.help ? spec.help + " " : "") + "4:3 value: " + st.map.get(b + "_" + k) });
      form.appendChild(field(k, shown, v, function (val) {
        if (k === "type" && !val) return;
        setKey(key, val);
      }, { type: type }));
    });
    box.appendChild(form);
    // keys the form does not know, shown raw so nothing is hidden
    var extra = blockKeys(b).map(function (k) { return k.slice(b.length + 1); }).filter(function (k) { return keys.indexOf(k.replace(/_(hd|sd|desc)$/, "")) < 0 || /_(sd|desc)$/.test(k) || (!st.hd && /_hd$/.test(k)); });
    if (extra.length) {
      var ex = el("div", { "class": "tc-extra" }, [el("h4", { text: "Other keys in this block" })]);
      extra.forEach(function (k) { ex.appendChild(field(k, { kind: "str", label: "" }, st.map.get(b + "_" + k), function (val) { setKey(b + "_" + k, val); })); });
      box.appendChild(ex);
    }
  }
  function famLabel() { return (st.editView ? S.VIEW_LABEL[st.editView] + ": " : "") + S.FAMILIES[st.editFamily].label; }
  function propsFor(type) {
    var t = S.TYPES[type] || {};
    var keys = ["type", "x", "y", "width", "height", "aligned", "scaled"];
    if (t.text || type === "HintText" || type === "InfoHintText" || t.presenter) keys.push("font");
    if (t.text) keys.push("color");
    Object.keys(S.PROPS).forEach(function (k) {
      var f = S.PROPS[k]["for"]; if (!f || keys.indexOf(k) >= 0) return;
      if (f === "image" && t.image) keys.push(k);
      else if (f === "text" && t.text) keys.push(k);
      else if (f === "overlay" && (t.image && type !== "Background")) keys.push(k);
      else if (Array.isArray(f) && f.indexOf(type) >= 0) keys.push(k);
    });
    if (type === "Grid") { keys.push("title", "devices"); }
    keys.push("enabled");
    return keys;
  }
  function moveBlock(b, d) {
    var m = S.BLOCK_RE.exec(b), view = m[1] || "", fam = m[2], n = +m[3], to = n + d;
    if (to < 0) return;
    var other = blockName(view, fam, to), tmp = "__tmp" + Date.now();
    renameBlock(b, tmp); renameBlock(other, b); renameBlock(tmp, other);
    st.selected = other; changed();
  }
  function dupBlock(b) {
    var m = S.BLOCK_RE.exec(b), view = m[1] || "", fam = m[2];
    var slots = slotsOf(view, fam), next = (slots.length ? slots[slots.length - 1] : -1) + 1;
    if (!view && (fam === "main" || fam === "info")) next = firstFree(view, fam);
    var to = blockName(view, fam, next);
    blockKeys(b).forEach(function (k) { st.map.set(to + k.slice(b.length), st.map.get(k)); });
    st.selected = to; changed();
  }
  function deleteBlock(b) {
    var m = S.BLOCK_RE.exec(b), view = m[1] || "", fam = m[2], n = +m[3];
    blockKeys(b).forEach(function (k) { st.map.delete(k); });
    // in a base list, close the gap so later blocks keep being read
    if (!view && (fam === "main" || fam === "info")) shiftDown(view, fam, n);
    st.selected = null; changed();
  }
  function shiftDown(view, fam, n) {
    // every family that falls back to this one shares the slot numbers: move them all together
    var fams = S.FAMILY_NAMES.filter(function (f) { return S.FAMILIES[f].chain.indexOf(fam) >= 0; });
    var views = [""].concat(S.VIEWS);
    views.forEach(function (v) { fams.forEach(function (f) {
      slotsOf(v, f).filter(function (x) { return x > n; }).forEach(function (x) { renameBlock(blockName(v, f, x), blockName(v, f, x - 1)); });
    }); });
  }
  function firstFree(view, fam) { var j = 0; while (C.present(st.map, blockName(view, fam, j))) j++; return j; }
  function renumber() {
    var count = 0;
    ["main", "info"].forEach(function (fam) {
      var slots = slotsOf("", fam);
      for (var want = 0; want < slots.length; want++) {
        var cur = slotsOf("", fam);
        if (cur[want] !== want) {
          var from = cur[want];
          var fams = S.FAMILY_NAMES.filter(function (f) { return S.FAMILIES[f].chain.indexOf(fam) >= 0; });
          [""].concat(S.VIEWS).forEach(function (v) { fams.forEach(function (f) { if (slotsOf(v, f).indexOf(from) >= 0) renameBlock(blockName(v, f, from), blockName(v, f, want)); }); });
          count++;
        }
      }
    });
    toast(count ? "Renumbered " + count + " slot" + (count > 1 ? "s" : "") + " so every block is read." : "Numbering already has no gaps.");
    changed();
  }
  function initElements() {
    var vSel = $("#el-view"), fSel = $("#el-family"), tSel = $("#el-addtype");
    vSel.appendChild(el("option", { value: "", text: "Every view (base)" }));
    S.VIEWS.forEach(function (v) { vSel.appendChild(el("option", { value: v, text: S.VIEW_LABEL[v] + " view only" })); });
    S.FAMILY_NAMES.forEach(function (f) { fSel.appendChild(el("option", { value: f, text: S.FAMILIES[f].label + " (" + f + ")" })); });
    S.TYPE_NAMES.forEach(function (t) { tSel.appendChild(el("option", { value: t, text: t })); });
    tSel.value = "StaticImage";
    vSel.addEventListener("change", function () { st.editView = vSel.value; st.selected = null; renderElements(); });
    fSel.addEventListener("change", function () {
      st.editFamily = fSel.value; st.selected = null;
      var pg = $("#pv-page"); if (pg.querySelector('option[value="' + fSel.value + '"]')) { pg.value = fSel.value; st.pv.family = fSel.value; st.pv.page = S.FAMILIES[fSel.value].page; }
      renderElements(); schedule();
    });
    tSel.addEventListener("change", function () { $("#el-addhelp").textContent = S.TYPES[tSel.value].desc; });
    $("#el-addhelp").textContent = S.TYPES[tSel.value].desc;
    $("#el-add").addEventListener("click", function () {
      var view = st.editView, fam = st.editFamily, type = tSel.value;
      var n;
      if (!view && (fam === "main" || fam === "info")) n = firstFree("", fam);
      else {
        // after the last slot the merged list reaches, so the parser still reads it
        var b = view ? C.build(st.map, { view: view, family: fam, isBuiltin: true }) : buildBase(fam);
        n = b.elements.length;
      }
      if (type === "Background" && n !== 0) toast("A Background is only used as block 0. Added it anyway; move it up to slot 0.", "warn");
      var bl = blockName(view, fam, n);
      st.map.set(bl + "_type", type);
      var t = S.TYPES[type];
      if (type === "StaticText") st.map.set(bl + "_value", "Text");
      if (type === "AttributeText") st.map.set(bl + "_attribute", "#Name");
      if (type === "AttributeImage") st.map.set(bl + "_attribute", "#System");
      if (type === "StaticImage") st.map.set(bl + "_default", "logo");
      if (type === "ItemCover" || type === "Coverflow" || type === "Grid" || type === "PSBBNFlow") st.map.set(bl + "_default", /apps|vcd/i.test(fam) ? "coverapp" : "cover");
      if (type === "ItemCover") { st.map.set(bl + "_x", "480"); st.map.set(bl + "_y", "240"); st.map.set(bl + "_width", "180"); st.map.set(bl + "_height", "256"); }
      if (t.x === 0 && t.y === 0 && type !== "Background" && !st.map.has(bl + "_x")) { st.map.set(bl + "_x", "POS_MID"); st.map.set(bl + "_y", "POS_MID"); }
      st.selected = bl; changed();
    });
    $("#el-renumber").addEventListener("click", renumber);
  }

  // ---- Images tab -----------------------------------------------------------------------------------------
  function usesOf(name) {
    var n = 0; st.map.forEach(function (v, k) { if (v === name && /_(default|overlay|overlay2|alt)(_hd|_sd|_desc)?$|^alt$/.test(k)) n++; }); return n;
  }
  function renderImages() {
    var box = $("#img-list"); box.innerHTML = "";
    if (!st.images.size) box.appendChild(el("p", { "class": "tc-hint", text: "No pictures of your own yet. Add some above, or replace one of OPL's below." }));
    Array.from(st.images.keys()).sort().forEach(function (n) {
      var a = st.images.get(n), big = a.w > S.TEX_MAX || a.h > S.TEX_MAX;
      var nameIn = el("input", { type: "text", value: n, size: 16, spellcheck: "false", "aria-label": "Picture name" });
      nameIn.addEventListener("change", function () {
        var nn = cleanName(nameIn.value.trim()); if (!nn || nn === n) return;
        st.images.set(nn, a); st.images.delete(n);
        st.map.forEach(function (v, k) { if (v === n && /_(default|overlay|overlay2|alt)(_hd|_sd|_desc)?$|^alt$/.test(k)) st.map.set(k, nn); });
        changed();
      });
      var card = el("div", { "class": "tc-imgcard" + (big ? " bad" : "") }, [
        el("div", { "class": "th" }, [a.img ? el("img", { src: a.url, alt: n }) : el("span", { text: "?" })]),
        el("div", { "class": "meta" }, [
          el("div", {}, [nameIn, el("code", { text: ".png" })]),
          el("small", { text: a.w + " × " + a.h + " · " + Math.round(a.data.length / 1024) + " KB · used " + usesOf(n) + "×" + (S.BUILTIN_ALL[n] ? " · replaces OPL's " + n : "") }),
          big ? el("small", { "class": "bad", text: "Too big: OPL refuses pictures over 1024 pixels a side." }) : null,
          el("div", { "class": "tc-acts" }, [
            (a.w > 640 || a.h > 480 || big) ? el("button", { type: "button", "class": "tc-mini", text: "Shrink to fit 640 × 480", onclick: function () { shrink(n, 640, 480); } }) : null,
            el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { if (a.url) URL.revokeObjectURL(a.url); st.images.delete(n); changed(); } })
          ])
        ])
      ]);
      box.appendChild(card);
    });
    // replace-a-built-in grid
    var rep = $("#img-builtins"); if (rep.childElementCount) { $$(".tc-bi", rep).forEach(function (b) { b.classList.toggle("mine", st.images.has(b.dataset.name)); }); return; }
    Object.keys(S.BUILTIN).forEach(function (gname) {
      rep.appendChild(el("h4", { text: gname }));
      var grid = el("div", { "class": "tc-bigrid" });
      S.BUILTIN[gname].forEach(function (n) {
        var file = S.BUILTIN_FILE[n] || n;
        var up = el("input", { type: "file", accept: "image/*", hidden: "hidden" });
        up.addEventListener("change", async function () {
          var f = up.files[0]; up.value = ""; if (!f) return;
          try { await addImage(n, await toPng(f.name, new Uint8Array(await f.arrayBuffer()))); changed(); toast("Your " + n + ".png now replaces OPL's."); } catch (e) { toast(e.message, "bad"); }
        });
        var btn = el("button", { type: "button", "class": "tc-bi", "data-name": n, title: "Replace " + n + ".png", onclick: function () { up.click(); } }, [
          S.BUILTIN_EMPTY[n] ? el("span", { "class": "noimg", text: "none built in" }) : el("img", { src: BASE + encodeURIComponent(file) + ".png", alt: "", loading: "lazy" }),
          el("code", { text: n })
        ]);
        grid.appendChild(btn); grid.appendChild(up);
      });
      rep.appendChild(grid);
    });
    $$(".tc-bi", rep).forEach(function (b) { b.classList.toggle("mine", st.images.has(b.dataset.name)); });
  }
  async function shrink(n, mw, mh) {
    var a = st.images.get(n); if (!a || !a.img) return;
    var s = Math.min(mw / a.w, mh / a.h, 1), w = Math.max(1, Math.round(a.w * s)), h = Math.max(1, Math.round(a.h * s));
    var c = document.createElement("canvas"); c.width = w; c.height = h; c.getContext("2d").drawImage(a.img, 0, 0, w, h);
    var blob = await new Promise(function (r) { c.toBlob(r, "image/png"); });
    await addImage(n, new Uint8Array(await blob.arrayBuffer())); changed();
  }
  function initImages() {
    $("#img-add").addEventListener("change", async function () {
      var files = Array.prototype.slice.call(this.files); this.value = "";
      for (var i = 0; i < files.length; i++) {
        try { await addImage(cleanName(stem(files[i].name)), await toPng(files[i].name, new Uint8Array(await files[i].arrayBuffer()))); }
        catch (e) { toast(e.message, "bad"); }
      }
      changed();
      if (files.length) toast("Added " + files.length + " picture" + (files.length > 1 ? "s" : "") + ". Pick them in an element's Picture box, or name one after an OPL picture to replace it.");
    });
  }

  // ---- Fonts and other files -------------------------------------------------------------------------------
  function renderFonts() {
    var box = $("#font-list"); box.innerHTML = "";
    if (!st.fonts.size) box.appendChild(el("p", { "class": "tc-hint", text: "No font files yet. Without one the theme uses OPL's built-in font." }));
    st.fonts.forEach(function (f, n) {
      var used = Array.from(st.map.keys()).filter(function (k) { return /^(default_font|font\d+)$/.test(k) && st.map.get(k) === n; });
      box.appendChild(el("div", { "class": "tc-fontrow" }, [
        el("span", { "class": "sample", style: "font-family:'" + f.family + "'", text: "Crystal Keep Chronicles 0123" }),
        el("code", { text: n }), el("small", { text: Math.round(f.data.length / 1024) + " KB · " + (used.length ? "used by " + used.join(", ") : "not used yet") }),
        el("button", { type: "button", "class": "tc-mini", text: "Use as main font", onclick: function () { setKey("default_font", n); } }),
        el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { st.fonts.delete(n); changed(); } })
      ]));
    });
    var slots = $("#font-slots"); slots.innerHTML = "";
    var fs = el("fieldset", { "class": "tc-fs" }, [el("legend", { text: "Font slots" })]);
    ["default_font", "default_font_size", "default_font_weight"].forEach(function (k) { fs.appendChild(field(k, S.GLOBALS[k], st.map.get(k), function (v) { setKey(k, v); }, { global: true })); });
    for (var i = 1; i <= 15; i++) {
      if (!st.map.has("font" + i) && !st.map.has("font" + i + "_size") && i > 3 && !st.map.has("font" + (i - 1))) continue;
      ["font" + i, "font" + i + "_size", "font" + i + "_weight"].forEach(function (k) { fs.appendChild(field(k, S.GLOBALS[k], st.map.get(k), function (v) { setKey(k, v); }, { global: true })); });
    }
    slots.appendChild(fs);
    var ex = $("#extra-list"); ex.innerHTML = "";
    st.extras.forEach(function (d, p) {
      ex.appendChild(el("div", { "class": "tc-fontrow" }, [el("code", { text: p }), el("small", { text: Math.round(d.length / 1024) + " KB" }),
        el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { st.extras.delete(p); changed(); } })]));
    });
    if (!st.extras.size) ex.appendChild(el("p", { "class": "tc-hint", text: "None. Sounds go in sound/ (for example sound/bgm.ogg)." }));
  }
  function initFonts() {
    $("#font-add").addEventListener("change", async function () {
      var files = Array.prototype.slice.call(this.files); this.value = "";
      for (var i = 0; i < files.length; i++) {
        var n = files[i].name.replace(/[^A-Za-z0-9 _\-.]/g, "_");
        await addFont(n, new Uint8Array(await files[i].arrayBuffer()));
        if (!st.map.has("default_font")) st.map.set("default_font", n);
      }
      changed();
    });
    $("#extra-add").addEventListener("change", async function () {
      var files = Array.prototype.slice.call(this.files); this.value = "";
      var dir = ($("#extra-dir").value || "").replace(/^\/+|\/+$/g, "");
      for (var i = 0; i < files.length; i++) st.extras.set((dir ? dir + "/" : "") + files[i].name, new Uint8Array(await files[i].arrayBuffer()));
      changed();
    });
  }

  // ---- Config file tab ---------------------------------------------------------------------------------
  function initCfg() {
    $("#cfg-apply").addEventListener("click", function () {
      var r = C.parse($("#cfg-text").value);
      st.map = r.map; st.notes = r.notes; st.selected = null;
      changed({ keepCfg: true });
      toast(r.notes.length ? "Applied, with " + r.notes.length + " note" + (r.notes.length > 1 ? "s" : "") + " on the Check tab." : "Applied.");
    });
    $("#cfg-copy").addEventListener("click", function () {
      var t = $("#cfg-text"); t.select();
      (navigator.clipboard ? navigator.clipboard.writeText(t.value) : Promise.reject()).then(function () { toast("Copied."); }, function () { document.execCommand("copy"); toast("Copied."); });
    });
  }

  // ---- Check tab ------------------------------------------------------------------------------------------
  function findings() {
    var text = C.serialize(st.map);
    var list = C.check(text, {
      has: function (n) { return st.images.has(n); },
      size: function (n) { var a = st.images.get(n); return a ? { w: a.w, h: a.h } : null; },
      fonts: new Set(st.fonts.keys())
    });
    // notes from the last pasted config (line-level problems the writer has since cleaned up)
    st.notes.forEach(function (n) { list.unshift({ level: n.level, msg: "Pasted file, line " + n.line + ": " + n.msg }); });
    var tarNames = [];
    st.images.forEach(function (a, n) {
      if ((n + ".png").length > 47) tarNames.push(n + ".png");
      if (!usesOf(n) && !S.BUILTIN_ALL[n] && !/_#?[A-Za-z]+$/.test(n) && n !== "settings_bg") list.push({ level: "info", msg: n + ".png is not used by any element." });
    });
    st.fonts.forEach(function (f, n) { if (n.length > 47) tarNames.push(n); });
    st.extras.forEach(function (d, p) { if (p.length > 47) tarNames.push(p); });
    if (tarNames.length) list.push({ level: "warn", msg: "Inside a one-file theme (tar) names are limited to 47 characters: " + tarNames.join(", ") + ". The zip is fine." });
    var vram = 0; st.images.forEach(function (a) { vram += a.w * a.h * 4; });
    if (vram > 2.5 * 1024 * 1024) list.push({ level: "warn", msg: "Your pictures add up to about " + (vram / 1048576).toFixed(1) + " MB once unpacked. The PS2 has 4 MB of video memory for everything; shrink large pictures if the theme loads slowly or pictures go missing." });
    if (st.name.length > 40) list.push({ level: "warn", msg: "Keep the theme name short; long folder names may not show in full." });
    return list;
  }
  function renderCheck() {
    var list = findings(), box = $("#chk-list"); box.innerHTML = "";
    var errs = list.filter(function (x) { return x.level === "error"; }).length, warns = list.filter(function (x) { return x.level === "warn"; }).length;
    var badge = $("#chk-badge"); badge.textContent = errs + warns ? String(errs + warns) : "✓"; badge.className = "tc-badge " + (errs ? "bad" : warns ? "warn" : "good");
    $("#chk-sum").textContent = errs || warns ? errs + " problem" + (errs === 1 ? "" : "s") + " and " + warns + " warning" + (warns === 1 ? "" : "s") + "." : "No problems found. The file uses only keys RiptOPL reads.";
    list.forEach(function (f) {
      var li = el("li", { "class": "tc-f " + f.level }, [el("b", { text: f.level === "error" ? "Problem" : f.level === "warn" ? "Warning" : "Note" }), " ", f.msg]);
      if (f.key && S.BLOCK_RE.test(C.blockOf(f.key) || f.key)) {
        var blk = C.blockOf(f.key) || f.key;
        li.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Show", onclick: function () { if (C.present(st.map, blk) || blockKeys(blk).length) selectBlock(blk); } }));
      }
      box.appendChild(li);
    });
  }

  // ---- Download -------------------------------------------------------------------------------------------
  function files() {
    var enc = new TextEncoder(), out = [{ path: "conf_theme.cfg", data: enc.encode(C.serialize(st.map, "RiptOPL theme: " + st.name + "\nMade with the RiptOPL Theme Creator")) }];
    st.images.forEach(function (a, n) { out.push({ path: n + ".png", data: a.data }); });
    st.fonts.forEach(function (f, n) { out.push({ path: n, data: f.data }); });
    st.extras.forEach(function (d, p) { out.push({ path: p, data: d }); });
    return out;
  }
  function initDownload() {
    $("#th-name").addEventListener("change", function () { st.name = this.value.trim() || "MyTheme"; save(); refreshAll(); });
    $$("[data-dl]").forEach(function (b) { b.addEventListener("click", function () {
      var errs = findings().filter(function (x) { return x.level === "error"; });
      if (errs.length && !confirm("The Check tab lists " + errs.length + " problem" + (errs.length > 1 ? "s" : "") + " that will show on the PS2. Download anyway?")) { showTab("check"); return; }
      var dir = "thm_" + safeName();
      if (b.dataset.dl === "zip") IO.download(IO.zip(files().map(function (f) { return { path: dir + "/" + f.path, data: f.data }; })), dir + ".zip");
      else { try { IO.download(IO.tar(files()), dir + ".tar"); } catch (e) { toast(e.message, "bad"); } }
    }); });
  }

  // ---- Toasts ---------------------------------------------------------------------------------------------
  function toast(msg, level) {
    var t = el("div", { "class": "tc-toast " + (level || ""), role: "status", text: msg });
    $("#tc-toasts").appendChild(t); setTimeout(function () { t.classList.add("go"); }, 4200); setTimeout(function () { t.remove(); }, 5000);
  }

  // ---- Boot ----------------------------------------------------------------------------------------------
  async function boot() {
    initTabs(); initPreview(); initPreviewControls(); initStart(); initElements(); initImages(); initFonts(); initCfg(); initDownload();
    try {
      var r = await fetch(BASE + "conf_theme_OPL.cfg"); if (!r.ok) throw new Error(r.status);
      st.builtinCfg = C.parse(await r.text()).map;
    } catch (e) { toast("The built-in theme did not load, so fallback views cannot be previewed.", "warn"); }
    var saved = null; try { saved = JSON.parse(localStorage.getItem(LS_KEY) || "null"); } catch (e) { saved = null; }
    if (saved && saved.cfg) {
      st.map = C.parse(saved.cfg).map; st.name = saved.name || "MyTheme";
      toast("Restored your last theme layout. Pictures and fonts are not kept between visits: download the zip to keep them.");
    } else if (st.builtinCfg) { st.map = new Map(st.builtinCfg); st.name = "MyRiptOPL"; st.isBuiltin = false; if (!st.map.has("badge_set")) st.map.set("badge_set", "ripps2"); }
    $("#th-name").value = st.name;
    refreshAll();
    showTab(saved ? "elements" : "start");
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot); else boot();
})();
