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
    // only names OPL has a picture for (and the two built-in button sets)
    if ((!S.BUILTIN_ALL[name] && !/^glyph_(standard|white)_/.test(name)) || S.BUILTIN_EMPTY[name]) return null;
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
    remember();
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
    $("#pv-widehint").hidden = !(st.pv.wide && b.globals.hd_autofit === undefined); // only when the theme has not chosen
    drawOverlay();
    renderInspector(); updatePills();
  }
  function canvasPoint(ev) {
    var cv = $("#pv-over"), r = cv.getBoundingClientRect();
    return { x: (ev.clientX - r.left) * R.W / r.width, y: (ev.clientY - r.top) * R.H / r.height };
  }
  // A full-screen picture (a backdrop) is only picked when nothing smaller is under the pointer.
  function isBackdrop(h) { return h.type === "Background" || (h.w >= R.W - 1 && h.h >= R.H - 1); }
  function hitAt(p) {
    var back = null;
    for (var i = hits.length - 1; i >= 0; i--) {
      var h = hits[i]; if (/^\(/.test(h.block)) continue;
      if (p.x >= h.x && p.x <= h.x + h.w && p.y >= h.y && p.y <= h.y + h.h) { if (isBackdrop(h)) { if (!back && h.type !== "Background") back = h; continue; } return h; }
    }
    return back;
  }

  // ---- Canvas editor: snap grid, guides, resize handles, nudge, align, undo -----------------------------
  var ed = { snap: true, grid: 8, showGrid: true, guides: true, outlines: false, pixels: false, zoom: 1, scope: "all" };
  try { var savedEd = JSON.parse(localStorage.getItem(LS_KEY + "-editor") || "null"); if (savedEd) Object.keys(ed).forEach(function (k) { if (savedEd[k] !== undefined) ed[k] = savedEd[k]; }); } catch (e) { /* storage off */ }
  function saveEd() { try { localStorage.setItem(LS_KEY + "-editor", JSON.stringify(ed)); } catch (e) { /* storage off */ } }
  var drag = null, guideLines = [], canvasActive = false;
  var RESIZABLE = { StaticImage: 1, GameImage: 1, AttributeImage: 1, ItemCover: 1, ItemIcon: 1, ItemsList: 1, MenuText: 1, MenuIcon: 1, LoadingIcon: 1, BdmIndex: 1, Coverflow: 1, PSBBNFlow: 1, Grid: 1, SaveIcon: 1 };
  function elOf(block) { return lastBuilt ? lastBuilt.elements.filter(function (x) { return x.block === block; })[0] || null : null; }
  function selHit() { if (!st.selected) return null; return hits.filter(function (x) { return x.block === st.selected; }).pop() || null; }
  function canResize(e) { return !!e && (RESIZABLE[e.type] === 1 || e.props.width !== undefined || e.props.height !== undefined); }
  function scaleNow() { var r = $("#pv-over").getBoundingClientRect(); return r.width ? R.W / r.width : 1; }

  // The key that is in effect for this view: _hd in 16:9 (or _wsX for x), _sd in 4:3, _desc with List Title and Description.
  function effKey(block, k) {
    var base = block + "_" + k;
    if (st.pv.desc && st.map.has(base + "_desc")) return base + "_desc";
    if (st.pv.wide) {
      if (st.hd || st.map.has(base + "_hd")) return base + "_hd";
      if (k === "x" && st.map.has(block + "_wsX")) return block + "_wsX";
    } else if (st.map.has(base + "_sd")) return base + "_sd";
    return base;
  }
  function curVal(block, k) { var v = st.map.get(effKey(block, k)); return v !== undefined ? v : st.map.get(block + "_" + k); }
  // Write an anchor value, keeping it counted from the same edge (or POS_MID) it was before.
  function writeAxis(block, e, k, v, size) {
    var t = S.TYPES[e.type] || {}, old = curVal(block, k), d = k === "x" ? t.x : t.y;
    v = Math.round(v);
    var fromEnd = old !== undefined ? /^\s*-/.test(old) : typeof d === "number" && d < 0;
    var wasMid = old !== undefined ? /^\s*POS_MID/.test(old) : d === "MID";
    var out = wasMid && v === size >> 1 ? "POS_MID" : fromEnd && v - size < 0 ? String(v - size) : String(v);
    st.map.set(effKey(block, k), out);
  }
  function anchorOf(q, box) {
    return { x: q.al === 1 ? box.x + box.w / 2 : q.al === 2 ? box.x + box.w : box.x, y: q.al === 0 ? box.y : box.y + box.h / 2 };
  }
  // Turn a new on-screen box into x, y, width and height for the block, from the drag's starting state.
  function applyBox(d, nb) {
    var q = d.q, b0 = d.box0, e = d.e, t = S.TYPES[e.type] || {};
    var snapGrid = ed.snap && !d.free, g = ed.grid;
    var gr = function (v, on) { return on && g > 1 ? Math.round(v / g) * g : Math.round(v); };
    if (d.mode !== "move") {
      var w0 = q.w != null ? q.w : b0.w / q.rx, h0 = q.h != null ? q.h : b0.h;
      if (/[ew]/.test(d.mode)) st.map.set(effKey(d.block, "width"), String(Math.max(1, gr(w0 * nb.w / b0.w, snapGrid && !d.gx))));
      if (/[ns]/.test(d.mode)) st.map.set(effKey(d.block, "height"), String(Math.max(1, gr(h0 * nb.h / b0.h, snapGrid && !d.gy))));
      if (t.presenter) return; // a cover row grows around its anchor
    }
    var a0 = anchorOf(q, b0), a1 = anchorOf(q, nb);
    var moveX = d.mode === "move" || /[ew]/.test(d.mode), moveY = d.mode === "move" || /[ns]/.test(d.mode);
    if (moveX && Math.abs(a1.x - a0.x) > 0.01) writeAxis(d.block, e, "x", gr(q.ax + (a1.x - a0.x) / q.kx, snapGrid && !d.gx), R.W);
    if (moveY && Math.abs(a1.y - a0.y) > 0.01) writeAxis(d.block, e, "y", gr(q.ay + (a1.y - a0.y), snapGrid && !d.gy), R.H);
  }
  // Snap the moving edges of a box to the screen's edges and centre and to other elements, within 6 screen pixels.
  function guideTargets(axis) {
    var out = axis === "x" ? [0, R.W / 2, R.W] : [0, R.H / 2, R.H];
    hits.forEach(function (h) {
      if (h.block === st.selected || /^\(/.test(h.block) || isBackdrop(h)) return;
      if (axis === "x") out.push(h.x, h.x + h.w / 2, h.x + h.w); else out.push(h.y, h.y + h.h / 2, h.y + h.h);
    });
    return out;
  }
  function snapEdges(edges, axis, thr) {
    var best = null, targets = guideTargets(axis);
    edges.forEach(function (v) { targets.forEach(function (tg) { var dd = tg - v; if (Math.abs(dd) <= thr && (!best || Math.abs(dd) < Math.abs(best.d))) best = { d: dd, at: tg }; }); });
    return best;
  }
  function onMove(ev) {
    if (!drag) return;
    var p = canvasPoint(ev), dx = p.x - drag.start.x, dy = p.y - drag.start.y;
    if (!drag.moved && Math.abs(dx) + Math.abs(dy) < 2) return;
    if (!drag.moved) {
      var nb0 = forEdit(drag.block);
      if (nb0 !== drag.block) { var d0 = drag; drag = null; startDrag(nb0, d0.mode, d0.start, d0.box0); if (!drag) return; }
    }
    drag.moved = true; drag.free = ev.altKey;
    var b0 = drag.box0, thr = 6 * scaleNow(), nb;
    guideLines = []; drag.gx = drag.gy = false;
    if (drag.mode === "move") {
      if (ev.shiftKey) { if (Math.abs(dx) > Math.abs(dy)) dy = 0; else dx = 0; } // straight line
      nb = { x: b0.x + dx, y: b0.y + dy, w: b0.w, h: b0.h };
      if (ed.guides && !drag.free) {
        var sx = snapEdges([nb.x, nb.x + nb.w / 2, nb.x + nb.w], "x", thr), sy = snapEdges([nb.y, nb.y + nb.h / 2, nb.y + nb.h], "y", thr);
        if (sx) { nb.x += sx.d; drag.gx = true; guideLines.push({ v: true, at: sx.at }); }
        if (sy) { nb.y += sy.d; drag.gy = true; guideLines.push({ v: false, at: sy.at }); }
      }
    } else {
      var l = b0.x, tp = b0.y, r = b0.x + b0.w, bt = b0.y + b0.h, m = drag.mode, sym = !!(S.TYPES[drag.e.type] || {}).presenter;
      if (/w/.test(m)) l += dx; if (/e/.test(m)) r += dx; if (/n/.test(m)) tp += dy; if (/s/.test(m)) bt += dy;
      if (ed.guides && !drag.free) {
        var ex = /w/.test(m) ? l : /e/.test(m) ? r : null, ey = /n/.test(m) ? tp : /s/.test(m) ? bt : null;
        var gx = ex !== null ? snapEdges([ex], "x", thr) : null, gy = ey !== null ? snapEdges([ey], "y", thr) : null;
        if (gx) { if (/w/.test(m)) l += gx.d; else r += gx.d; drag.gx = true; guideLines.push({ v: true, at: gx.at }); }
        if (gy) { if (/n/.test(m)) tp += gy.d; else bt += gy.d; drag.gy = true; guideLines.push({ v: false, at: gy.at }); }
      }
      if (ev.shiftKey && m.length === 2 && b0.w && b0.h) { // keep the shape
        var f = Math.max((r - l) / b0.w, (bt - tp) / b0.h);
        if (/w/.test(m)) l = r - b0.w * f; else r = l + b0.w * f;
        if (/n/.test(m)) tp = bt - b0.h * f; else bt = tp + b0.h * f;
      }
      if (sym) { var cx = b0.x + b0.w / 2, cy = b0.y + b0.h / 2, hw = Math.max(Math.abs(r - cx), Math.abs(l - cx)), hh = Math.max(Math.abs(bt - cy), Math.abs(tp - cy));
        if (/[ew]/.test(m)) { l = cx - hw; r = cx + hw; } if (/[ns]/.test(m)) { tp = cy - hh; bt = cy + hh; } }
      if (r - l < 2) { if (/w/.test(m)) l = r - 2; else r = l + 2; }
      if (bt - tp < 2) { if (/n/.test(m)) tp = bt - 2; else bt = tp + 2; }
      nb = { x: l, y: tp, w: r - l, h: bt - tp };
    }
    // start each step from the values the drag began with
    drag.keys.forEach(function (kv) { if (kv[1] === undefined) st.map.delete(kv[0]); else st.map.set(kv[0], kv[1]); });
    applyBox(drag, nb);
    st.isBuiltin = false;
    schedule();
  }
  function handleAt(h, p) {
    var hs = 7 * scaleNow(), pts = handlePoints(h);
    for (var k in pts) if (Math.abs(p.x - pts[k].x) <= hs && Math.abs(p.y - pts[k].y) <= hs) return k;
    return null;
  }
  function handlePoints(h) {
    var l = h.x, r = h.x + h.w, t = h.y, b = h.y + h.h, cx = l + h.w / 2, cy = t + h.h / 2;
    return { nw: { x: l, y: t }, n: { x: cx, y: t }, ne: { x: r, y: t }, e: { x: r, y: cy }, se: { x: r, y: b }, s: { x: cx, y: b }, sw: { x: l, y: b }, w: { x: l, y: cy } };
  }
  var CURSOR = { nw: "nwse-resize", se: "nwse-resize", ne: "nesw-resize", sw: "nesw-resize", n: "ns-resize", s: "ns-resize", e: "ew-resize", w: "ew-resize" };
  // RiptOPL's own 640 x 480 units, the ones every x, y, width and height in the theme file use.
  // A line for every unit once they are at least 4 screen pixels apart (zoom in), else every 10.
  function toScreenX(x, sq) { return sq ? R.autofitX(x) : x; }
  function fromScreenX(x, sq) { return sq ? 320 + (x - 320) * 4 / 3 : x; }
  function drawPixelGrid(c, k) {
    var rect = $("#pv-over").getBoundingClientRect(), sq = squeezedGrid();
    var ux = (sq ? 0.75 : 1) * rect.width / R.W, uy = rect.height / R.H; // screen pixels per unit (16:9 stretches the box)
    // when zoomed, keep the rulers along the visible part of the screen
    var bx = $(".tc-canvas-box"), ox = bx.scrollLeft * R.W / rect.width, oy = bx.scrollTop * R.H / rect.height, ky = R.H / rect.height;
    var lo = sq ? -107 : 0, hi = sq ? 747 : R.W;
    var line = function (x0, y0, x1, y1, a) { c.strokeStyle = "rgba(255,255,255," + a + ")"; c.beginPath(); c.moveTo(x0, y0); c.lineTo(x1, y1); c.stroke(); };
    c.lineWidth = k;
    var stepX = ux >= 4 ? 1 : 10, stepY = uy >= 4 ? 1 : 10;
    for (var x = Math.ceil(lo / stepX) * stepX; x <= hi; x += stepX) { var sx = toScreenX(x, sq); if (sx < 0 || sx > R.W) continue; line(sx, 0, sx, R.H, x % 100 === 0 ? 0.3 : x % 10 === 0 ? 0.14 : 0.06); }
    for (var y = 0; y <= R.H; y += stepY) line(0, y, R.W, y, y % 100 === 0 ? 0.3 : y % 10 === 0 ? 0.14 : 0.06);
    // rulers along the top and left, numbered in RiptOPL pixels
    var lab = [10, 20, 40, 80, 160].filter(function (n) { return n * ux >= 44; })[0] || 160, labY = [10, 20, 40, 80, 160].filter(function (n) { return n * uy >= 28; })[0] || 160;
    var rs = 14 * ky, rw = 34 * k;
    c.fillStyle = "rgba(0,0,0,.6)"; c.fillRect(0, oy, R.W, rs); c.fillRect(ox, 0, rw, R.H);
    c.fillStyle = "#cfe8ff"; c.font = (10 * k) + "px ui-monospace, Menlo, Consolas, monospace"; c.textBaseline = "top";
    // text drawn in a 4:3 canvas that 16:9 stretches: undo the stretch so the numbers keep their shape
    var txt = function (t, x, y) { c.save(); c.translate(x, y); c.scale(1, ky / k); c.fillText(t, 0, 0); c.restore(); };
    for (x = Math.ceil(lo / lab) * lab; x <= hi; x += lab) { sx = toScreenX(x, sq); if (sx < ox + rw || sx > R.W - 2 * k) continue; line(sx, oy, sx, oy + rs, 0.8); txt(String(x), sx + 2 * k, oy + 2 * ky); }
    for (y = labY; y <= R.H; y += labY) { if (y < oy + rs) continue; line(ox, y, ox + rw, y, 0.8); txt(String(y), ox + 2 * k, y + 1 * ky); }
  }
  function showCoords(p) {
    var o = $("#ed-coords"); if (!o) return;
    if (!p) { o.textContent = "Point at the preview to see where it is in RiptOPL pixels."; return; }
    var sq = squeezedGrid(), x = Math.round(fromScreenX(p.x, sq)), y = Math.round(p.y);
    o.textContent = "RiptOPL pixels: x " + x + ", y " + y + (sq ? " (widescreen squeezes x toward the centre, as on the PS2)" : "");
  }
  function squeezedGrid() {
    if (!st.pv.wide || !lastBuilt || st.hd) return false; // a separate 16:9 place is written in real screen pixels
    var e = st.selected && elOf(st.selected);
    if (e) return R.posOf(lastBuilt, e, true).kx !== 1;
    return parseInt(lastBuilt.globals.hd_autofit, 10) === 1;
  }
  function drawOverlay() {
    var ov = $("#pv-over"), c = ov.getContext("2d"), k = scaleNow();
    // Draw the overlay near the screen's own resolution so thin lines stay sharp when zoomed in
    // (capped at 4096 pixels wide to keep memory down, so 8x zoom on a sharp screen is a little soft).
    var cw = ov.getBoundingClientRect().width, want = Math.min(4096, Math.max(R.W, Math.round(cw * (window.devicePixelRatio || 1))));
    if (cw && ov.width !== want) { ov.width = want; ov.height = Math.round(want * R.H / R.W); }
    var f = ov.width / R.W;
    c.setTransform(f, 0, 0, f, 0, 0); c.clearRect(0, 0, R.W, R.H);
    if (ed.pixels) drawPixelGrid(c, k);
    if (ed.showGrid && ed.grid >= 4) {
      c.lineWidth = k;
      // In 16:9 with hd_autofit the PS2 squeezes x toward the centre (320 + (x - 320) * 3/4), so the grid is drawn
      // where whole grid steps of x land on screen; elements placed in real screen pixels keep the plain grid.
      var sq = squeezedGrid();
      var lo = sq ? Math.floor(-320 / 3 / ed.grid) * ed.grid : ed.grid, hi = sq ? R.W + 320 / 3 : R.W - 1;
      for (var gx = lo; gx <= hi; gx += ed.grid) {
        var sxp = sq ? R.autofitX(gx) : gx; if (sxp <= 0 || sxp >= R.W) continue;
        c.strokeStyle = Math.round(gx / ed.grid) % 4 ? "rgba(255,255,255,.07)" : "rgba(255,255,255,.16)"; c.beginPath(); c.moveTo(sxp + .5 * k, 0); c.lineTo(sxp + .5 * k, R.H); c.stroke(); }
      for (var gy = ed.grid, j = 1; gy < R.H; gy += ed.grid, j++) { c.strokeStyle = j % 4 ? "rgba(255,255,255,.07)" : "rgba(255,255,255,.16)"; c.beginPath(); c.moveTo(0, gy + .5 * k); c.lineTo(R.W, gy + .5 * k); c.stroke(); }
      c.strokeStyle = "rgba(90,200,255,.35)"; c.setLineDash([4 * k, 4 * k]);
      c.beginPath(); c.moveTo(R.W / 2, 0); c.lineTo(R.W / 2, R.H); c.moveTo(0, R.H / 2); c.lineTo(R.W, R.H / 2); c.stroke(); c.setLineDash([]);
    }
    if (ed.outlines) {
      c.lineWidth = k; c.strokeStyle = "rgba(120,200,255,.7)"; c.setLineDash([3 * k, 3 * k]);
      hits.forEach(function (h) { if (!isBackdrop(h)) c.strokeRect(h.x, h.y, h.w, h.h); });
      c.setLineDash([]);
    }
    var h = selHit();
    if (h) {
      c.lineWidth = 2 * k; c.strokeStyle = "#ffd54a"; c.strokeRect(h.x, h.y, h.w, h.h);
      if (canResize(elOf(h.block))) {
        var pts = handlePoints(h), s = 5 * k;
        Object.keys(pts).forEach(function (n) { c.fillStyle = "#ffd54a"; c.fillRect(pts[n].x - s, pts[n].y - s, 2 * s, 2 * s); c.strokeStyle = "#000"; c.lineWidth = k; c.strokeRect(pts[n].x - s, pts[n].y - s, 2 * s, 2 * s); });
      }
    }
    c.strokeStyle = "#ff4fd8"; c.lineWidth = k;
    guideLines.forEach(function (gl) { c.beginPath(); if (gl.v) { c.moveTo(gl.at, 0); c.lineTo(gl.at, R.H); } else { c.moveTo(0, gl.at); c.lineTo(R.W, gl.at); } c.stroke(); });
  }
  function startDrag(block, mode, p, h) {
    var e = elOf(block); if (!e) return false;
    var q = R.posOf(lastBuilt, e, st.pv.wide);
    // the first move to a new separate 16:9 x starts from where the element is drawn now, in real screen pixels
    if (q.kx !== 1 && /_hd$/.test(effKey(block, "x"))) { q.ax = q.x; q.kx = 1; }
    var keys = ["x", "y", "width", "height"].map(function (k) { var kk = effKey(block, k); return [kk, st.map.get(kk)]; });
    if (st.pv.wide && !st.hd && st.map.has(block + "_wsX")) keys.push([block + "_wsX", st.map.get(block + "_wsX")]);
    drag = { block: block, mode: mode, e: e, q: q, start: p, box0: { x: h.x, y: h.y, w: h.w, h: h.h }, keys: keys, moved: false };
    return true;
  }
  function initPreview() {
    var ov = $("#pv-over");
    ov.addEventListener("pointerdown", function (ev) {
      canvasActive = true;
      var p = canvasPoint(ev), sh = selHit(), mode = null;
      if (sh && canResize(elOf(sh.block))) mode = handleAt(sh, p);
      var h = mode ? sh : hitAt(p);
      if (!h && ev.pointerType === "touch" && ed.zoom > 1) return; // a swipe on empty space scrolls the zoomed preview
      if (!h) { if (st.selected) { st.selected = null; renderElements(); schedule(); } return; }
      if (!mode) mode = "move";
      if (st.selected !== h.block) selectBlock(h.block);
      if (startDrag(h.block, mode, p, h)) { ov.setPointerCapture(ev.pointerId); ev.preventDefault(); }
    });
    ov.addEventListener("pointermove", function (ev) {
      if (drag) { onMove(ev); showCoords(canvasPoint(ev)); return; }
      var sh = selHit(), p = canvasPoint(ev), m = sh && canResize(elOf(sh.block)) ? handleAt(sh, p) : null;
      ov.style.cursor = m ? CURSOR[m] : hitAt(p) ? "move" : "default";
      showCoords(p);
    });
    var end = function (ev) { if (drag && drag.moved) { guideLines = []; changed(); } drag = null; guideLines = []; schedule();
      var r = ov.getBoundingClientRect(); if (!ev || ev.clientX < r.left || ev.clientX > r.right || ev.clientY < r.top || ev.clientY > r.bottom) showCoords(null); };
    ov.addEventListener("pointerleave", function () { if (!drag) showCoords(null); });
    ov.addEventListener("pointerup", end); ov.addEventListener("pointercancel", end);
    ov.addEventListener("dblclick", function (ev) { var h = hitAt(canvasPoint(ev)); if (h) { selectBlock(h.block); var f = $("#el-props input, #el-props select"); if (f) f.focus(); } });
    document.addEventListener("pointerdown", function (ev) { if (!ev.target.closest || !ev.target.closest(".tc-canvas-box, .tc-edbar, .tc-insp")) canvasActive = false; });
    document.addEventListener("keydown", onKey);
    window.addEventListener("resize", schedule);
  }
  function typingIn(t) { return t && (/^(INPUT|TEXTAREA|SELECT)$/.test(t.tagName) || t.isContentEditable); }
  function onKey(ev) {
    var mod = ev.ctrlKey || ev.metaKey, key = ev.key;
    if ($(".tc-modal")) { if (key === "Escape") closeModal(false); return; }
    if (typingIn(ev.target)) return;
    if (mod && (key === "z" || key === "Z")) { ev.preventDefault(); if (ev.shiftKey) redo(); else undo(); return; }
    if (mod && (key === "y" || key === "Y")) { ev.preventDefault(); redo(); return; }
    if (!canvasActive || !st.selected) return;
    var step = ev.shiftKey ? Math.max(ed.grid, 2) : 1;
    var dir = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1] }[key];
    if (dir) { ev.preventDefault(); nudge(dir[0] * step, dir[1] * step); return; }
    if (key === "Delete" || key === "Backspace") { ev.preventDefault(); var b = st.selected; deleteBlock(b); toast("Deleted " + b + ". Press Ctrl+Z to bring it back."); return; }
    if (mod && (key === "d" || key === "D")) { ev.preventDefault(); dupBlock(st.selected); return; }
    if (key === "Escape") { st.selected = null; renderElements(); schedule(); }
  }
  function nudge(dx, dy) {
    editBlock(); var h = selHit(); if (!h) return;
    if (!startDrag(st.selected, "move", { x: 0, y: 0 }, h)) return;
    drag.free = true; // a nudge moves by exactly what was pressed
    applyBox(drag, { x: h.x + dx * drag.q.kx, y: h.y + dy, w: h.w, h: h.h });
    drag = null; changed();
  }
  function alignTo(where) {
    if (!selHit()) { toast("Pick an element first: click it in the preview.", "warn"); return; }
    editBlock(); var h = selHit(); if (!h) return;
    var nb = { x: h.x, y: h.y, w: h.w, h: h.h };
    if (where === "left") nb.x = 0; if (where === "hcenter") nb.x = (R.W - h.w) / 2; if (where === "right") nb.x = R.W - h.w;
    if (where === "top") nb.y = 0; if (where === "vmiddle") nb.y = (R.H - h.h) / 2; if (where === "bottom") nb.y = R.H - h.h;
    if (!startDrag(st.selected, "move", { x: 0, y: 0 }, h)) return;
    drag.free = true; applyBox(drag, nb); drag = null; changed();
  }
  // The bar under the preview: the selected element's position and size, editable.
  function renderInspector() {
    var box = $("#ed-insp"); if (!box) return;
    var b = st.selected, e = b ? elOf(b) : null;
    if (!e) { if (box.dataset.for !== "") { box.dataset.for = ""; box.innerHTML = ""; box.appendChild(el("span", { "class": "tc-hint", text: "Click an element to select it. Drag to move, drag a yellow handle to resize. Arrow keys nudge (Shift: one grid step), Alt turns snapping off while dragging, Delete removes, Ctrl+Z undoes." })); } return; }
    var keyFor = function (k) { return effKey(b, k); };
    if (box.dataset.for !== b + "|" + keyFor("x")) {
      box.dataset.for = b + "|" + keyFor("x"); box.innerHTML = "";
      box.appendChild(el("b", { text: typeLabel(e.type) }));
      ["x", "y", "width", "height"].forEach(function (k) {
        var id = "ed-v-" + k;
        var inp = el("input", { id: id, type: "text", size: 6, spellcheck: "false", "data-k": k });
        inp.addEventListener("change", function () { var tb = editBlock(); setKey(effKey(tb, k), inp.value.trim()); });
        inp.addEventListener("keydown", function (ev) { if (ev.key === "ArrowUp" || ev.key === "ArrowDown") { var n = parseInt(inp.value, 10); if (!isNaN(n)) { ev.preventDefault(); inp.value = String(n + (ev.key === "ArrowUp" ? 1 : -1) * (ev.shiftKey ? 10 : 1)); var tb = editBlock(); setKey(effKey(tb, k), inp.value); } } });
        box.appendChild(el("label", { "for": id }, [{ x: "X", y: "Y", width: "W", height: "H" }[k] + " ", inp]));
      });
      box.appendChild(el("small", { "class": "tc-keys" }));
    }
    var t = S.TYPES[e.type] || {};
    $$("input[data-k]", box).forEach(function (inp) {
      if (document.activeElement === inp) return;
      var k = inp.dataset.k, v = curVal(b, k);
      inp.value = v !== undefined ? v : "";
      var d = k === "x" ? t.x : k === "y" ? t.y : k === "width" ? t.w : t.h;
      inp.placeholder = d === null || d === undefined ? "auto" : d === "MID" ? "POS_MID" : d === "INF" ? "DIM_INF" : String(d);
    });
    var note = /_hd$/.test(keyFor("x")) ? "Editing the 16:9 (_hd) values" : /_sd$/.test(keyFor("x")) ? "Editing the 4:3 (_sd) values" : /_wsX$/.test(keyFor("x")) ? "x is the _wsX value in 16:9" : "";
    $(".tc-keys", box).textContent = note;
  }

  // ---- Undo ----------------------------------------------------------------------------------------------
  var undoStack = [], redoStack = [], lastSnap = null;
  function remember() {
    var now = C.serialize(st.map);
    if (lastSnap !== null && now !== lastSnap) { undoStack.push(lastSnap); if (undoStack.length > 300) undoStack.shift(); redoStack = []; }
    lastSnap = now; updateUndoButtons();
  }
  function restore(cfg) { st.map = C.parse(cfg).map; lastSnap = cfg; st.isBuiltin = false; if (st.selected && !blockKeys(st.selected).length) st.selected = null; save(); refreshAll(); updateUndoButtons(); }
  function undo() { if (!undoStack.length) { toast("Nothing to undo."); return; } redoStack.push(lastSnap); restore(undoStack.pop()); }
  function redo() { if (!redoStack.length) { toast("Nothing to redo."); return; } undoStack.push(lastSnap); restore(redoStack.pop()); }
  function updateUndoButtons() { var u = $("#ed-undo"), r = $("#ed-redo"); if (u) u.disabled = !undoStack.length; if (r) r.disabled = !redoStack.length; }
  function initEditorBar() {
    var bindCb = function (id, k) { var e = $(id); e.checked = !!ed[k]; e.addEventListener("change", function () { ed[k] = e.checked; saveEd(); schedule(); }); };
    bindCb("#ed-snap", "snap"); bindCb("#ed-showgrid", "showGrid"); bindCb("#ed-guides", "guides"); bindCb("#ed-outlines", "outlines");
    bindCb("#ed-pixels", "pixels");
    $("#pv-autofit").addEventListener("click", function () {
      st.map.set("hd_autofit", "1");
      if (lastBuilt && lastBuilt.view) st.map.delete(lastBuilt.view + ".hd_autofit"); // a per-view setting would win over it
      st.isBuiltin = false; changed();
      if (parseInt(built().globals.hd_autofit, 10) === 1) toast("Widescreen now keeps the 4:3 shape (hd_autofit=1). Undo takes it back.");
      else toast("hd_autofit is set, but something else in the theme still turns it off for this view. Check the Config file tab.", "bad");
    });
    var z = $("#ed-zoom"), setZoom = function () {
      var n = Math.max(1, +ed.zoom || 1), box = $(".tc-canvas-box"); z.value = String(n);
      $(".tc-zoom").style.width = (n * 100) + "%"; box.classList.toggle("zoomed", n > 1); schedule();
    };
    z.addEventListener("change", function () { ed.zoom = +z.value; saveEd(); setZoom(); }); setZoom();
    $(".tc-canvas-box").addEventListener("scroll", function () { if (ed.pixels) schedule(); }, { passive: true });
    var g = $("#ed-grid"); g.value = String(ed.grid); g.addEventListener("change", function () { ed.grid = +g.value; saveEd(); schedule(); });
    $("#ed-undo").addEventListener("click", undo); $("#ed-redo").addEventListener("click", redo);
    $$("[data-align]").forEach(function (b) { b.addEventListener("click", function () { alignTo(b.dataset.align); }); });
    $("#ed-big").addEventListener("click", function () { var w = $(".tc-wrap"); w.classList.toggle("big"); this.textContent = w.classList.contains("big") ? "Smaller preview" : "Bigger preview"; schedule(); });
    $$('input[name="ed-scope"]').forEach(function (r) { r.checked = r.value === ed.scope; r.addEventListener("change", function () { if (r.checked) { ed.scope = r.value; saveEd(); } }); });
    updateUndoButtons();
  }

  // ---- In-page questions (the browser's own pop-ups are blocked in some viewers) -------------------------
  var modalDone = null;
  function ask(msg, okText, cancelText) {
    return new Promise(function (res) {
      closeModal(false);
      var ok = el("button", { type: "button", "class": "btn", text: okText || "OK", onclick: function () { closeModal(true); } });
      var no = el("button", { type: "button", "class": "tc-mini", text: cancelText || "Cancel", onclick: function () { closeModal(false); } });
      var m = el("div", { "class": "tc-modal", role: "dialog", "aria-modal": "true" }, [el("div", { "class": "tc-dialog" }, [el("p", { text: msg }), el("div", { "class": "tc-acts" }, [ok, no])])]);
      m.addEventListener("click", function (ev) { if (ev.target === m) closeModal(false); });
      document.body.appendChild(m); modalDone = res; ok.focus();
    });
  }
  function closeModal(v) { var m = $(".tc-modal"); if (m) m.remove(); var f = modalDone; modalDone = null; if (f) f(v); }
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
      var v = this.value; st.pv.view = v.replace(/[35]$/, ""); st.pv.cover = /5$/.test(v) ? 5 : 3;
      if (st.selected && !buildScreen(st.pv.family).elements.some(function (e) { return e.block === st.selected; })) st.selected = null;
      renderElements(); schedule();
    });
    $("#pv-page").addEventListener("change", function () {
      setScreen(this.value);
    });
    bind("#pv-wide", "wide");
    $("#pv-wide2").addEventListener("change", function () { var w = $("#pv-wide"); w.checked = this.checked; w.dispatchEvent(new Event("change")); }); bind("#pv-alt", "alt"); bind("#pv-desc", "desc"); bind("#pv-art", "art"); bind("#pv-disc", "discArt");
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
    if ((st.images.size || st.fonts.size || st.extras.size) && !(await ask("Start over? Your layout can be brought back with Undo, but the pictures and fonts you added will be cleared. Download the zip first to keep them.", "Start over", "Keep editing"))) return;
    if (kind === "builtin" && !st.builtinCfg) { toast("The built-in theme file did not load. Check your connection and reload the page. Nothing was cleared.", "bad"); return; }
    clearAll();
    if (kind === "builtin") {
      st.map = new Map(st.builtinCfg); st.name = "MyRiptOPL";
      // A disk theme only gets OPL's own badge pictures with badge_set=ripps2 (themes.cpp thmLoad)
      if (!st.map.has("badge_set")) st.map.set("badge_set", "ripps2");
      // bezel_top/bottom and psbbn_bg are built in, so nothing needs copying
      toast("Loaded the built-in RiptOPL theme: four views in one file. Click anything on the preview to change it, or pick another screen above it.");
    } else {
      st.map = C.parse(STARTERS[kind]).map; st.name = kind === "blank" ? "MyTheme" : "My" + S.VIEW_LABEL[kind].replace(/\s/g, "");
    }
    st.selected = null; st.editView = ""; st.editFamily = "main";
    if (kind !== "builtin" && kind !== "blank") { st.pv.view = kind; $("#pv-view").value = kind === "coverflow" ? "coverflow3" : kind; }
    else { st.pv.view = "list"; $("#pv-view").value = "list"; }
    $("#th-name").value = st.name;
    changed(); showTab("elements");
    if (kind !== "builtin") toast("Ready. Click anything on the preview to change it, or pick another screen above it.");
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
        else if (/\.tar$/i.test(f.name)) await importFiles(IO.untar(buf));
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
    st.editFamily = st.pv.family; st.editView = "";
    renderPalette(); updatePills();
    var list = $("#el-list"); list.innerHTML = "";
    var head = $("#el-listhead"); if (head) head.textContent = "On the " + screenLabel(st.pv.family) + " screen";
    var b = buildScreen(st.pv.family), table = screenTable(), all = pageScreens(st.pv.page).length;
    b.elements.forEach(function (e) {
      var on = shownOn(e, table), here = !e.off;
      var chip = e.off ? "hidden here" : on.length >= all ? "every screen" : on.length > 1 ? on.length + " screens" : "this screen only";
      var eye = el("button", { type: "button", "class": "tc-eye" + (here ? "" : " off"), title: here ? "Hide it on this screen" : "Show it on this screen", "aria-label": (here ? "Hide " : "Show ") + typeLabel(e.type) + " on this screen",
        onclick: function (ev) { ev.stopPropagation(); setShown(e, st.pv.family, !here); } }, [here ? "👁" : "⌀"]);
      var li = el("li", { "class": "tc-el" + (st.selected === e.block ? " sel" : "") + (e.off ? " off" : ""), tabindex: "0" }, [
        eye,
        el("span", { "class": "ty", text: e.off ? "(hidden on this screen)" : typeLabel(e.type) }),
        el("span", { "class": "sum", text: summary(e) }),
        el("span", { "class": "from", text: chip })
      ]);
      li.addEventListener("click", function () { if (!e.off) selectBlock(e.block); });
      li.addEventListener("keydown", function (ev) { if (ev.key === "Enter" && !e.off) selectBlock(e.block); });
      list.appendChild(li);
    });
    if (!b.elements.length) list.appendChild(el("li", { "class": "tc-empty", text: "Nothing on this screen yet. Add something below." }));
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
    showTab("elements"); renderElements(); schedule();
    // scroll only the list itself: moving the page would pull the preview out from under the pointer
    var sel = $(".tc-el.sel"), list = $("#el-list");
    if (sel && list) { var top = sel.offsetTop - list.offsetTop; if (top < list.scrollTop || top + sel.offsetHeight > list.scrollTop + list.clientHeight) list.scrollTop = top - list.clientHeight / 2; }
  }
  function chainHas(fam, other) { return S.FAMILIES[fam].chain.indexOf(other) >= 0; }
  function renderProps() {
    var box = $("#el-props"); box.innerHTML = "";
    var b = st.selected;
    if (!b || !S.BLOCK_RE.test(b)) { box.appendChild(el("p", { "class": "tc-hint", text: "Pick an element in the list, or click it in the preview. Drag it in the preview to move it." })); return; }
    var type = st.map.get(b + "_type");
    var table = screenTable(), e0 = buildScreen(st.pv.family).elements.filter(function (x) { return x.block === b; })[0];
    box.appendChild(el("div", { "class": "tc-prophead" }, [
      el("h3", {}, [typeLabel(type) + " ", el("code", { "class": "tc-blk", text: b })]),
      el("p", { "class": "tc-hint", text: (S.TYPES[type] || {}).desc || "" })]));
    if (e0) {
      var on = shownOn(e0, table), row = table[e0.slot] || {};
      var sfs = el("fieldset", { "class": "tc-fs tc-shown" }, [el("legend", { text: "Shown on" })]);
      var boxes = el("div", { "class": "tc-checks" });
      pageScreens(st.pv.page).forEach(function (f) {
        var cb = el("input", { type: "checkbox" }); cb.checked = on.indexOf(f) >= 0;
        cb.addEventListener("change", function () { setShown(e0, f, cb.checked); });
        var other = row[f] && !row[f].off && row[f].block !== b;
        boxes.appendChild(el("label", { title: other ? "This screen has its own version of this element (" + row[f].block + ")" : "" }, [cb, " " + screenLabel(f) + (other ? " (own version)" : "")]));
      });
      sfs.appendChild(boxes);
      if (on.length > 1) {
        sfs.appendChild(el("p", { "class": "tc-hint" }, [
          "One element shared by " + on.length + " screens: a change shows on all of them. ",
          el("button", { type: "button", "class": "tc-mini", text: "Make a separate copy for the " + screenLabel(st.pv.family), onclick: function () {
            var keep = ed.scope; ed.scope = "screen"; var nb = forEdit(b); ed.scope = keep; st.selected = nb; changed(); } })
        ]));
      }
      box.appendChild(sfs);
    }
    var acts = el("div", { "class": "tc-acts" });
    acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Send back", title: "Draw it behind the element before it", onclick: function () { moveBlock(editBlock(), -1); } }));
    acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Bring forward", title: "Draw it in front of the element after it", onclick: function () { moveBlock(editBlock(), 1); } }));
    acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Duplicate", onclick: function () { dupBlock(b); } }));
    if (e0) acts.appendChild(el("button", { type: "button", "class": "tc-mini", text: "Hide on this screen", onclick: function () { st.selected = null; setShown(e0, st.pv.family, false); } }));
    acts.appendChild(el("button", { type: "button", "class": "tc-mini danger", text: e0 && shownOn(e0, table).length > 1 ? "Remove from every screen" : "Remove", onclick: function () { deleteBlock(b); toast("Removed. Undo brings it back."); } }));
    box.appendChild(acts);
    var hdl = el("label", { "class": "tc-hdtoggle" }, [el("input", { type: "checkbox", checked: st.hd ? "checked" : null, onchange: function () { st.hd = this.checked; if (st.hd && !st.pv.wide) { st.pv.wide = true; $("#pv-wide").checked = true; } renderProps(); schedule(); } }), " Give it a separate place in widescreen (16:9)"]);
    box.appendChild(hdl);
    if (!type && st.map.get(b + "_enabled") === "0") return;
    var keys = propsFor(type);
    var BASIC = ["default", "overlay", "value", "attribute", "title", "display", "font", "color", "aligned", "pattern", "wrap", "reflection", "disc", "rows", "columns", "spacing", "highlight", "style", "devices", "decorator"];
    var form = el("div", { "class": "tc-form tc-basic" });
    var more = el("div", { "class": "tc-form" }), nMore = 0;
    keys.forEach(function (k) {
      var target = BASIC.indexOf(k) >= 0 ? form : (nMore++, more);
      var spec = (type === "Grid" && S.GRID_ONLY_NAMES[k]) || S.PROPS[k];
      var key = b + "_" + k + (st.hd && k !== "type" ? "_hd" : "");
      var v = st.map.get(key);
      var shown = spec;
      if (st.hd && k !== "type" && st.map.has(b + "_" + k)) shown = Object.assign({}, spec, { def: st.map.get(b + "_" + k), help: (spec.help ? spec.help + " " : "") + "4:3 value: " + st.map.get(b + "_" + k) });
      target.appendChild(field(k, shown, v, function (val) {
        if (k === "type" && !val) return;
        var tb = editBlock(); setKey(tb + key.slice(b.length), val);
      }, { type: type }));
    });
    if (!form.childElementCount) form.appendChild(el("p", { "class": "tc-hint", text: "Move and resize it on the preview. Everything else is under More settings." }));
    box.appendChild(form);
    box.appendChild(el("details", { "class": "tc-more" }, [el("summary", { text: "More settings (" + nMore + "): exact position, kind, frame corners, and the rest" }), more]));
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

  // ---- Screens: which games/apps/PS1/favorites screens show an element ------------------------------------
  // RiptOPL reads each screen slot by slot along its fallback chain (themes.cpp addGUIElemSlot): the PS1 list
  // takes slot 3 from vcdMain3, else appsMain3, else main3. <block>_enabled=0 on its own makes the slot present
  // and empty, which hides it on that screen and on the screens that fall back to it.
  function pageScreens(page) { return S.FAMILY_NAMES.filter(function (f) { return S.FAMILIES[f].page === page && !S.FAMILIES[f].optIn; }); }
  function prefixOf(b) { var m = S.BLOCK_RE.exec(b || ""); return m && m[1] ? m[1] + "." : ""; }
  function famOf(b) { var m = S.BLOCK_RE.exec(b || ""); return m ? m[2] : null; }
  function slotOf(b) { var m = S.BLOCK_RE.exec(b || ""); return m ? +m[3] : -1; }
  function screenLabel(f) { return S.FAMILIES[f].label.replace(/ \(opt-in\)$/, ""); }
  function buildScreen(f) { return C.build(st.map, { view: st.pv.view, family: f, wide: st.pv.wide, desc: st.pv.desc, alt: st.pv.alt, tmpl: null, isBuiltin: true }); }
  // slot -> { family: { block, off } } for every screen of the page shown in the preview
  function screenTable() {
    var t = {};
    pageScreens(st.pv.page).forEach(function (f) {
      buildScreen(f).elements.forEach(function (e) { (t[e.slot] = t[e.slot] || {})[f] = { block: e.block, off: !!e.off }; });
    });
    return t;
  }
  function slotState(j) { return screenTable()[j] || {}; }
  function sameState(a, b) { return (!a && !b) || (a && b && a.block === b.block && a.off === b.off); }
  function copyBlock(from, to, src) {
    src = src || st.map;
    blockKeys(to).forEach(function (k) { st.map.delete(k); });
    src.forEach(function (v, k) { if (k.indexOf(from + "_") === 0 && C.blockOf(k) === from) st.map.set(to + k.slice(from.length), v); });
  }
  function clearBlock(b) { blockKeys(b).forEach(function (k) { st.map.delete(k); }); }
  // After one screen changed, give every other screen that changed by accident its old element back.
  function pin(before, snap, j, except) {
    for (var pass = 0; pass < 6; pass++) {
      var now = slotState(j), fixed = false;
      pageScreens(st.pv.page).forEach(function (f) {
        if (f === except || sameState(before[f], now[f])) return;
        var was = before[f], own = prefixOf(was ? was.block : now[f] && now[f].block) + f + j;
        if (was && !was.off && was.block !== own) copyBlock(was.block, own, snap);
        else if (!was || was.off) { clearBlock(own); st.map.set(own + "_enabled", "0"); }
        fixed = true;
      });
      if (!fixed) return;
    }
  }
  function shownOn(e, table) {
    var row = (table || screenTable())[e.slot] || {};
    return pageScreens(st.pv.page).filter(function (f) { return row[f] && row[f].block === e.block && !row[f].off; });
  }
  // The nearest real element (one with a type) in screen f's fallback chain for slot j: what showing it brings back.
  function sourceFor(f, j, pre, src) {
    var chain = S.FAMILIES[f].chain, pres = pre ? [pre, ""] : [""];
    for (var q = 0; q < pres.length; q++) for (var c = 0; c < chain.length; c++) { var b = pres[q] + chain[c] + j; if (src.has(b + "_type")) return b; }
    return null;
  }
  // Show or hide the element in this slot on screen f, leaving every other screen as it was.
  function setShown(e, f, on) {
    var j = e.slot, before = slotState(j), snap = new Map(st.map);
    var cur = before[f], own = prefixOf(cur ? cur.block : e.block) + f + j;
    if (!on) {
      if (st.map.has(own + "_type")) clearBlock(own);
      st.map.set(own + "_enabled", "0");
    } else {
      if (st.map.get(own + "_enabled") === "0") { st.map.delete(own + "_enabled"); if (!st.map.has(own + "_type")) clearBlock(own); }
      var s = slotState(j)[f];
      if (!s || s.off) { copyBlock(sourceFor(f, j, prefixOf(own), snap) || e.block, own, snap); st.map.delete(own + "_enabled"); }
    }
    pin(before, snap, j, f);
    if (!on && f === st.pv.family && st.selected === e.block) st.selected = null; // it is no longer on the screen in view
    changed();
  }
  // "This screen only": the first change to an element another screen shares makes this screen its own copy.
  function forEdit(b) {
    if (ed.scope !== "screen" || !b || !C.present(st.map, b)) return b;
    var f = st.pv.family;
    if (!S.FAMILIES[f] || S.FAMILIES[f].optIn || famOf(b) === f) return b;
    var j = slotOf(b), before = slotState(j), snap = new Map(st.map), own = prefixOf(b) + f + j;
    copyBlock(b, own, snap); st.map.delete(own + "_enabled");
    pin(before, snap, j, f);
    st.selected = own;
    lastBuilt = built();
    hits.forEach(function (h) { if (h.block === b) h.block = own; });
    toast("This change is for the " + screenLabel(f) + " only. The other screens keep the element as it was.");
    return own;
  }
  function editBlock() { return forEdit(st.selected); }

  // ---- Screen and layout pickers above the preview -----------------------------------------------------
  var PAGE_PILLS = [["main", "Games"], ["appsMain", "Apps"], ["vcdMain", "PS1 games"], ["favsMain", "Favorites"], ["info", "Game info"], ["appsInfo", "App info"], ["vcdInfo", "PS1 info"], ["favsInfo", "Favorites info"]];
  var VIEW_PILLS = [["list", "List"], ["coverflow3", "Coverflow"], ["coverflow5", "Coverflow ×5"], ["psbbn", "PSBBN"], ["grid", "Grid"]];
  function initPills() {
    var pg = $("#pv-pages"), vw = $("#pv-views");
    PAGE_PILLS.forEach(function (p) {
      pg.appendChild(el("button", { type: "button", role: "tab", "class": "tc-pill", "data-page": p[0], text: p[1], onclick: function () { setScreen(p[0]); } }));
    });
    vw.appendChild(el("span", { "class": "tc-small", text: "Layout" }));
    VIEW_PILLS.forEach(function (p) {
      vw.appendChild(el("button", { type: "button", role: "tab", "class": "tc-pill", "data-view": p[0], text: p[1], onclick: function () { var s = $("#pv-view"); s.value = p[0]; s.dispatchEvent(new Event("change")); } }));
    });
  }
  function setScreen(f) {
    var s = $("#pv-page"); s.value = f; st.pv.family = f; st.pv.page = S.FAMILIES[f].page;
    if (st.selected && !buildScreen(f).elements.some(function (e) { return e.block === st.selected; })) st.selected = null;
    renderElements(); schedule();
  }
  function updatePills() {
    var vcur = st.pv.view === "coverflow" ? "coverflow" + st.pv.cover : st.pv.view;
    $$("#pv-pages .tc-pill").forEach(function (b) { b.setAttribute("aria-selected", b.dataset.page === st.pv.family ? "true" : "false"); });
    $$("#pv-views .tc-pill").forEach(function (b) { b.setAttribute("aria-selected", b.dataset.view === vcur ? "true" : "false"); });
    var n = $("#ed-scope-name"); if (n) n.textContent = "the " + screenLabel(st.pv.family);
    var w2 = $("#pv-wide2"); if (w2) w2.checked = !!st.pv.wide;
  }

  // ---- Adding ------------------------------------------------------------------------------------------
  function typeLabel(t) { return (S.TYPES[t] || {}).label || t || "?"; }
  function addElement(type) {
    var f = st.pv.family, page = st.pv.page, base = page === "info" ? "info" : "main";
    var info = lastBuilt ? lastBuilt.info : null, view = st.pv.view;
    var prefix = info && info.own && info.own[view] ? view + "." : "";
    var fam = ed.scope === "screen" && !S.FAMILIES[f].optIn ? f : base;
    var n = 0, table = screenTable();
    Object.keys(table).forEach(function (j) { n = Math.max(n, +j + 1); });
    while (C.present(st.map, prefix + fam + n)) n++;
    var before = slotState(n), snap = new Map(st.map), bl = prefix + fam + n, t = S.TYPES[type];
    st.map.set(bl + "_type", type);
    if (type === "StaticText") st.map.set(bl + "_value", "Your text");
    if (type === "AttributeText") st.map.set(bl + "_attribute", "#Name");
    if (type === "AttributeImage") st.map.set(bl + "_attribute", "#System");
    if (type === "StaticImage") st.map.set(bl + "_default", "logo");
    if (type === "GameImage") { st.map.set(bl + "_pattern", "SCR"); st.map.set(bl + "_default", "screen"); st.map.set(bl + "_width", "200"); st.map.set(bl + "_height", "150"); }
    if (type === "ItemCover" || type === "Coverflow" || type === "Grid" || type === "PSBBNFlow") st.map.set(bl + "_default", /apps|vcd/i.test(fam) ? "coverapp" : "cover");
    if (type === "ItemCover") { st.map.set(bl + "_width", "180"); st.map.set(bl + "_height", "256"); }
    if (type !== "Background" && type !== "HintText" && type !== "InfoHintText" && !st.map.has(bl + "_x")) { st.map.set(bl + "_x", "POS_MID"); st.map.set(bl + "_y", "POS_MID"); }
    if (type === "Background" && n !== 0) toast("A background picture is only used as the first element of a screen. Use a Picture sized to the screen instead.", "warn");
    if (fam !== base) pin(before, snap, n, f);
    st.selected = bl; changed();
    toast("Added " + typeLabel(type).toLowerCase() + " in the middle of the screen" + (fam === base ? " (on every " + (page === "info" ? "info" : "list") + " screen)" : " (on the " + screenLabel(f) + " only)") + ". Drag it where you want it.");
    void t;
  }
  function renderPalette() {
    var a = $("#el-palette"), b = $("#el-palette-more"); if (!a || a.childElementCount) return;
    S.TYPE_NAMES.forEach(function (t) {
      var btn = el("button", { type: "button", "class": "tc-add-btn", title: S.TYPES[t].desc, onclick: function () { addElement(t); } }, [el("b", { text: "+ " + typeLabel(t) }), el("span", { text: S.TYPES[t].desc })]);
      (S.COMMON_TYPES.indexOf(t) >= 0 ? a : b).appendChild(btn);
    });
  }

  // ---- Images tab -----------------------------------------------------------------------------------------
  function usesOf(name) {
    var n = 0; st.map.forEach(function (v, k) { if (v === name && /_(default|overlay|overlay2|alt)(_hd|_sd|_desc)?$|^alt$/.test(k)) n++; }); return n;
  }
  function renderImages() {
    var box = $("#img-list"); box.innerHTML = "";
    if (!st.images.size) box.appendChild(el("p", { "class": "tc-hint", text: "No pictures of your own yet. Add some above, or use the badge and picture sections below." }));
    Array.from(st.images.keys()).sort().forEach(function (n) {
      var a = st.images.get(n), big = a.w > S.TEX_MAX || a.h > S.TEX_MAX;
      var nameIn = el("input", { type: "text", value: n, size: 16, spellcheck: "false", "aria-label": "Picture name" });
      nameIn.addEventListener("change", function () {
        var nn = cleanName(nameIn.value.trim()); if (!nn || nn === n) { nameIn.value = n; return; }
        if (st.images.has(nn)) { toast("There is already a picture called " + nn + ". Pick another name, or remove that one first.", "bad"); nameIn.value = n; return; }
        st.images.set(nn, a); st.images.delete(n);
        st.map.forEach(function (v, k) { if (v === n && /_(default|overlay|overlay2|alt)(_hd|_sd|_desc)?$|^alt$/.test(k)) st.map.set(k, nn); });
        changed();
      });
      var role = pictureRole(n);
      var card = el("div", { "class": "tc-imgcard" + (big ? " bad" : "") }, [
        el("div", { "class": "th" }, [a.img ? el("img", { src: a.url, alt: n }) : el("span", { text: "?" })]),
        el("div", { "class": "meta" }, [
          el("div", {}, [nameIn, el("code", { text: ".png" })]),
          el("small", { text: a.w + " × " + a.h + " · " + Math.round(a.data.length / 1024) + " KB · used " + usesOf(n) + "×" + (role ? " · " + role : "") }),
          big ? el("small", { "class": "bad", text: "Too big: OPL refuses pictures over 1024 pixels a side." }) : null,
          el("div", { "class": "tc-acts" }, [
            (a.w > 640 || a.h > 480 || big) ? el("button", { type: "button", "class": "tc-mini", text: "Shrink to fit 640 × 480", onclick: function () { shrink(n, 640, 480); } }) : null,
            el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { dropImage(n); changed(); } })
          ])
        ])
      ]);
      box.appendChild(card);
    });
    renderBadges();
    renderBuiltins();
  }
  function dropImage(n) { var a = st.images.get(n); if (a && a.url) URL.revokeObjectURL(a.url); st.images.delete(n); }
  // What RiptOPL does with a theme file of this name, in a few words (for the picture cards).
  function pictureRole(n) {
    var b = badgeOfFile(n);
    if (b) return "badge for " + b.attr + " = " + b.value;
    for (var i = 0; i < S.BUILTIN_GROUPS.length; i++) if (S.BUILTIN_GROUPS[i].names.indexOf(n) >= 0) return (S.BUILTIN_GROUPS[i].auto ? "replaces OPL's " : "stands in for OPL's ") + n;
    return S.BUILTIN_ALL[n] ? "stands in for OPL's " + n : "";
  }

  // ---- Badges (AttributeImage) ------------------------------------------------------------------------
  function badgeSetOn() { return String(st.map.get("badge_set") || "").toLowerCase() === "ripps2"; }
  // themeelems.cpp drawAttributeImage: with badge_set=ripps2 a theme file named after RiptOPL's own badge
  // (ISO.png) is used; without it the file is <value>_<attribute>.png. #DiscType has no built-in badge, so it
  // is always read as <value>_#DiscType.png.
  function badgeFile(b, v, on) { return (on === undefined ? badgeSetOn() : on) && !b.noBuiltin ? v : v + "_" + b.attr; }
  function badgeOfFile(n) {
    for (var i = 0; i < S.BADGES.length; i++) {
      var b = S.BADGES[i];
      for (var j = 0; j < b.values.length; j++) {
        if (n === b.values[j] + "_" + b.attr || (!b.noBuiltin && n === b.values[j] && badgeSetOn())) return { attr: b.attr, value: b.values[j] };
      }
    }
    return null;
  }
  // Switching badge_set renames the badge files the theme already has, so nothing is lost.
  function setBadgeSet(on) {
    var moved = 0;
    S.BADGES.forEach(function (b) {
      if (b.noBuiltin) return;
      b.values.forEach(function (v) {
        var from = badgeFile(b, v, !on), to = badgeFile(b, v, on);
        if (st.images.has(from) && !st.images.has(to)) { st.images.set(to, st.images.get(from)); st.images.delete(from); moved++; }
      });
    });
    if (on) st.map.set("badge_set", "ripps2"); else st.map.delete("badge_set");
    changed();
    toast((on ? "Badges now use RiptOPL's names, and any badge you have not supplied shows RiptOPL's own." : "Badges now use the <value>_<attribute>.png names; a badge you have not supplied shows nothing.") + (moved ? " Renamed " + moved + " file" + (moved > 1 ? "s" : "") + " to match." : ""));
  }
  async function fetchBuiltinPng(name) {
    var file = S.BUILTIN_FILE[name] || name;
    var r = await fetch(BASE + encodeURIComponent(file) + ".png");
    if (!r.ok) throw new Error("Could not load OPL's " + name + " picture (" + r.status + ").");
    return new Uint8Array(await r.arrayBuffer());
  }
  async function fillBadges(b) {
    var list = b ? [b] : S.BADGES, n = 0, fails = 0;
    for (var i = 0; i < list.length; i++) {
      if (list[i].noBuiltin) continue;
      for (var j = 0; j < list[i].values.length; j++) {
        var v = list[i].values[j], fn = badgeFile(list[i], v);
        if (st.images.has(fn)) continue;
        try { await addImage(fn, await fetchBuiltinPng(v)); n++; } catch (e) { fails++; }
      }
    }
    changed();
    toast(n ? "Copied " + n + " of RiptOPL's badge pictures into your theme, ready to edit or replace." : "Every badge already has a picture.", fails ? "warn" : "");
  }
  // Puts an AttributeImage for this badge on the game info page, next to the last badge there.
  function addBadgeElement(b) {
    var fam = "info", base = buildBase(fam), last = null;
    base.elements.forEach(function (e) { if (e.type === "AttributeImage") last = e; });
    var n = firstFree("", fam), bl = blockName("", fam, n);
    st.map.set(bl + "_type", "AttributeImage");
    st.map.set(bl + "_attribute", b.attr);
    if (last) {
      var lx = parseInt(last.props.x, 10), ly = last.props.y;
      st.map.set(bl + "_x", isNaN(lx) ? "40" : String(lx + (lx < 0 ? -1 : 1) * (parseInt(last.props.width, 10) || 48) + (lx < 0 ? -8 : 8)));
      if (ly !== undefined) st.map.set(bl + "_y", String(ly));
      if (last.props.width) st.map.set(bl + "_width", String(last.props.width));
      if (last.props.height) st.map.set(bl + "_height", String(last.props.height));
    } else { st.map.set(bl + "_x", "40"); st.map.set(bl + "_y", "-60"); st.map.set(bl + "_width", "48"); st.map.set(bl + "_height", "24"); }
    st.editView = ""; st.editFamily = fam; st.selected = bl;
    st.pv.page = "info"; st.pv.family = "info"; var pg = $("#pv-page"); if (pg) pg.value = "info";
    changed();
    toast("Added the " + b.label.toLowerCase() + " badge (" + bl + ") to the game info page. Drag it into place on the preview.");
  }
  function badgeUsed(b) {
    var used = false; st.map.forEach(function (v, k) { if (/_attribute$/.test(k) && v === b.attr && st.map.get(k.replace(/_attribute$/, "_type")) === "AttributeImage") used = true; }); return used;
  }
  function uploadInto(name, after) {
    var up = el("input", { type: "file", accept: "image/*", hidden: "hidden" });
    up.addEventListener("change", async function () {
      var f = up.files[0]; up.value = ""; if (!f) return;
      try { await addImage(name, await toPng(f.name, new Uint8Array(await f.arrayBuffer()))); changed(); toast(after || "Saved as " + name + ".png in your theme."); } catch (e) { toast(e.message, "bad"); }
    });
    document.body.appendChild(up); up.click(); setTimeout(function () { up.remove(); }, 60000);
  }
  function tile(name, label, fallbackSrc, opts) {
    opts = opts || {};
    var mine = st.images.get(name);
    var pic = mine && mine.url ? el("img", { src: mine.url, alt: "" }) : fallbackSrc ? el("img", { src: fallbackSrc, alt: "", loading: "lazy", "class": "theirs" }) : el("span", { "class": "noimg", text: opts.emptyText || "nothing yet" });
    var t = el("div", { "class": "tc-bi" + (mine ? " mine" : ""), "data-name": name }, [
      el("button", { type: "button", "class": "tc-bipick", title: (mine ? "Replace " : "Add ") + name + ".png", onclick: function () { uploadInto(name, opts.after); } }, [pic, el("code", { text: label || name })]),
      el("small", { text: mine ? "yours: " + name + ".png" : (fallbackSrc ? (opts.theirsText || "OPL's own") : "") }),
      mine ? el("button", { type: "button", "class": "tc-x", title: "Remove your " + name + ".png", "aria-label": "Remove " + name, onclick: function () { dropImage(name); changed(); } }, ["×"]) : null
    ]);
    return t;
  }
  function renderBadges() {
    var box = $("#img-badges"); if (!box) return; box.innerHTML = "";
    var on = badgeSetOn();
    var cb = el("input", { type: "checkbox" }); cb.checked = on;
    cb.addEventListener("change", function () { setBadgeSet(cb.checked); });
    box.appendChild(el("p", {}, [el("label", { "class": "tc-hdtoggle" }, [cb, " Use RiptOPL's badge set for anything I don't supply (", el("code", { text: "badge_set=ripps2" }), ")"])]));
    box.appendChild(el("p", { "class": "tc-hint", text: on
      ? "On: a badge you add is saved under RiptOPL's own name (for example ISO.png) and replaces that one badge; the rest show RiptOPL's pictures."
      : "Off: a badge you add is saved as <value>_<attribute>.png (for example ISO_#Format.png). A badge you leave out shows the element's own Picture, or nothing. Faded pictures below are RiptOPL's, shown for reference only." }));
    box.appendChild(el("div", { "class": "tc-acts" }, [
      el("button", { type: "button", "class": "tc-mini", text: "Copy all of RiptOPL's badges into my theme", onclick: function () { fillBadges(); } }),
      el("button", { type: "button", "class": "tc-mini danger", text: "Remove all my badge pictures", onclick: async function () {
        var names = Array.from(st.images.keys()).filter(function (n) { return badgeOfFile(n); });
        if (!names.length) { toast("There are no badge pictures to remove."); return; }
        if (!(await ask("Remove " + names.length + " badge picture" + (names.length > 1 ? "s" : "") + " from your theme?", "Remove", "Keep them"))) return;
        names.forEach(dropImage); changed();
      } })
    ]));
    S.BADGES.forEach(function (b) {
      var used = badgeUsed(b);
      var fs = el("fieldset", { "class": "tc-fs tc-badgeset" }, [el("legend", {}, [b.label + " ", el("code", { text: "attribute=" + b.attr })])]);
      fs.appendChild(el("div", { "class": "tc-acts" }, [
        el("span", { "class": "tc-small", text: used ? "Shown by an element in your theme." : "No element shows this badge yet." }),
        el("button", { type: "button", "class": "tc-mini", text: used ? "Add another to the info page" : "Add it to the info page", onclick: function () { addBadgeElement(b); } }),
        b.noBuiltin ? null : el("button", { type: "button", "class": "tc-mini", text: "Copy RiptOPL's set", onclick: function () { fillBadges(b); } })
      ]));
      if (b.noBuiltin) fs.appendChild(el("p", { "class": "tc-hint", text: "RiptOPL has no pictures of its own for this one: supply all three, or it shows nothing." }));
      var grid = el("div", { "class": "tc-bigrid" });
      b.values.forEach(function (v) {
        var fn = badgeFile(b, v);
        grid.appendChild(tile(fn, v, b.noBuiltin ? null : BASE + encodeURIComponent(S.BUILTIN_FILE[v] || v) + ".png",
          { theirsText: on ? "OPL's own (shown)" : "OPL's (reference)", emptyText: "none yet", after: "Saved as " + fn + ".png: the badge for " + b.attr + " = " + v + "." }));
      });
      fs.appendChild(grid);
      box.appendChild(fs);
    });
    box.appendChild(el("p", { "class": "tc-hint", text: "RiptOPL has no ESRB or other age-rating badge. Rating is the 0 to 5 star rating from each game's settings file. The preview's stand-in games carry made-up values; step through them with ▲ ▼ to see each badge." }));
  }

  // ---- OPL's own pictures a theme can replace -----------------------------------------------------------
  async function copyGlyphSet(set) {
    var n = 0;
    for (var i = 0; i < S.GLYPH_NAMES.length; i++) {
      var g = S.GLYPH_NAMES[i];
      try {
        var r = await fetch(BASE + S.GLYPH_SETS[set] + g + ".png"); if (!r.ok) continue;
        await addImage(g, new Uint8Array(await r.arrayBuffer())); n++;
      } catch (e) { /* counted below */ }
    }
    changed();
    toast(n === S.GLYPH_NAMES.length ? "Copied RiptOPL's " + set + " button set into your theme as cross.png, circle.png and the rest." : "Copied " + n + " of " + S.GLYPH_NAMES.length + " button pictures.", n === S.GLYPH_NAMES.length ? "" : "warn");
  }
  function renderBuiltins() {
    var rep = $("#img-builtins"); rep.innerHTML = "";
    S.BUILTIN_GROUPS.forEach(function (gr) {
      var head = el("h4", {}, [gr.name + " ", el("span", { "class": "tc-badge " + (gr.auto ? "good" : "warn"), text: gr.auto ? "used automatically" : "used where an element names it" })]);
      rep.appendChild(head);
      rep.appendChild(el("p", { "class": "tc-hint", text: gr.note }));
      if (gr.glyphs) {
        rep.appendChild(el("div", { "class": "tc-acts" }, [
          el("button", { type: "button", "class": "tc-mini", text: "Copy the standard button set into my theme", onclick: function () { copyGlyphSet("standard"); } }),
          el("button", { type: "button", "class": "tc-mini", text: "Copy the white button set", onclick: function () { copyGlyphSet("white"); } })
        ]));
      }
      var grid = el("div", { "class": "tc-bigrid" });
      gr.names.forEach(function (n) {
        grid.appendChild(tile(n, n, S.BUILTIN_EMPTY[n] ? null : BASE + encodeURIComponent(S.BUILTIN_FILE[n] || n) + ".png",
          { emptyText: "none built in", after: "Your " + n + ".png now " + (gr.auto ? "replaces OPL's." : "stands in wherever an element names " + n + ".") }));
      });
      rep.appendChild(grid);
    });
    rep.appendChild(el("p", { "class": "tc-hint", text: "Not replaceable: the animated boot logo frames (" + S.FIXED_PICTURES.join(", ") + ") always come from RiptOPL itself. A theme's logo.png is used only where an element names logo." }));
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
    var ss = $("#sound-slots"); ss.innerHTML = "";
    S.SOUNDS.forEach(function (snd) {
      var d = st.extras.get(snd.file);
      var pick = function () {
        var up = el("input", { type: "file", accept: snd.file.slice(snd.file.lastIndexOf(".")), hidden: "hidden" });
        up.addEventListener("change", async function () {
          var f = up.files[0]; up.value = ""; if (!f) return;
          var want = snd.file.slice(snd.file.lastIndexOf(".")).toLowerCase();
          if (f.name.toLowerCase().slice(-want.length) !== want) toast("\"" + f.name + "\" is not a " + want + " file. Added it as " + snd.file + " anyway; RiptOPL plays its own sound if it cannot read it.", "warn");
          st.extras.set(snd.file, new Uint8Array(await f.arrayBuffer())); changed();
        });
        document.body.appendChild(up); up.click(); setTimeout(function () { up.remove(); }, 60000);
      };
      ss.appendChild(el("div", { "class": "tc-fontrow" }, [
        el("b", { text: snd.label }), el("code", { text: snd.file }),
        el("small", { text: d ? "yours, " + Math.round(d.length / 1024) + " KB" : (/bgm/.test(snd.file) ? "none: the music chosen in Settings plays, if any" : "RiptOPL's own") }),
        el("button", { type: "button", "class": "tc-mini", text: d ? "Replace" : "Add", onclick: pick }),
        d ? el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { st.extras.delete(snd.file); changed(); } }) : null
      ]));
    });
    var ex = $("#extra-list"); ex.innerHTML = "";
    st.extras.forEach(function (d, p) {
      if (S.SOUNDS.some(function (x) { return x.file === p; })) return;
      ex.appendChild(el("div", { "class": "tc-fontrow" }, [el("code", { text: p }), el("small", { text: Math.round(d.length / 1024) + " KB" }),
        el("button", { type: "button", "class": "tc-mini danger", text: "Remove", onclick: function () { st.extras.delete(p); changed(); } })]));
    });
    if (!ex.childElementCount) ex.appendChild(el("p", { "class": "tc-hint", text: "None." }));
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
    $$("[data-dl]").forEach(function (b) { b.addEventListener("click", async function () {
      var errs = findings().filter(function (x) { return x.level === "error"; });
      if (errs.length && !(await ask("The Check tab lists " + errs.length + " problem" + (errs.length > 1 ? "s" : "") + " that will show on the PS2. Download anyway?", "Download anyway", "Show the problems"))) { showTab("check"); return; }
      var dir = "thm_" + safeName();
      if (b.dataset.dl === "zip") IO.download(IO.zip(files().map(function (f) { return { path: dir + "/" + f.path, data: f.data }; })), dir + ".zip");
      else { try { IO.download(IO.tar(files()), dir + ".tar"); } catch (e) { toast(e.message, "bad"); } }
    }); });
  }

  // ---- Toasts ---------------------------------------------------------------------------------------------
  function toast(msg, level) {
    var t = el("div", { "class": "tc-toast " + (level || ""), role: "status", text: msg });
    var box = $("#tc-toasts"); while (box.childElementCount >= 2) box.firstChild.remove(); // keep the newest two
    box.appendChild(t); setTimeout(function () { t.classList.add("go"); }, 4200); setTimeout(function () { t.remove(); }, 5000);
  }

  // ---- Boot ----------------------------------------------------------------------------------------------
  async function boot() {
    initTabs(); initPreview(); initEditorBar(); initPills(); initPreviewControls(); initStart(); initElements(); initImages(); initFonts(); initCfg(); initDownload();
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
    lastSnap = C.serialize(st.map);
    refreshAll();
    showTab(saved ? "elements" : "start");
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot); else boot();
})();
