/* RiptOPL Theme Creator: zip and tar, written and read in the browser. Nothing is uploaded anywhere. */
(function (g) {
  "use strict";
  var enc = new TextEncoder();

  var CRC = (function () { var t = new Uint32Array(256); for (var n = 0; n < 256; n++) { var c = n; for (var k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1; t[n] = c >>> 0; } return t; })();
  function crc32(u8) { var c = 0xFFFFFFFF; for (var i = 0; i < u8.length; i++) c = CRC[(c ^ u8[i]) & 0xFF] ^ (c >>> 8); return (c ^ 0xFFFFFFFF) >>> 0; }

  // files: [{ path, data: Uint8Array }]. Stored (no compression): PNGs are already compressed.
  function zip(files) {
    var parts = [], central = [], offset = 0;
    var d = new Date(), dosTime = (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1), dosDate = ((d.getFullYear() - 1980) << 9) | ((d.getMonth() + 1) << 5) | d.getDate();
    files.forEach(function (f) {
      var name = enc.encode(f.path), data = f.data, crc = crc32(data);
      var h = new DataView(new ArrayBuffer(30));
      h.setUint32(0, 0x04034b50, true); h.setUint16(4, 20, true); h.setUint16(6, 0x0800, true); h.setUint16(8, 0, true);
      h.setUint16(10, dosTime, true); h.setUint16(12, dosDate, true); h.setUint32(14, crc, true);
      h.setUint32(18, data.length, true); h.setUint32(22, data.length, true); h.setUint16(26, name.length, true); h.setUint16(28, 0, true);
      parts.push(new Uint8Array(h.buffer), name, data);
      var c = new DataView(new ArrayBuffer(46));
      c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true); c.setUint16(8, 0x0800, true); c.setUint16(10, 0, true);
      c.setUint16(12, dosTime, true); c.setUint16(14, dosDate, true); c.setUint32(16, crc, true); c.setUint32(20, data.length, true); c.setUint32(24, data.length, true);
      c.setUint16(28, name.length, true); c.setUint32(42, offset, true);
      central.push(new Uint8Array(c.buffer), name);
      offset += 30 + name.length + data.length;
    });
    var csize = central.reduce(function (s, p) { return s + p.length; }, 0);
    var e = new DataView(new ArrayBuffer(22));
    e.setUint32(0, 0x06054b50, true); e.setUint16(8, files.length, true); e.setUint16(10, files.length, true);
    e.setUint32(12, csize, true); e.setUint32(16, offset, true);
    return new Blob(parts.concat(central, [new Uint8Array(e.buffer)]), { type: "application/zip" });
  }

  async function unzip(buf) {
    var u8 = new Uint8Array(buf), dv = new DataView(buf), out = [];
    var eocd = -1;
    for (var i = u8.length - 22; i >= Math.max(0, u8.length - 65557); i--) if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
    if (eocd < 0) throw new Error("This is not a zip file.");
    var count = dv.getUint16(eocd + 10, true), p = dv.getUint32(eocd + 16, true);
    for (var n = 0; n < count; n++) {
      if (dv.getUint32(p, true) !== 0x02014b50) throw new Error("The zip's file list is damaged.");
      var method = dv.getUint16(p + 10, true), csize = dv.getUint32(p + 20, true), nlen = dv.getUint16(p + 28, true),
          xlen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true), lho = dv.getUint32(p + 42, true);
      var name = new TextDecoder().decode(u8.subarray(p + 46, p + 46 + nlen));
      p += 46 + nlen + xlen + clen;
      if (/\/$/.test(name)) continue;
      var start = lho + 30 + dv.getUint16(lho + 26, true) + dv.getUint16(lho + 28, true);
      var raw = u8.subarray(start, start + csize), data;
      if (method === 0) data = raw.slice();
      else if (method === 8) {
        if (typeof DecompressionStream === "undefined") throw new Error("This browser cannot unpack compressed zips. Unzip it and use Open folder instead.");
        var ds = new Blob([raw]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
        data = new Uint8Array(await new Response(ds).arrayBuffer());
      } else throw new Error("\"" + name + "\" uses a zip compression this page cannot read. Unzip it and use Open folder instead.");
      out.push({ path: name, data: data });
    }
    return out;
  }

  // ustar, the format RiptOPL's tar.cpp reads for THM/thm_<Name>.tar (uncompressed).
  function tar(files) {
    var parts = [];
    files.forEach(function (f) {
      var h = new Uint8Array(512), name = enc.encode(f.path);
      if (name.length > 100) throw new Error("\"" + f.path + "\" is too long a name for a tar.");
      h.set(name, 0);
      function oct(v, off, len) { var s = v.toString(8); while (s.length < len - 1) s = "0" + s; h.set(enc.encode(s), off); }
      oct(420, 100, 8); oct(0, 108, 8); oct(0, 116, 8); oct(f.data.length, 124, 12); oct(Math.floor(Date.now() / 1000), 136, 12);
      for (var i = 148; i < 156; i++) h[i] = 32;
      h[156] = 48; h.set(enc.encode("ustar\u000000"), 257);
      var sum = 0; for (var j = 0; j < 512; j++) sum += h[j];
      var cs = sum.toString(8); while (cs.length < 6) cs = "0" + cs; h.set(enc.encode(cs + "\u0000 "), 148);
      parts.push(h, f.data);
      var pad = (512 - (f.data.length % 512)) % 512; if (pad) parts.push(new Uint8Array(pad));
    });
    parts.push(new Uint8Array(1024));
    return new Blob(parts, { type: "application/x-tar" });
  }
  function untar(buf) {
    var u8 = new Uint8Array(buf), out = [], p = 0, dec = new TextDecoder();
    while (p + 512 <= u8.length) {
      var h = u8.subarray(p, p + 512);
      if (h.every(function (b) { return b === 0; })) break;
      var name = dec.decode(h.subarray(0, 100)).replace(/\0.*$/, "");
      var prefix = dec.decode(h.subarray(345, 500)).replace(/\0.*$/, "");
      if (prefix && dec.decode(h.subarray(257, 262)) === "ustar") name = prefix + "/" + name;
      var size = parseInt(dec.decode(h.subarray(124, 136)).replace(/[^0-7]/g, "") || "0", 8), type = h[156];
      p += 512;
      if (type === 0 || type === 48) out.push({ path: name, data: u8.slice(p, p + size) });
      p += Math.ceil(size / 512) * 512;
    }
    return out;
  }

  function download(blob, name) {
    var a = document.createElement("a"); a.href = URL.createObjectURL(blob); a.download = name;
    document.body.appendChild(a); a.click(); setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
  }

  g.RTC_IO = { zip: zip, unzip: unzip, tar: tar, untar: untar, download: download, crc32: crc32 };
})(window);
