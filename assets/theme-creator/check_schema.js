#!/usr/bin/env node
/* Re-checks schema.js against a RiptOPL checkout: every global key, element property and element type the
 * creator offers must appear as a string in the theme parser's source. Run from anywhere:
 *   node assets/theme-creator/check_schema.js /path/to/RiptOPL
 * Exit code 1 lists the names the source no longer has (renamed or removed), so the page stops offering them.
 * The page itself never runs this file. */
"use strict";
var fs = require("fs"), path = require("path");
var root = process.argv[2];
if (!root) { console.error("usage: node check_schema.js <RiptOPL checkout>"); process.exit(2); }
global.window = global;
require(path.join(__dirname, "schema.js"));
var S = global.RTC_SCHEMA;

var dirs = ["src/gfx", "src/core", "src/ui", "include"].map(function (d) { return path.join(root, d); });
var src = "";
dirs.forEach(function (d) {
  if (!fs.existsSync(d)) return;
  fs.readdirSync(d).forEach(function (f) { if (/\.(c|cpp|h)$/.test(f)) src += fs.readFileSync(path.join(d, f), "utf8") + "\n"; });
});
if (!src) { console.error("No RiptOPL sources under " + root); process.exit(2); }

// A key built at run time (fontN, <view>.*) is checked by its fixed part.
function inSource(name) {
  var base = name.replace(/^font\d+/, "font").replace(/^(list|coverflow|psbbn|grid)\./, "");
  return src.indexOf('"' + name + '"') >= 0 || src.indexOf('"' + base + '"') >= 0 || src.indexOf('"' + base.replace(/^font/, "font%d")) >= 0 ||
         src.indexOf('"_' + base + '"') >= 0 || src.indexOf('"' + base + '%') >= 0 || src.indexOf('_' + base + '"') >= 0;
}
var missing = [];
Object.keys(S.GLOBALS).forEach(function (k) { if (!inSource(k)) missing.push("global " + k); });
Object.keys(S.PROPS).forEach(function (k) { if (!inSource(k)) missing.push("property " + k); });
S.TYPE_NAMES.forEach(function (t) { if (!inSource(t)) missing.push("type " + t); });
var n = Object.keys(S.GLOBALS).length + Object.keys(S.PROPS).length + S.TYPE_NAMES.length;
if (missing.length) { console.log(missing.length + " of " + n + " names not found in the source:\n  " + missing.join("\n  ")); process.exit(1); }
console.log("All " + n + " globals, properties and types are in the source.");
