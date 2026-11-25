/* RiptOPL Theme Creator: draws a close approximation of a RiptOPL page on a 640x480 canvas.
 * Positions, sizes, anchors, defaults and the 16:9 width rule follow themes.cpp/themeelems.cpp; the art,
 * game names and animation are stand-ins. The console is the final word. */
(function (g) {
  "use strict";
  var S = g.RTC_SCHEMA;
  var W = S.SCREEN_W, H = S.SCREEN_H;
  var ROW_H = 19;          // MENU_ITEM_HEIGHT: FNTSYS_DEFAULT_SIZE 17 + 2
  var DECORATOR = 20;      // DECORATOR_SIZE

  var GAMES = [
    { name: "Crystal Keep Chronicles", id: "SLUS_201.23", dev: "Northwind", genre: "RPG", rel: "2003", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_4", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_w", scan: "Scan_480p", size: "3910", hue: 210 },
    { name: "Neon Drift Racing", id: "SLES_512.04", dev: "Apex Arcade", genre: "Racing", rel: "2004", system: "PS2", media: "DVD", format: "ZSO", disc: "PS2DVD", rating: "Rating_3", players: "Players_2", vmode: "Vmode_pal", aspect: "Aspect_s", scan: "Scan_480i", size: "2140", hue: 320 },
    { name: "Iron Lantern", id: "SLUS_209.80", dev: "Quiet Forge", genre: "Action", rel: "2005", system: "PS2", media: "CD", format: "ISO", disc: "PS2CD", rating: "Rating_5", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_w", scan: "Scan_480p", size: "640", hue: 30 },
    { name: "Moonlit Harbour", id: "SCUS_973.01", dev: "Tidewater", genre: "Adventure", rel: "2002", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_2", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_s", scan: "Scan_480i", size: "4100", hue: 260 },
    { name: "Pocket Robot League", id: "SLUS_214.56", dev: "Gearbox Kids", genre: "Sports", rel: "2006", system: "PS2", media: "CD", format: "ZSO", disc: "PS2CD", rating: "Rating_3", players: "Players_4", vmode: "Vmode_multi", aspect: "Aspect_s", scan: "Scan_480i", size: "580", hue: 120 },
    { name: "Shadow of the Ninth Bell", id: "SLES_533.77", dev: "Lamplight", genre: "Horror", rel: "2004", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_4", players: "Players_1", vmode: "Vmode_pal", aspect: "Aspect_w", scan: "Scan_576i", size: "3300", hue: 0 },
    { name: "Sky Garden Tactics", id: "SLUS_207.11", dev: "Paper Kite", genre: "Strategy", rel: "2003", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_3", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_s", scan: "Scan_480i", size: "1800", hue: 170 },
    { name: "Thunder Valley Rally", id: "SLUS_219.02", dev: "Apex Arcade", genre: "Racing", rel: "2007", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_4", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_w", scan: "Scan_480p", size: "2900", hue: 50 },
    { name: "Velvet Circuit", id: "SLES_548.20", dev: "Starling", genre: "Music", rel: "2005", system: "PS2", media: "DVD", format: "ZSO", disc: "PS2DVD", rating: "Rating_2", players: "Players_1", vmode: "Vmode_pal", aspect: "Aspect_s", scan: "Scan_480i", size: "1200", hue: 290 },
    { name: "Wandering Cartographer", id: "SLUS_212.88", dev: "Northwind", genre: "RPG", rel: "2006", system: "PS2", media: "DVD", format: "ISO", disc: "PS2DVD", rating: "Rating_5", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_w", scan: "Scan_480p", size: "4300", hue: 90 },
    { name: "Arcade Paradise Mix", id: "SLPM_660.10", dev: "Coin Hall", genre: "Arcade", rel: "2003", system: "PS2", media: "CD", format: "ISO", disc: "PS2CD", rating: "Rating_3", players: "Players_1", vmode: "Vmode_ntsc", aspect: "Aspect_s", scan: "Scan_240p", size: "420", hue: 15 },
    { name: "Glacier Run", id: "SLES_501.11", dev: "Polar Bit", genre: "Sports", rel: "2002", system: "PS2", media: "CD", format: "ISO", disc: "PS2CD", rating: "Rating_2", players: "Players_1", vmode: "Vmode_pal", aspect: "Aspect_s", scan: "Scan_576i", size: "510", hue: 190 }
  ];
  var PS1 = [
    { name: "Retro Kart 64-bit", id: "SLUS_004.12", dev: "Pixel Pit", genre: "Racing", rel: "1998", system: "PS1", media: "CD", format: "VCD", disc: "PS1CD", size: "480", hue: 40 },
    { name: "Castle of Ash", id: "SCES_011.20", dev: "Gloam", genre: "Action", rel: "1997", system: "PS1", media: "CD", format: "VCD", disc: "PS1CD", size: "520", hue: 280 },
    { name: "Cosmo Puzzle", id: "SLUS_008.90", dev: "Starling", genre: "Puzzle", rel: "1999", system: "PS1", media: "CD", format: "VCD", disc: "PS1CD", size: "210", hue: 150 },
    { name: "Midnight Garage", id: "SLES_020.03", dev: "Apex Arcade", genre: "Racing", rel: "2000", system: "PS1", media: "CD", format: "VCD", disc: "PS1CD", size: "610", hue: 350 }
  ];
  var APPS = [
    { name: "Launchstation", id: "LAUNCHSTATION.ELF", format: "ELF", media: "APP", hue: 220 },
    { name: "POPSLoader", id: "POPSLOADER.ELF", format: "ELF", media: "APP", hue: 10 },
    { name: "wLaunchELF", id: "BOOT.ELF", format: "ELF", media: "APP", hue: 100 },
    { name: "PS2 Media Player", id: "PS2MP.ELF", format: "ELF", media: "APP", hue: 300 },
    { name: "Homebrew Mixer", id: "MIXER.ELF", format: "ELF", media: "APP", hue: 60 }
  ];
  var DEVICES = [["usb", "USB Games"], ["mmce", "MMCE Games"], ["hdd", "HDD Games"], ["eth", "Network Games"], ["app", "Applications"], ["fav", "Favorites"]];

  // ---- Generated stand-in art -------------------------------------------------------------------------
  var artCache = {};
  function mk(w, h) { var c = document.createElement("canvas"); c.width = w; c.height = h; return c; }
  function sampleCover(item, square) {
    var key = "cov" + item.id + (square ? "s" : "");
    if (artCache[key]) return artCache[key];
    var w = square ? 200 : 140, h = 200, c = mk(w, h), x = c.getContext("2d");
    var gr = x.createLinearGradient(0, 0, w, h);
    gr.addColorStop(0, "hsl(" + item.hue + ",55%,42%)"); gr.addColorStop(1, "hsl(" + ((item.hue + 40) % 360) + ",60%,14%)");
    x.fillStyle = gr; x.fillRect(0, 0, w, h);
    x.globalAlpha = 0.25; x.fillStyle = "#fff";
    for (var i = 0; i < 5; i++) { x.beginPath(); x.arc((item.hue * (i + 3)) % w, (i * 53 + item.hue) % h, 18 + i * 9, 0, 7); x.fill(); }
    x.globalAlpha = 1;
    if (!square) { x.fillStyle = "rgba(0,0,0,.65)"; x.fillRect(0, 0, w, 20); x.fillStyle = "#fff"; x.font = "bold 11px sans-serif"; x.fillText(item.system === "PS1" ? "PlayStation" : "PS2", 6, 14); }
    x.fillStyle = "#fff"; x.font = "bold 17px sans-serif"; x.textAlign = "center";
    wrapText(x, item.name, w / 2, h * 0.55, w - 16, 19);
    artCache[key] = c; return c;
  }
  function sampleShot(item, n) {
    var key = "scr" + item.id + n;
    if (artCache[key]) return artCache[key];
    var c = mk(256, 192), x = c.getContext("2d");
    var gr = x.createLinearGradient(0, 0, 0, 192);
    gr.addColorStop(0, "hsl(" + (item.hue + n * 60) % 360 + ",50%,60%)"); gr.addColorStop(1, "hsl(" + (item.hue + 20) % 360 + ",40%,20%)");
    x.fillStyle = gr; x.fillRect(0, 0, 256, 192);
    x.fillStyle = "rgba(0,0,0,.35)"; x.beginPath(); x.moveTo(0, 150); x.lineTo(90, 100 + n * 10); x.lineTo(170, 140); x.lineTo(256, 90); x.lineTo(256, 192); x.lineTo(0, 192); x.fill();
    artCache[key] = c; return c;
  }
  function sampleDisc(item) {
    var key = "ico" + item.id;
    if (artCache[key]) return artCache[key];
    var c = mk(64, 64), x = c.getContext("2d");
    var gr = x.createRadialGradient(32, 32, 4, 32, 32, 31);
    gr.addColorStop(0, "#fff"); gr.addColorStop(0.2, "hsl(" + item.hue + ",60%,60%)"); gr.addColorStop(1, "hsl(" + item.hue + ",50%,30%)");
    x.fillStyle = gr; x.beginPath(); x.arc(32, 32, 31, 0, 7); x.fill();
    x.fillStyle = "#222"; x.beginPath(); x.arc(32, 32, 6, 0, 7); x.fill();
    artCache[key] = c; return c;
  }
  function wrapText(x, text, cx, cy, maxW, lh) {
    var words = text.split(" "), lines = [], cur = "";
    words.forEach(function (w) { var t = cur ? cur + " " + w : w; if (x.measureText(t).width > maxW && cur) { lines.push(cur); cur = w; } else cur = t; });
    if (cur) lines.push(cur);
    var y = cy - (lines.length - 1) * lh / 2;
    lines.forEach(function (l, i) { x.fillText(l, cx, y + i * lh); });
  }

  // ---- Helpers ------------------------------------------------------------------------------------------
  function hex(c) { var m = /^#?([0-9a-f]{6})$/i.exec(c || ""); return m ? m[1] : null; }
  // Text is drawn by the GS modulating a white glyph with the colour (alpha 0x80 = 1.0), so a theme's
  // colour shows about twice as bright (opl.cpp: text_color 0x47526C "shows as #8FA4D8").
  function textCss(c, alpha) {
    var h = hex(c) || "47526C";
    var r = Math.min(255, parseInt(h.slice(0, 2), 16) * 2), gg = Math.min(255, parseInt(h.slice(2, 4), 16) * 2), b = Math.min(255, parseInt(h.slice(4, 6), 16) * 2);
    return "rgba(" + r + "," + gg + "," + b + "," + (alpha == null ? 1 : alpha) + ")";
  }
  function rawCss(c) { var h = hex(c); return h ? "#" + h : null; }
  function num(v, def) { if (v === undefined || v === "") return def; var n = parseInt(v, 10); return isNaN(n) ? 0 : n; }

  function Ctx(canvas, built, opts, assets) {
    this.c = canvas.getContext("2d");
    this.b = built; this.o = opts; this.a = assets; this.hits = [];
    var gl = built.globals;
    this.col = {
      bg: gl.bg_color || S.GLOBALS.bg_color.def, blend: gl.plasma_blend_color || "#000000",
      text: gl.text_color || S.GLOBALS.text_color.def, ui: gl.ui_text_color || S.GLOBALS.ui_text_color.def,
      sel: gl.sel_text_color || S.GLOBALS.sel_text_color.def
    };
    this.col.title = gl.title_color || this.col.text;
    this.wide = !!opts.wide;
    this.useDefault = gl.use_default === undefined ? true : num(gl.use_default, 1) !== 0;
  }
  Ctx.prototype.font = function (slot) {
    var gl = this.b.globals, f = this.o.fonts || {};
    var key = slot > 0 ? "font" + slot : "default_font";
    var file = gl[key] !== undefined ? gl[key] : (slot > 0 ? gl.default_font : undefined);
    var sizeKey = slot > 0 && gl[key] !== undefined ? "font" + slot + "_size" : "default_font_size";
    var size = num(gl[sizeKey], 17); if (size <= 0) size = 17;
    var wKey = slot > 0 && gl[key] !== undefined ? "font" + slot + "_weight" : "default_font_weight";
    var weight = num(gl[wKey], 0);
    if (!weight && file === "builtin:ripflowbold") weight = 700;
    var fam = file && f[file] ? "'" + f[file] + "'" : (file && file.indexOf("builtin:roboto") === 0 ? "Roboto, 'Segoe UI', Arial, sans-serif" : "'Segoe UI', Arial, Helvetica, sans-serif");
    return { css: (weight >= 100 ? weight : 400) + " " + size + "px " + fam, size: size };
  };
  // A theme image by name: the theme's own <name>.png, else OPL's built-in picture of that name.
  Ctx.prototype.img = function (name) {
    if (!name) return null;
    var u = this.a.user(name); if (u) return u;
    if (S.BUILTIN_ALL[name] && (this.useDefault || this.o.isBuiltin)) return this.a.builtin(name);
    return null;
  };
  // themes.cpp thmLayoutAutoFitX, rounded the same way the renderer places elements
  function autofitX(x) { return Math.round(320 + (x - 320) * 3 / 4); }
  Ctx.prototype.pos = function (e, def) {
    var p = e.props, t = S.TYPES[e.type] || {};
    function axis(v, d, size) {
      var n;
      if (v === undefined) n = d === "MID" ? size >> 1 : d;
      else if (String(v).indexOf("POS_MID") === 0) n = size >> 1;
      else n = parseInt(v, 10) || 0;
      return n < 0 ? size + n : n;
    }
    function dim(v, d, size) {
      if (v === undefined) return d === "INF" ? size : d;
      if (String(v).indexOf("DIM_INF") === 0) return size;
      return parseInt(v, 10) || 0;
    }
    var x = axis(p.x, def && def.x !== undefined ? def.x : t.x, W), y = axis(p.y, def && def.y !== undefined ? def.y : t.y, H);
    var w = dim(p.width, def && def.w !== undefined ? def.w : t.w, W), h = dim(p.height, def && def.h !== undefined ? def.h : t.h, H);
    var al = p.aligned !== undefined ? (num(p.aligned, 0) === 0 ? 0 : num(p.aligned, 0) === 2 ? 2 : 1) : t.aligned;
    var sc = p.scaled !== undefined ? num(p.scaled, 1) !== 0 : !!t.scaled;
    // hd_autofit=1: 320 + (x - 320) * 3/4, for elements without their own 16:9 x (themes.cpp thmLayoutAutoFitX)
    var m = this.b.map, blk = e.block, af = false;
    if (this.wide && num(this.b.globals.hd_autofit, 0) === 1 && e.type !== "Background" && !t.presenter &&
        !m.has(blk + "_x_hd") && !m.has(blk + "_wsX") && w !== W && !(t.image && !sc)) { x = autofitX(x); af = true; }
    return { x: x, y: y, w: w, h: h, al: al, sc: sc, af: af };
  };
  // rmDrawPixmap: anchor, size (null = the picture's own), 16:9 width rule for scaled elements
  Ctx.prototype.box = function (q, natW, natH) {
    var w = q.w == null ? natW : q.w, h = q.h == null ? natH : q.h;
    if (q.sc && this.wide) w = w * 3 / 4;
    var x = q.x, y = q.y;
    if (q.al === 1) { x -= w / 2; y -= h / 2; } else if (q.al === 2) { x -= w; y -= h / 2; }
    return { x: x, y: y, w: w, h: h };
  };
  Ctx.prototype.hit = function (e, r) { if (r && r.w > 0 && r.h > 0) this.hits.push({ block: e.block, type: e.type, x: r.x, y: r.y, w: r.w, h: r.h }); };
  Ctx.prototype.drawImg = function (src, r, alpha) {
    if (!src) return;
    var c = this.c; c.save(); if (alpha != null) c.globalAlpha = alpha;
    c.drawImage(src.img || src, r.x, r.y, r.w, r.h); c.restore();
  };
  // Art inlaid in a frame at the overlay corners (rmDrawOverlayPixmap), then the frame, then overlay2.
  Ctx.prototype.drawFramed = function (art, e, r) {
    var p = e.props, ov = this.img(p.overlay), ov2 = this.img(p.overlay2);
    if (ov && p.overlay_ulx !== undefined) {
      var ew = num(p.width, r.w) || r.w, eh = num(p.height, r.h) || r.h, sx = r.w / ew, sy = r.h / eh;
      var ax = num(p.overlay_ulx, 0), ay = num(p.overlay_uly, 0), bx = num(p.overlay_lrx, ew), by = num(p.overlay_lry, eh);
      if (art) this.drawImg(art, { x: r.x + ax * sx, y: r.y + ay * sy, w: (bx - ax) * sx, h: (by - ay) * sy });
    } else if (art) this.drawImg(art, r);
    if (ov) this.drawImg(ov, r);
    if (ov2) this.drawImg(ov2, r);
  };
  Ctx.prototype.text = function (str, x, y, align, color, font, maxW, valign) {
    var c = this.c; c.save();
    c.font = font.css; c.fillStyle = color; c.textBaseline = valign === "middle" ? "middle" : "top";
    c.textAlign = align;
    if (this.wide) { c.translate(x, y); c.scale(0.75, 1); x = 0; y = 0; }
    if (maxW && c.measureText(str).width > maxW / (this.wide ? 0.75 : 1)) {
      var lim = maxW / (this.wide ? 0.75 : 1), s = str;
      while (s.length > 1 && c.measureText(s + "…").width > lim) s = s.slice(0, -1);
      str = s + "…";
    }
    c.fillText(str, x, y);
    var wpx = c.measureText(str).width * (this.wide ? 0.75 : 1);
    c.restore();
    return wpx;
  };

  // ---- Page data --------------------------------------------------------------------------------------
  function pageData(o) {
    var fam = o.family || "main";
    var list = /vcd/i.test(fam) ? PS1 : /apps/i.test(fam) ? APPS : GAMES;
    var dev = /apps/i.test(fam) ? 4 : /favs/i.test(fam) ? 5 : 0;
    return { items: list, sel: Math.min(o.sel || 2, list.length - 1), dev: dev, square: list !== GAMES, kind: list === APPS ? "app" : list === PS1 ? "ps1" : "ps2" };
  }

  // ---- Element painters ---------------------------------------------------------------------------------
  var P = {};
  P.Background = function (X, e) {
    var p = e.props, pic = X.img(p["default"]);
    if (pic) { X.drawImg(pic, { x: 0, y: 0, w: W, h: H }); X.hit(e, { x: 0, y: 0, w: W, h: H }); return; }
    var c = X.c, hi = rawCss(X.col.bg) || "#0C3AA8", lo = rawCss(X.col.blend) || "#000";
    var gr = c.createLinearGradient(0, 0, W * 0.3, H);
    gr.addColorStop(0, lo); gr.addColorStop(0.55, hi); gr.addColorStop(1, lo);
    c.fillStyle = gr; c.fillRect(0, 0, W, H);
    c.save(); c.globalAlpha = 0.18; c.globalCompositeOperation = "lighter";
    for (var i = 0; i < 6; i++) { var rg = c.createRadialGradient(80 + i * 110, 120 + (i % 3) * 120, 10, 80 + i * 110, 120 + (i % 3) * 120, 160); rg.addColorStop(0, hi); rg.addColorStop(1, "rgba(0,0,0,0)"); c.fillStyle = rg; c.fillRect(0, 0, W, H); }
    c.restore();
    X.hit(e, { x: 0, y: 0, w: W, h: H });
  };
  P.StaticImage = function (X, e) {
    var pic = X.img(e.props["default"]); var q = X.pos(e);
    if (!pic) { if (q.w && q.h) X.hit(e, X.box(q, q.w, q.h)); return; }
    var r = X.box(q, pic.w, pic.h); X.drawFramed(pic, e, r); X.hit(e, r);
  };
  function artFor(X, e, it, pd, pattern) {
    pattern = (pattern || e.props.pattern || "COV").toUpperCase();
    if (!X.o.art) return null;
    if (pattern === "COV" || pattern === "COV2") return sampleCover(it, pd.square);
    if (pattern === "SCR") return sampleShot(it, 0);
    if (pattern === "SCR2") return sampleShot(it, 1);
    if (pattern === "ICO") return X.o.discArt === false ? null : sampleDisc(it);
    return null; // LGO, BG and others: the stand-in games have none
  }
  P.GameImage = function (X, e, pd) {
    var it = pd.items[pd.sel], q = X.pos(e), pat = (e.props.pattern || "").toUpperCase();
    var art = artFor(X, e, it, pd, pat || "COV"), def = X.img(e.props["default"]);
    if (pat === "ICO" && X.o.discArt === false) def = null;
    var src = art || def; if (!src) return;
    var nat = src.w ? [src.w, src.h] : [src.width, src.height];
    if (q.w == null && q.h == null && art) nat = pat.indexOf("SCR") === 0 ? [256, 192] : nat;
    if (q.w == null && q.h == null && !src.w) nat = [140, 200]; // a cover with neither: 140x200
    var r = X.box(q, nat[0], nat[1]); X.drawFramed(src, e, r); X.hit(e, r);
    if (num(e.props.reflection, 0)) reflect(X, src, r);
  };
  P.ItemCover = function (X, e, pd) { if (!e.props.pattern) e = { block: e.block, type: e.type, props: Object.assign({ pattern: "COV" }, e.props) }; P.GameImage(X, e, pd); };
  P.ItemIcon = function (X, e, pd) { e = { block: e.block, type: e.type, props: Object.assign({}, e.props, { pattern: "ICO" }) }; P.GameImage(X, e, pd); };
  P.AttributeImage = function (X, e, pd) {
    var it = pd.items[pd.sel], attr = e.props.attribute || "", q = X.pos(e);
    var map = { "#System": it.system, "#Media": it.media, "#Format": it.format, "#DiscType": it.disc, Vmode: it.vmode, Aspect: it.aspect, Scan: it.scan, Players: it.players, Rating: it.rating };
    var v = map[attr]; var pic = null;
    // themeelems.cpp drawAttributeImage: the built-in theme, or a disk theme with badge_set=ripps2, uses the
    // embedded badge for the value (or <attribute>_<value>); a disk theme otherwise reads <value>_<attribute>.png.
    var bs = X.o.isBuiltin || String(X.b.globals.badge_set || "").toLowerCase() === "ripps2";
    // With badge_set a theme file named after the built-in picture (ISO.png, Scan_480p.png) wins (thmLoadResource).
    if (v && bs) pic = (X.o.isBuiltin ? null : X.a.user(v) || X.a.user(attr + "_" + v)) || X.a.builtin(v) || X.a.builtin(attr + "_" + v);
    if (v && !pic && !X.o.isBuiltin) pic = X.a.user(v + "_" + attr);
    if (!pic && X.o.isBuiltin && v) return;
    if (!pic) pic = X.img(e.props["default"]);
    if (!pic) return;
    var r = X.box(q, pic.w, pic.h); X.drawFramed(pic, e, r); X.hit(e, r);
  };
  function reflect(X, src, r) {
    var c = X.c; c.save(); c.translate(r.x, r.y + r.h * 2 + 2); c.scale(1, -1); c.globalAlpha = 0.22;
    c.drawImage(src.img || src, 0, 0, r.w, r.h); c.restore();
    var gr = c.createLinearGradient(0, r.y + r.h, 0, r.y + r.h * 1.6); gr.addColorStop(0, "rgba(0,0,0,0)"); gr.addColorStop(1, "rgba(0,0,0,0.9)");
  }
  function textOf(X, e, pd) {
    var p = e.props, it = pd.items[pd.sel];
    if (e.type === "StaticText") return p.value || "";
    if (e.type === "GameCountText") return String(pd.items.length);
    if (e.type === "ItemText") return it.id;
    var a = p.attribute || "", label = p.title, v;
    var vals = { "#Name": it.name, Title: it.name, Developer: it.dev, Genre: it.genre, Release: it.rel, Description: "A stand-in description: the game's own CFG text shows here.", "#Size": it.size ? it.size + " MiB" : "", "#Startup": it.id, "#Format": it.format, "#Media": it.media, "#System": it.system };
    v = vals[a]; if (v === undefined) v = "";
    var lbl = label !== undefined ? label : (a === "#Name" || a === "Title" ? "" : a.replace(/^#/, "") + ": ");
    var disp = num(p.display, 0);
    if (disp === 1 && !v) return "";
    if (disp === 2 || !lbl) return v;
    return (label !== undefined ? label + ": " : lbl) + v;
  }
  P.StaticText = function (X, e, pd) {
    var p = e.props, q = X.pos(e), str = textOf(X, e, pd);
    if ((e.type === "AttributeText" && (p.attribute === "Title" || p.attribute === "Description") && X.o.listCfgHidden && /Main/.test(e.block)) || !str) return;
    var font = X.font(num(p.font, 0)), color = p.color ? textCss(p.color) : textCss(e.type === "AttributeText" && p.attribute === "#Name" || e.type === "ItemText" ? X.col.title : X.col.text);
    var align = q.al === 1 ? "center" : q.al === 2 ? "right" : "left";
    var hasBox = q.w != null || q.h != null, boxW = q.w == null ? W : q.w;
    if (num(p.wrap, 0) && hasBox) {
      var c = X.c; c.save(); c.font = font.css; c.fillStyle = color; c.textBaseline = "top";
      var words = str.split(" "), line = "", yy = q.al === 1 ? q.y - (q.h || 0) / 2 : q.y, x0 = q.al === 1 ? q.x - boxW / 2 : q.x;
      words.forEach(function (w) { var t = line ? line + " " + w : w; if (c.measureText(t).width > boxW && line) { c.fillText(line, x0, yy); yy += font.size + 2; line = w; } else line = t; });
      if (line) c.fillText(line, x0, yy); c.restore();
      X.hit(e, { x: x0, y: q.al === 1 ? q.y - (q.h || 0) / 2 : q.y, w: boxW, h: q.h || font.size * 3 });
      return;
    }
    var ty = q.al ? q.y : q.y, valign = q.al ? "middle" : "top";
    var wpx = X.text(str, q.x, ty, align, color, font, hasBox ? boxW : 0, valign);
    var hx = align === "center" ? q.x - wpx / 2 : align === "right" ? q.x - wpx : q.x;
    X.hit(e, { x: hx, y: q.al ? q.y - font.size / 2 : q.y, w: wpx, h: font.size + 2 });
  };
  P.AttributeText = P.StaticText; P.GameCountText = P.StaticText; P.ItemText = P.StaticText;
  P.MenuText = function (X, e, pd) {
    var p = e.props, q = X.pos(e), font = X.font(num(p.font, 0)), color = p.color ? textCss(p.color) : textCss(X.col.text);
    var name = DEVICES[pd.dev][1], style = p.style || "";
    if (style === "psbbn") {
      var step = q.h > 24 ? Math.max(18, q.h / 3) : 20, cy = q.al ? q.y : q.y + q.h / 2, lx = q.al === 1 ? q.x - (q.w || 160) / 2 : q.x;
      X.text(DEVICES[(pd.dev + 5) % 6][1], lx + 12, cy - step, "left", textCss(p.color || X.col.text, 0.5), font, (q.w || 160) - 12, "middle");
      X.c.fillStyle = "#5898ff"; X.c.fillRect(lx + 2, cy - 2, 4, 4);
      X.text(name, lx + 12, cy, "left", color, font, (q.w || 160) - 12, "middle");
      X.text(DEVICES[(pd.dev + 1) % 6][1], lx + 12, cy + step, "left", textCss(p.color || X.col.text, 0.5), font, (q.w || 160) - 12, "middle");
      X.hit(e, { x: lx, y: cy - step - 10, w: q.w || 160, h: step * 2 + 20 }); return;
    }
    var str = "<  " + name + "  >";
    var r = X.box(q, q.w, q.h), align = q.al === 1 ? "center" : "left";
    X.text(str, q.al === 1 ? q.x : q.x, q.al ? q.y : q.y, align, color, font, 0, q.al ? "middle" : "top");
    X.hit(e, r);
  };
  P.MenuIcon = function (X, e, pd) {
    var names = ["usb", "mmce", "hdd", "eth", "app", "fav"];
    var pic = X.img(names[pd.dev]); if (!pic) return;
    var q = X.pos(e), r = X.box(q, pic.w, pic.h); X.drawImg(pic, r); X.hit(e, r);
  };
  P.BdmIndex = function (X, e) { var pic = X.img("Index_1"); if (!pic) return; var q = X.pos(e), r = X.box(q, pic.w, pic.h); X.drawImg(pic, r); X.hit(e, r); };
  P.LoadingIcon = function (X, e) { var pic = X.img("load" + (X.o.frame % 8)) || X.img("load0"); if (!pic) return; var q = X.pos(e), r = X.box(q, pic.w, pic.h); X.drawImg(pic, r); X.hit(e, r); };
  P.SaveIcon = function (X, e) {
    var q = X.pos(e), r = X.box(q, q.w, q.h), c = X.c;
    if (!X.o.saveIcons) { X.hit(e, r); return; }
    c.save(); c.strokeStyle = "rgba(255,255,255,.55)"; c.setLineDash([3, 3]); c.strokeRect(r.x, r.y, r.w, r.h);
    c.fillStyle = "rgba(255,255,255,.7)"; c.font = "10px sans-serif"; c.textAlign = "center"; c.fillText("3D save icon", r.x + r.w / 2, r.y + r.h / 2 + 3); c.restore();
    X.hit(e, r);
  };
  P.ItemsList = function (X, e, pd) {
    var p = e.props, q = X.pos(e);
    if (p.devices && !devMatch(p.devices, pd.dev)) return;
    if (num(p.hidden, 0)) { X.hit(e, X.box(q, q.w, q.h)); return; }
    var font = X.font(num(p.font, 0)), rows = Math.max(1, Math.floor(q.h / ROW_H));
    var x = q.x, y = q.y; if (q.al) { x -= q.w >> 1; y -= q.h >> 1; }
    var start = Math.max(0, Math.min(pd.sel - Math.floor(rows / 2), pd.items.length - rows));
    var hasDeco = !!p.decorator && X.o.art && !(String(p.decorator).toUpperCase() === "ICO" && X.o.discArt === false);
    var ss = X.o.selStyle == null ? 7 : X.o.selStyle, hl = (p.highlight || "bar").toLowerCase();
    for (var i = 0; i < rows && start + i < pd.items.length; i++) {
      var idx = start + i, it = pd.items[idx], isSel = idx === pd.sel;
      var rowY = y + i * ROW_H;
      if (isSel && q.al !== 1) {
        var hlX = q.x - (q.al === 2 ? q.w : 0);
        if (hl === "bar") {
          if (ss & 4) { X.c.fillStyle = "rgba(30,60,156," + (0x5C / 0x80) + ")"; X.c.fillRect(hlX - 12, rowY + 1, q.w + 12, ROW_H - 2); }
          if (ss & 2) { X.c.fillStyle = "rgb(180,200,255)"; X.c.fillRect(hlX - 12, rowY + 1, 3, ROW_H - 2); }
        } else if (hl === "line" && (ss & 4)) { X.c.fillStyle = "rgb(180,200,255)"; X.c.fillRect(hlX - 12, rowY + ROW_H - 3, q.w + 12, 2); }
      }
      var color = isSel && (ss & 1) ? textCss(X.col.sel) : (p.color ? textCss(p.color) : textCss(X.col.title));
      var tx = x;
      if (hasDeco) { var d = sampleDisc(it); X.drawImg(d, { x: x, y: rowY, w: X.wide ? DECORATOR * 0.75 : DECORATOR, h: DECORATOR }); tx += DECORATOR; }
      var align = q.al === 1 ? "center" : "left";
      X.text(it.name, align === "center" ? q.x : tx, rowY, align, color, font, q.w - (hasDeco ? DECORATOR : 0));
    }
    X.hit(e, { x: x, y: y, w: q.w, h: Math.max(ROW_H, Math.min(rows, pd.items.length) * ROW_H) });
  };
  function devMatch(list, dev) {
    var names = ["usb", "mmce", "hdd", "eth", "app", "fav"];
    return String(list).toLowerCase().split(/[, \t]+/).some(function (n) { return n === names[dev] || (n === "smb" && dev === 3); });
  }
  var HINTS_MAIN = [["start", "menu", "Menu"], ["cross", "run", "Run"], ["triangle", "info", "Info"], ["square", "options", "Options"], ["select", "refresh", "Refresh"], ["R3", "star", "Favourite"], ["L3", "view", "View"]];
  var HINTS_INFO = [["cross", "run", "Run"], ["circle", "back", "Back"]];
  P.HintText = function (X, e, pd, info) {
    var p = e.props, gl = X.b.globals, q = X.pos(e);
    if (p.devices && !devMatch(p.devices, pd.dev)) return;
    var list = (info ? HINTS_INFO : HINTS_MAIN).slice();
    if (info && p.show_run !== undefined && num(p.show_run, 1) === 0) list = list.filter(function (h) { return h[1] !== "run"; });
    var hide = String(gl.hints_hide || "").toLowerCase().split(/[, \t]+/).filter(Boolean);
    list = list.filter(function (h) { return hide.indexOf(h[1]) < 0 && hide.indexOf(h[0].toLowerCase()) < 0; });
    var order = String(gl.hints_order || "").toLowerCase().split(/[, \t]+/).filter(Boolean);
    if (order.length) {
      var first = [];
      order.forEach(function (o) { list.forEach(function (h) { if ((h[1] === o || h[0].toLowerCase() === o) && first.indexOf(h) < 0) first.push(h); }); });
      list = first.concat(list.filter(function (h) { return first.indexOf(h) < 0; }));
    }
    var labels = gl.hints_labels === undefined || num(gl.hints_labels, 1) !== 0, column = String(gl.hints_layout || "").toLowerCase() === "column";
    var pitch = Math.max(12, Math.min(96, num(gl.hints_spacing, 28))), font = X.font(num(p.font, 0)), color = textCss(X.col.ui);
    var set = String(gl.glyph_set || "theme").toLowerCase();
    var x = q.x, y = q.y, x0 = x, maxX = x;
    list.forEach(function (h) {
      var glyph = set === "standard" || set === "white" ? X.a.builtin("glyph_" + set + "_" + h[0]) : (set === "builtin" ? X.a.builtin(h[0]) : X.img(h[0]));
      var gw = X.wide ? 15 : 20;
      if (glyph) X.drawImg(glyph, { x: x, y: y, w: gw, h: 20 });
      var nx = x + gw + 4;
      if (labels) nx += X.text(h[2], nx, y + 10, "left", color, font, 0, "middle") + 14; else nx += 6;
      if (column) { maxX = Math.max(maxX, nx); y += pitch; } else { x = nx; maxX = x; }
    });
    X.hit(e, { x: x0, y: q.y, w: Math.max(40, maxX - x0), h: column ? y - q.y : 22 });
  };
  P.InfoHintText = function (X, e, pd) { P.HintText(X, e, pd, true); };

  // ---- Presenters --------------------------------------------------------------------------------------
  P.Coverflow = function (X, e, pd) {
    var p = e.props, q = X.pos(e), n = X.o.coverCount === 5 ? 5 : 3, items = pd.items, half = (n - 1) / 2;
    var w = q.w, h = q.h; if (X.wide) w = w * 3 / 4;
    var off = num(X.b.globals.coverflow_cover_offset, 0), boxes = [];
    for (var d = -half; d <= half; d++) {
      var idx = (pd.sel + d + items.length) % items.length, it = items[idx], ad = Math.abs(d);
      var s = ad === 0 ? 1.0 : ad === 1 ? 0.78 : 0.6;
      var cw = w * s, ch = h * s, cx = q.x + d * w * (n === 5 ? 0.82 : 1.05) + (ad ? 0 : w * off / 256), cy = q.y + (h - ch) / 2;
      boxes.push({ d: d, it: it, r: { x: cx - cw / 2, y: cy - ch / 2, w: cw, h: ch } });
    }
    boxes.sort(function (a, b) { return Math.abs(b.d) - Math.abs(a.d); });
    boxes.forEach(function (b) {
      var art = artFor(X, e, b.it, pd, "COV") || X.img(p["default"]);
      X.c.save(); if (b.d !== 0 && X.o.dim) X.c.globalAlpha = 0.6;
      X.drawFramed(art, e, b.r); X.c.restore();
      if (num(p.reflection, 0) && art) reflect(X, art, b.r);
    });
    X.hit(e, boxes[boxes.length - 1].r);
  };
  P.PSBBNFlow = function (X, e, pd) {
    var p = e.props, q = X.pos(e), c = X.c, size = q.w || 204, items = pd.items;
    var sw = X.wide ? size * 3 / 4 : size;
    // header and hint bands with rules
    c.fillStyle = "#000"; c.fillRect(0, 0, W, 52); c.fillRect(0, H - 46, W, 46);
    c.fillStyle = "rgba(255,255,255,.35)"; c.fillRect(0, 52, W, 1); c.fillRect(0, H - 46, W, 1);
    // tail of covers turned away to the left
    for (var k = 6; k >= 1; k--) {
      var it = items[(pd.sel + k) % items.length], art = artFor(X, e, it, { square: true }, "COV") || X.img(p["default"]);
      if (!art) continue;
      var s = 1 - k * 0.11, tw = sw * s * 0.42, th = size * s, tx = q.x - sw / 2 - 26 - k * (sw * 0.36), ty = q.y - th / 2 + k * 4;
      c.save(); c.globalAlpha = Math.max(0.15, 0.85 - k * 0.12);
      c.setTransform(1, -0.12, 0, 1, 0, 0); c.drawImage(art.img || art, tx, ty + tx * 0.12, tw, th);
      c.restore();
    }
    var cur = items[pd.sel], fart = artFor(X, e, cur, { square: true }, "COV") || X.img(p["default"]);
    var r = { x: q.x - sw / 2, y: q.y - size / 2, w: sw, h: size };
    if (fart) { c.save(); roundRect(c, r.x, r.y, r.w, r.h, 10); c.clip(); X.drawFramed(fart, e, r); c.restore(); }
    c.save(); c.strokeStyle = "rgba(255,255,255,.85)"; c.lineWidth = 2; roundRect(c, r.x, r.y, r.w, r.h, 10); c.stroke(); c.restore();
    var font = X.font(num(p.font, 0));
    X.text((pd.sel + 1) + "/" + items.length, r.x + r.w, r.y + r.h + 6, "right", textCss(X.col.text), font);
    X.hit(e, r);
  };
  function roundRect(c, x, y, w, h, rr) { c.beginPath(); c.moveTo(x + rr, y); c.arcTo(x + w, y, x + w, y + h, rr); c.arcTo(x + w, y + h, x, y + h, rr); c.arcTo(x, y + h, x, y, rr); c.arcTo(x, y, x + w, y, rr); c.closePath(); }
  P.Grid = function (X, e, pd) {
    var p = e.props, gp = X.gridProps || p, q = X.pos(e), c = X.c, items = pd.items;
    var rows = Math.max(1, Math.min(4, num(gp.rows, 3))), cols = Math.max(1, Math.min(8, num(gp.columns, 4))), gap = num(gp.spacing, 16);
    while (rows * cols > 24 && rows > 1) rows--;
    var cw = q.w, ch = q.h; if (X.wide) cw = cw * 3 / 4;
    var totW = cols * cw + (cols - 1) * gap * (X.wide ? 0.75 : 1), totH = rows * ch + (rows - 1) * gap;
    var x0 = q.x - totW / 2, y0 = q.y - totH / 2, selR = null, sel = pd.sel % (rows * cols);
    var vig = num(gp.vignette, 1);
    for (var i = 0; i < rows * cols && i < items.length; i++) {
      var r = { x: x0 + (i % cols) * (cw + gap * (X.wide ? 0.75 : 1)), y: y0 + Math.floor(i / cols) * (ch + gap), w: cw, h: ch };
      if (i === sel) { selR = r; continue; }
      var art = artFor(X, e, items[i], pd, "COV");
      if (!art && gp.coverless === "name" || !art && gp.coverless === "serial") { glassTile(X, r, gp.coverless === "serial" ? items[i].id : items[i].name); continue; }
      art = art || X.img(p["default"]);
      c.save(); c.globalAlpha = vig === 0 ? 1 : vig === 2 || X.o.dim ? 0.45 : 0.72; X.drawFramed(art, e, r); c.restore();
    }
    if (selR) {
      var g2 = { x: selR.x - selR.w * 0.06, y: selR.y - selR.h * 0.06, w: selR.w * 1.12, h: selR.h * 1.12 };
      c.save(); c.shadowColor = textCss(X.col.sel, 0.8); c.shadowBlur = 16; c.fillStyle = "rgba(0,0,0,.01)"; c.fillRect(g2.x, g2.y, g2.w, g2.h); c.restore();
      X.drawFramed(artFor(X, e, items[sel], pd, "COV") || X.img(p["default"]), e, g2);
      c.save(); c.strokeStyle = textCss(X.col.sel); c.lineWidth = 2; c.strokeRect(g2.x, g2.y, g2.w, g2.h); c.restore();
    }
    var font = X.font(num(p.font, 0));
    if (gp.counter === undefined || num(gp.counter, 1)) X.text((pd.sel + 1) + "/" + items.length, x0 + totW, y0 + totH + 10, "right", textCss(X.col.text), font);
    if (num(gp.title, 0)) X.text(items[pd.sel].name, q.x, gp.title_y !== undefined ? num(gp.title_y, 0) : y0 + totH + 28, "center", textCss(X.col.sel), X.font(num(gp.title_font, 0)), 0, "middle");
    if (gp.devices === undefined || num(gp.devices, 1)) {
      c.fillStyle = "rgba(0,0,0,.45)"; c.fillRect(0, 60, X.wide ? 24 : 32, H - 120);
      ["usb", "mmce", "hdd", "eth", "app", "fav"].forEach(function (n, k) { var ic = X.img(n); if (ic) { c.save(); c.globalAlpha = k === pd.dev ? 1 : 0.45; X.drawImg(ic, { x: 4, y: 80 + k * 34, w: X.wide ? 18 : 24, h: 24 * ic.h / ic.w || 24 }); c.restore(); } });
    }
    X.hit(e, { x: x0, y: y0, w: totW, h: totH });
  };
  function glassTile(X, r, label) {
    var c = X.c; c.save(); c.fillStyle = "rgba(255,255,255,.12)"; c.fillRect(r.x, r.y, r.w, r.h); c.strokeStyle = "rgba(255,255,255,.3)"; c.strokeRect(r.x, r.y, r.w, r.h);
    c.fillStyle = "#fff"; c.font = "11px sans-serif"; c.textAlign = "center"; wrapText(c, label, r.x + r.w / 2, r.y + r.h / 2, r.w - 6, 13); c.restore();
  }

  // ---- Entry point ---------------------------------------------------------------------------------------
  // opts: { wide, family, page:'main'|'info', art, discArt, selStyle, coverCount, dim, saveIcons, frame, fonts, isBuiltin, listCfgHidden, sel }
  function render(canvas, built, opts, assets) {
    var X = new Ctx(canvas, built, opts, assets), c = X.c;
    c.setTransform(1, 0, 0, 1, 0, 0); c.clearRect(0, 0, W, H); c.fillStyle = "#000"; c.fillRect(0, 0, W, H);
    var pd = pageData(opts), info = opts.page === "info", hasBg = false;
    var firstGrid = built.elements.filter(function (e) { return e.type === "Grid"; })[0];
    X.gridProps = firstGrid ? firstGrid.props : null;
    var drawn = [];
    // OPL adds a plain Background when block 0 is not one
    if (!built.elements.length || built.elements[0].type !== "Background") P.Background(X, { block: "(auto background)", type: "Background", props: {} });
    built.elements.forEach(function (e) {
      if (e.off || !e.type) return;
      if (num(e.props.enabled, 1) === 0) return;
      var fn = P[e.type];
      if (!fn) return;
      try { fn(X, e, pd, info); drawn.push(e.block); } catch (err) { if (g.console) console.warn("draw", e.block, err); }
    });
    var hasList = built.elements.some(function (e) { return e.type === "ItemsList" && !e.off; });
    if (!info && !hasList) P.ItemsList(X, { block: "(auto list)", type: "ItemsList", props: { x: "42", y: "42", width: "373", height: "316" } }, pd);
    return X.hits;
  }

  // Where an element's anchor sits, for the editor: q from Ctx.pos, and kx = how far the anchor moves on screen
  // per unit of x (0.75 when hd_autofit squeezes it in 16:9, else 1).
  function posOf(built, e, wide) {
    var q = Ctx.prototype.pos.call({ b: built, wide: wide }, e);
    var raw = Ctx.prototype.pos.call({ b: built, wide: false }, e);
    q.kx = q.af ? 0.75 : 1;
    q.rx = wide && q.sc ? 0.75 : 1; // drawn width per unit of width=
    q.ax = raw.x; q.ay = raw.y;      // the anchor as the file states it, before the 16:9 squeeze
    return q;
  }
  g.RTC_RENDER = { render: render, posOf: posOf, autofitX: autofitX, GAMES: GAMES, W: W, H: H };
})(window);
