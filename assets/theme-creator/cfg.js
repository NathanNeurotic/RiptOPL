/* RiptOPL Theme Creator: reading, writing and checking conf_theme.cfg the way RiptOPL does.
 * Ports of: config.cpp configReadFileBuffer/splitAssignment/parsePrefix, themes.cpp thmBlockPresent and
 * the per-slot fallback chains, themeviews.cpp thmViewScan/thmViewSynthesize and the _wsX/_sd/_hd,
 * _desc and alt= passes. Runs in the browser or under Node for the tests. */
(function (g) {
  "use strict";
  var S = g.RTC_SCHEMA;

  function isWS(c) { return c === " " || c === "\t"; }

  // ---- Reading -------------------------------------------------------------------------------------
  // Returns { map: Map(key -> value) in OPL's order (first set keeps its place, later sets replace the
  // value), notes: [{line, level, msg}] }.
  function parse(text) {
    var map = new Map(), notes = [];
    var prefix = "", latched = false;
    var lines = String(text).replace(/\r\n?/g, "\n").split("\n");
    for (var n = 0; n < lines.length; n++) {
      var line = lines[n], lineno = n + 1;
      var p = 0;
      while (p < line.length && isWS(line[p])) p++;
      var rest = line.slice(p);
      if (rest.replace(/[ \t]+$/, "") === "") continue;
      if (rest[0] === "#" || rest.slice(0, 2) === "//" || rest.slice(0, 2) === "/*") continue;
      if (!latched) {
        latched = true;
        if (/^[A-Za-z*][-A-Za-z0-9_*]*[ \t]+=.*;[ \t]*$/.test(rest))
          notes.push({ line: lineno, level: "error", msg: "This file looks like libconfig (key = value;). Theme files use key=value with no spaces or semicolons." });
      }
      var eq = rest.indexOf("=");
      if (eq >= 0) {
        var key = rest.slice(0, eq), val = rest.slice(eq + 1);
        if (!isWS(line[0])) prefix = "";
        var full = prefix ? prefix + "_" + key : key;
        if (key === "") continue; // configKeyValidate refuses an empty key
        if (/[ \t]$/.test(key))
          notes.push({ line: lineno, level: "error", msg: "Space before '=' becomes part of the name \"" + key + "\", so OPL never finds it." });
        if (/[ \t]\S+=/.test(val))
          notes.push({ line: lineno, level: "error", msg: "Only the first key=value on a line is read; put \"" + val.trim().split(/[ \t]+/).slice(1).join(" ") + "\" on lines of their own." });
        if (!isWS(line[0]) && S.PROPS[key] && key !== "alt" && n > 0 && lastWasBlock(lines, n))
          notes.push({ line: lineno, level: "error", msg: "\"" + key + "\" is not indented, so it is a top-level key and not part of the block above. Indent it with a tab." });
        if (full.length > S.KEY_MAX) {
          notes.push({ line: lineno, level: "error", msg: "Name \"" + full + "\" is " + full.length + " characters; OPL keeps only the first " + S.KEY_MAX + "." });
          full = full.slice(0, S.KEY_MAX);
        }
        if (val.length > S.VALUE_MAX) {
          notes.push({ line: lineno, level: "warn", msg: "Value is cut to " + S.VALUE_MAX + " characters." });
          val = val.slice(0, S.VALUE_MAX);
        }
        if (map.has(full) && map.get(full) !== val)
          notes.push({ line: lineno, level: "warn", msg: "\"" + full + "\" is set again here; this later value wins." });
        map.set(full, val);
      } else {
        var col = line.indexOf(":");
        if (col > 0) {
          prefix = line.slice(0, col);
          if (prefix !== prefix.trim())
            notes.push({ line: lineno, level: "error", msg: "Spaces around the block name \"" + prefix + "\" become part of it. Start block lines at the left edge." });
          if (prefix.length > S.KEY_MAX) prefix = prefix.slice(0, S.KEY_MAX);
        } else {
          notes.push({ line: lineno, level: "warn", msg: "OPL skips this line: it has no '=' and no ':'." });
        }
      }
    }
    return { map: map, notes: notes };
  }
  function lastWasBlock(lines, n) {
    for (var i = n - 1; i >= 0; i--) {
      var t = lines[i].trim();
      if (!t || t[0] === "#") continue;
      return /^[^=]+:\s*$/.test(lines[i]) || isWS(lines[i][0]);
    }
    return false;
  }

  // ---- Writing -------------------------------------------------------------------------------------
  // Groups "<block>_<prop>" keys under "<block>:" lines with tab-indented props, the format OPL writes
  // and reads back to the same keys. Globals first, then blocks: base families in order, then views.
  function blockOf(key) {
    var us = key.indexOf("_");
    while (us > 0) {
      var b = key.slice(0, us);
      if (S.BLOCK_RE.test(b)) return b;
      us = key.indexOf("_", us + 1);
    }
    return null;
  }
  function blockSortKey(b) {
    var m = S.BLOCK_RE.exec(b);
    var v = m[1] ? S.VIEWS.indexOf(m[1]) + 1 : 0;
    return [v, S.FAMILY_NAMES.indexOf(m[2]), parseInt(m[3], 10)];
  }
  function serialize(map, header) {
    var globals = [], blocks = new Map();
    map.forEach(function (val, key) {
      var b = blockOf(key);
      if (b) {
        if (!blocks.has(b)) blocks.set(b, []);
        blocks.get(b).push([key.slice(b.length + 1), val]);
      } else globals.push([key, val]);
    });
    var out = [];
    if (header) header.split("\n").forEach(function (l) { out.push("# " + l); });
    globals.forEach(function (kv) { out.push(kv[0] + "=" + kv[1]); });
    var names = Array.from(blocks.keys()).sort(function (a, b) {
      var x = blockSortKey(a), y = blockSortKey(b);
      for (var i = 0; i < 3; i++) if (x[i] !== y[i]) return x[i] - y[i];
      return 0;
    });
    names.forEach(function (b) {
      out.push(b + ":");
      var props = blocks.get(b);
      // type first, then the rest in their own order
      props.sort(function (p, q) { return (p[0] === "type" ? -1 : 0) - (q[0] === "type" ? -1 : 0); });
      props.forEach(function (kv) { out.push("\t" + kv[0] + "=" + kv[1]); });
    });
    return out.join("\n") + "\n";
  }

  // ---- The passes OPL runs before building a theme ---------------------------------------------------
  // opts: { wide: bool, desc: bool, alt: bool }
  function applyPasses(src, opts) {
    var m = new Map(src);
    // _wsX first, then _sd/_hd, which therefore win (themeviews.cpp thmApplyDisplayLayout)
    src.forEach(function (v, k) {
      if (opts.wide && /_wsX$/.test(k)) m.set(k.slice(0, -4) + "_x", v);
    });
    var suf = opts.wide ? "_hd" : "_sd";
    src.forEach(function (v, k) {
      if (k.length > 3 && k.slice(-3) === suf) m.set(k.slice(0, -3), v);
    });
    // _desc (thmApplyListDescLayout): only while List Title and Description shows the CFG text
    if (opts.desc) Array.from(m.keys()).forEach(function (k) {
      if (k.length > 5 && k.slice(-5) === "_desc") m.set(k.slice(0, -5), m.get(k));
    });
    // alt= (Alternate Background)
    if (opts.alt) {
      var top = m.get("alt");
      Array.from(m.keys()).forEach(function (k) {
        var t = /^(.*)_type$/.exec(k);
        if (!t || !blockOf(k)) return;
        var b = t[1];
        if (m.has(b + "_alt")) m.set(b + "_default", m.get(b + "_alt"));
        else if (top && m.get(k) === "Background") m.set(b + "_default", top);
      });
    }
    return m;
  }

  function getInt(m, k, d) {
    if (!m.has(k)) return d;
    var v = parseInt(m.get(k), 10);
    return isNaN(v) ? 0 : v; // atoi
  }
  function present(m, b) {
    if (m.has(b + "_enabled") && getInt(m, b + "_enabled", 1) === 0) return true;
    return m.has(b + "_type");
  }

  // ---- Views (themeviews.cpp) ----------------------------------------------------------------------
  function nativeView(m) {
    for (var j = 0; j < 64; j++) {
      var b = "main" + j;
      if (!present(m, b)) break;
      var t = m.get(b + "_type");
      if (t === "PSBBNFlow") return "psbbn";
      if (t === "Coverflow") return "coverflow";
      if (t === "Grid") return "grid";
    }
    return "list";
  }
  function viewScan(m) {
    var info = { native: nativeView(m), blocks: {}, own: {}, listed: false, offered: [] };
    m.forEach(function (v, k) {
      var dot = k.indexOf(".");
      if (dot <= 0 || dot >= 16) return;
      var w = k.slice(0, dot);
      if (S.VIEWS.indexOf(w) < 0) return;
      info.blocks[w] = 1;
      if (k.slice(dot + 1) === "inherit" && v === "0") info.own[w] = 1;
    });
    var mask = {};
    if (m.has("views")) String(m.get("views")).split(/[, \t]+/).forEach(function (w) { if (S.VIEWS.indexOf(w) >= 0) mask[w] = 1; });
    info.listed = Object.keys(mask).length > 0;
    if (!info.listed) { mask[info.native] = 1; Object.keys(info.blocks).forEach(function (w) { mask[w] = 1; }); }
    info.mask = mask;
    var dv = m.get("default_view");
    if (dv && mask[dv]) info.defaultView = dv;
    else if (mask[info.native]) info.defaultView = info.native;
    else info.defaultView = S.VIEWS.filter(function (w) { return mask[w]; })[0];
    return info;
  }

  var SYNTH = [
    ["main", ["main"]], ["appsMain", ["appsMain", "main"]], ["vcdMain", ["vcdMain", "main"]], ["favsMain", ["favsMain", "main"]],
    ["favsVcdMain", ["favsVcdMain", "vcdMain", "main"]], ["favsAppsMain", ["favsAppsMain", "appsMain", "main"]]
  ];
  var PRESENTER = { list: "ItemCover", coverflow: "Coverflow", psbbn: "PSBBNFlow", grid: "Grid" };
  function isPresenter(t) { return t === "ItemCover" || t === "Coverflow" || t === "PSBBNFlow" || t === "Grid"; }
  function findTmpl(tmpl, vn, chain, type) {
    var j, c, b;
    for (c = 0; c < chain.length; c++) for (j = 0; j < 64; j++) { b = vn + "." + chain[c] + j; if (tmpl.get(b + "_type") === type) return b; }
    for (c = 0; c < chain.length; c++) for (j = 0; j < 64; j++) { b = chain[c] + j; if (tmpl.get(b + "_type") === type) return b; }
    return null;
  }
  function copyBlock(to, from, src, dst) {
    Array.from(from.keys()).forEach(function (k) {
      if (k.indexOf(src + "_") === 0) to.set((dst + "_" + k.slice(src.length + 1)).slice(0, S.KEY_MAX), from.get(k));
    });
  }
  function synthesize(m, tmpl, view) {
    var vn = view, presenter = PRESENTER[view];
    if (!findTmpl(tmpl, vn, ["main"], presenter)) return false;
    var has = {}, presenterSlot = -1;
    SYNTH.forEach(function (fam, f) {
      var tplList = findTmpl(tmpl, vn, fam[1], "ItemsList");
      for (var j = 0; j < 64; j++) {
        var src = fam[0] + j;
        if (!present(m, src)) continue;
        var dst = vn + "." + src, type = m.get(src + "_type");
        if (getInt(m, src + "_enabled", 1) === 0) m.set(dst + "_enabled", "0");
        else if (isPresenter(type)) {
          var tpl = !has[f] && findTmpl(tmpl, vn, fam[1], presenter);
          if (tpl) { copyBlock(m, tmpl, tpl, dst); has[f] = 1; if (f === 0) presenterSlot = j; }
          else m.set(dst + "_enabled", "0");
        } else {
          copyBlock(m, m, src, dst);
          if (type === "ItemsList" && tplList && !m.has(src + "_devices"))
            ["x", "y", "width", "height", "aligned"].forEach(function (k) { if (tmpl.has(tplList + "_" + k)) m.set(dst + "_" + k, tmpl.get(tplList + "_" + k)); });
        }
      }
    });
    if (presenterSlot < 0) {
      for (presenterSlot = 0; presenterSlot < 64; presenterSlot++)
        if (!present(m, "main" + presenterSlot) && !present(m, vn + ".main" + presenterSlot)) break;
      copyBlock(m, tmpl, findTmpl(tmpl, vn, ["main"], presenter), vn + ".main" + presenterSlot);
      has[0] = 1;
    }
    for (var f = 1; f <= 2; f++) {
      if (has[f]) continue;
      var nm = SYNTH[f][0], src2 = nm + presenterSlot, dst2 = vn + "." + nm + presenterSlot;
      if (present(m, src2) || present(m, dst2)) continue;
      var t2 = findTmpl(tmpl, vn, [nm], presenter);
      if (t2) copyBlock(m, tmpl, t2, dst2);
    }
    return true;
  }

  // Build what OPL would draw for one page. Returns { view, viewNote, globals, elements:[{block, type, props}] }.
  // req: { view, family, wide, desc, alt, tmpl (built-in Map or null), isBuiltin }
  function build(src, req) {
    var m = applyPasses(src, req);
    var info = viewScan(m);
    var view = info.mask[req.view] ? req.view : info.defaultView, note = "";
    if (!req.isBuiltin && !info.listed && !info.mask[req.view]) {
      if (req.tmpl) {
        var t = applyPasses(req.tmpl, req);
        if (synthesize(m, t, req.view)) { info.blocks[req.view] = 1; view = req.view; note = "Your theme does not describe this view, so RiptOPL builds it from your layout with the built-in " + S.VIEW_LABEL[req.view] + " covers (a fallback view)."; }
      }
    } else if (!info.mask[req.view]) {
      note = "This theme's views= does not offer " + S.VIEW_LABEL[req.view] + "; RiptOPL shows " + S.VIEW_LABEL[view] + " instead.";
    }
    var prefix = info.blocks[view] ? view : ""; // thmViewPrefix
    var inherit = !info.own[view];
    function gv(k) { return prefix && m.has(prefix + "." + k) ? m.get(prefix + "." + k) : m.get(k); }
    var globals = {};
    Object.keys(S.GLOBALS).forEach(function (k) { var v = S.VIEW_GLOBALS.indexOf(k) >= 0 ? gv(k) : m.get(k); if (v !== undefined) globals[k] = v; });
    var fam = S.FAMILIES[req.family] || S.FAMILIES.main;
    var chain = fam.chain;
    var elements = [];
    var declared = true;
    if (fam.optIn) { // opt-in families only exist when one of their blocks is declared
      declared = false;
      for (var d = 0; d < 64 && !declared; d++) if (resolve(d, [req.family], false)) declared = true;
      if (!declared) chain = chain.slice(1);
    }
    function resolve(j, ch, allowBase) {
      var i;
      if (prefix) for (i = 0; i < ch.length; i++) { var vb = prefix + "." + ch[i] + j; if (present(m, vb)) return vb; }
      if (allowBase !== false && inherit) for (i = 0; i < ch.length; i++) { var bb = ch[i] + j; if (present(m, bb)) return bb; }
      return null;
    }
    for (var j = 0; j < 64; j++) {
      var b = resolve(j, chain);
      if (!b) break;
      if (getInt(m, b + "_enabled", 1) === 0) { elements.push({ block: b, slot: j, type: m.get(b + "_type") || "", props: {}, off: true }); continue; }
      var props = {};
      m.forEach(function (v, k) { if (k.indexOf(b + "_") === 0) props[k.slice(b.length + 1)] = v; });
      elements.push({ block: b, slot: j, type: props.type, props: props });
    }
    // Background only counts as block 0; ItemText moves to the end of its screen
    elements = elements.filter(function (e, i) { return !(e.type === "Background" && i !== 0); });
    var texts = elements.filter(function (e) { return e.type === "ItemText"; });
    elements = elements.filter(function (e) { return e.type !== "ItemText"; }).concat(texts);
    return { view: view, viewNote: note, info: info, globals: globals, elements: elements, map: m };
  }

  // ---- Checking ------------------------------------------------------------------------------------
  // assets: { has(name) -> bool, size(name) -> {w,h} | null, fonts: Set of file names }
  function check(text, assets) {
    var r = parse(text), m = r.map, out = r.notes.slice();
    function add(level, msg, key) { out.push({ level: level, msg: msg, key: key }); }
    function stripSuffix(k) { return k.replace(/_(hd|sd|desc)$/, "").replace(/_(hd|sd)$/, ""); }
    var seenBlocks = {};
    m.forEach(function (v, k0) {
      var k = stripSuffix(k0);
      var b = blockOf(k);
      if (b) {
        seenBlocks[b] = 1;
        var prop = k.slice(b.length + 1);
        var type = m.get(b + "_type") || m.get(b + "_type_hd") || m.get(b + "_type_sd") || m.get(b + "_type_desc");
        if (prop === "wsX") return;
        if (!S.PROPS[prop] && !(type === "Grid" && S.GRID_ONLY_NAMES[prop]))
          add("warn", "\"" + k0 + "\": OPL has no \"" + prop + "\" property, so it is ignored.", k0);
        if (prop === "type" && S.TYPE_NAMES.indexOf(v) < 0)
          add("error", "\"" + k0 + "=" + v + "\": unknown element type. Known types: " + S.TYPE_NAMES.join(", ") + ". (Names are case-sensitive.)", k0);
        if (prop === "color" && !/^#[0-9A-Fa-f]{6}$/.test(v)) add("warn", "\"" + k0 + "=" + v + "\": colours are #RRGGBB.", k0);
        if ((prop === "default" || prop === "overlay" || prop === "overlay2" || prop === "alt") && v) checkImage(k0, v);
        if (prop === "devices" && type !== "Grid") String(v).split(/[, \t]+/).filter(Boolean).forEach(function (d) {
          if (S.DEVICE_NAMES.indexOf(d.toLowerCase()) < 0) add("warn", "\"" + k0 + "\": unknown device \"" + d + "\" is skipped.", k0);
        });
        if (prop === "font") { var fi = parseInt(v, 10); if (fi > 0 && !m.has("font" + fi)) add("info", "\"" + k0 + "=" + v + "\": font" + fi + " is not set, so it uses the main font.", k0); }
        return;
      }
      var dot = k.indexOf(".");
      if (dot > 0) {
        var w = k.slice(0, dot), rest = k.slice(dot + 1);
        if (S.VIEWS.indexOf(w) >= 0) {
          if (S.VIEW_GLOBALS.indexOf(rest) < 0) add("warn", "\"" + k0 + "\": a view can only override " + S.VIEW_GLOBALS.join(", ") + "; this key is ignored.", k0);
          return;
        }
        add("warn", "\"" + k0 + "\": \"" + w + "\" is not a view (list, coverflow, psbbn, grid).", k0);
        return;
      }
      if (/^(main|info|appsMain|appsInfo|favsMain|favsInfo|vcdMain|vcdInfo|favsVcdMain|favsVcdInfo|favsAppsMain|favsAppsInfo)\d*_?/i.test(k) && !S.GLOBALS[k]) {
        add("warn", "\"" + k0 + "\" looks like an element key, but its block name is not one OPL reads (family names are case-sensitive, e.g. appsMain3).", k0);
        return;
      }
      if (!S.GLOBALS[k]) { add("warn", "\"" + k0 + "\": not a theme key OPL reads, so it is ignored.", k0); return; }
      var G = S.GLOBALS[k];
      if (G.kind === "color" && !/^#[0-9A-Fa-f]{6}$/.test(v)) add("warn", "\"" + k0 + "=" + v + "\": colours are #RRGGBB.", k0);
      if (G.kind === "font" && v && v.indexOf("builtin") !== 0 && assets && !assets.fonts.has(v)) add("error", "\"" + k0 + "=" + v + "\": that font file is not in the theme. Add it on the Fonts tab.", k0);
      if (k === "alt" && v) checkImage(k0, v);
      if (k === "views") String(v).split(/[, \t]+/).filter(Boolean).forEach(function (w2) { if (S.VIEWS.indexOf(w2) < 0) add("warn", "views=: \"" + w2 + "\" is not a view.", k0); });
    });
    function checkImage(k, v) {
      if (!assets) return;
      if (assets.has(v)) {
        var sz = assets.size(v);
        if (sz && (sz.w > S.TEX_MAX || sz.h > S.TEX_MAX)) add("error", "\"" + v + ".png\" is " + sz.w + " x " + sz.h + "; OPL refuses pictures over " + S.TEX_MAX + " pixels a side. Use Shrink on the Images tab.", k);
        return;
      }
      if (S.BUILTIN_ALL[v] && !S.BUILTIN_EMPTY[v]) return;
      if (S.BUILTIN_EMPTY[v]) add("info", "\"" + k + "=" + v + "\": OPL has no built-in " + v + " picture; nothing draws unless the theme ships " + v + ".png.", k);
      else add("error", "\"" + k + "=" + v + "\": no " + v + ".png in the theme and no built-in picture of that name. Add it on the Images tab.", k);
    }
    // Blocks: contiguous numbering, Background first, one plain ItemsList
    var byFamily = {};
    Object.keys(seenBlocks).forEach(function (b) {
      var mm = S.BLOCK_RE.exec(b);
      var fk = (mm[1] ? mm[1] + "." : "") + mm[2];
      (byFamily[fk] = byFamily[fk] || []).push(parseInt(mm[3], 10));
    });
    // The parser walks slot 0, 1, 2... through each family's fallback chain (view blocks first) and stops
    // at the first slot where nothing in the chain is present: a block past that point is never read.
    Object.keys(byFamily).forEach(function (fk) {
      var dot = fk.indexOf("."), vw = dot > 0 ? fk.slice(0, dot) : "", famName = dot > 0 ? fk.slice(dot + 1) : fk;
      var chain = S.FAMILIES[famName].chain, own = vw && m.get(vw + ".inherit") === "0";
      function slotPresent(j) {
        for (var c = 0; c < chain.length; c++) {
          if (vw && present(m, vw + "." + chain[c] + j)) return true;
          if ((!vw || !own) && present(m, chain[c] + j)) return true;
        }
        return false;
      }
      var len = 0;
      while (len < 64 && slotPresent(len)) len++;
      var lost = byFamily[fk].filter(function (x) { return x >= len; }).sort(function (a, b) { return a - b; });
      if (lost.length) add("error", "Slot " + len + " is empty in " + (vw ? "the " + S.VIEW_LABEL[vw] + " view's " : "") + chain[chain.length - 1] + " list, so " + lost.map(function (x) { return fk + x; }).join(", ") + (lost.length > 1 ? " are" : " is") + " never read. Number blocks without gaps (Elements tab: Renumber).", fk + lost[0]);
    });
    ["main", "info"].forEach(function (f) {
      var nums = byFamily[f] || [];
      nums.forEach(function (x) { if (x !== 0 && m.get(f + x + "_type") === "Background") add("warn", f + x + ": a Background is only used as block 0.", f + x); });
    });
    var plainList = false, anyList = false;
    for (var j = 0; j < 64; j++) {
      if (!present(m, "main" + j)) break;
      if (m.get("main" + j + "_type") === "ItemsList") { anyList = true; if (!m.has("main" + j + "_devices")) plainList = true; }
    }
    if (!plainList) add("warn", anyList ? "Every games-list ItemsList has devices=. Add one plain ItemsList: regular OPL crashes on the first scroll without it." : "The games list has no ItemsList. RiptOPL adds a default one at 42,42; regular OPL crashes on the first scroll. Add one (it can be one row, or hidden=1).", "main");
    if (!m.has("main0_type") && !m.has("main0_enabled")) add("info", "No main0 block: OPL adds a plain Background (plasma) and reads nothing else from main.", "main0");
    return out;
  }

  var API = { parse: parse, serialize: serialize, applyPasses: applyPasses, viewScan: viewScan, synthesize: synthesize, build: build, check: check, blockOf: blockOf, present: present };
  g.RTC_CFG = API;
  if (typeof module !== "undefined") module.exports = API;
})(typeof window !== "undefined" ? window : globalThis);
