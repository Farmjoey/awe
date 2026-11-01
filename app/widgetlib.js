// NOVA RIDE widgets 2.0: JSON <-> compact binary (max ~2 KB) understood by the board, plus a canvas renderer
// that draws the same layout in the app (editor preview). The firmware parser (src/WidgetLayout.h) enforces the
// same rules. Keep both in sync. Old (v1) JSON files still compile: they are upgraded on the way.
(function (root) {
  'use strict';
  var TYPES = { text: 1, num: 2, bar: 3, arc: 4, rect: 6, circle: 7, line: 8, button: 9, segbar: 10, spark: 11, dot: 12, icon: 13 };
  var TYPE_ALIAS = { ring: 'arc', modebar: 'modebar' };
  var SRC = { none: 0, speed: 1, power: 2, battery: 3, volt: 4, current: 5, temp: 6, motortemp: 7, trip: 8, total: 9, mode: 10, fault: 11,
              maxspeed: 12, maxpower: 13, minvolt: 14, energy: 15, ridetime: 16,
              ble: 17, phone: 18, limit: 19, demo: 20, alert: 21, regen: 22, moving: 23 };
  var NUMERIC = function (s) { return (s >= 1 && s <= 9) || (s >= 12 && s <= 16); };
  var STATUS = function (s) { return s >= 17 && s <= 23; };
  var PAL = { text: 0, neon: 1, ok: 2, warn: 3, bad: 4, dim: 5, panel: 6, bg: 7, glow: 8, white: 9, black: 10 };
  var FONTS = { '1': 0, '2': 1, '4': 2, '6': 3, '7': 4, '8': 5 };
  var ALIGN = { left: 0, center: 1, right: 2 };
  var ACTIONS = { none: 0, nextpage: 1, prevpage: 2, walk: 3, eco: 4, drive: 5, sport: 6, prevtrack: 7, playpause: 8, nexttrack: 9, settings: 10, messages: 11, music: 12, safety: 13 };
  var ICONS = { bolt: 0, battery: 1, thermo: 2, gauge: 3, warning: 4, lock: 5, phone: 6, light: 7 };
  var OPS = { none: 0, '>': 1, '<': 2, '==': 3, '!=': 4, '>=': 5, '<=': 6 };
  var F = { fill: 1, round: 2, border: 4, knob: 8, ticks: 16, caps: 32, bidir: 64, rev: 128 };
  var LIM = { items: 48, str: 24, strLen: 16, colors: 16, header: 12, itemLen: 32 };
  var V1FONT = { '2': 'two', '4': 'four' };

  function inv(o) { var r = {}; Object.keys(o).forEach(function (k) { r[o[k]] = k; }); return r; }
  function clampi(v, a, b) { v = Math.round(+v); return v < a ? a : v > b ? b : v; }
  function rgb565(hex) { var r = parseInt(hex.slice(1, 3), 16), g = parseInt(hex.slice(3, 5), 16), b = parseInt(hex.slice(5, 7), 16); return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3); }

  // v1 file (ring/bar/modebar, fixed top margin) -> v2 object
  function upgrade(w) {
    if (!w || w.v === 2) return w;
    var o = { v: 2, name: w.name, author: w.author, desc: w.desc, pages: 1, items: [] };
    (w.items || []).forEach(function (it) {
      var n = JSON.parse(JSON.stringify(it));
      if (n.t === 'ring') { n.t = 'arc'; n.a0 = 135; n.sweep = 270; n.color2 = 'panel'; }
      if (n.t === 'bar') { n.border = true; n.color2 = 'panel'; }
      if (n.t === 'rect') { n.border = true; n.rad = 4; }
      o.items.push(n);
    });
    return o;
  }

  function compile(w0) {
    var err = [];
    function bad(i, m) { err.push((i < 0 ? '' : 'Element ' + (i + 1) + ': ') + m); }
    if (!w0 || typeof w0 !== 'object') return { ok: false, errors: ['Ingen widget'] };
    var w = upgrade(w0);
    var items = w.items;
    if (!Array.isArray(items) || items.length < 1) return { ok: false, errors: ['Widget skal have mindst ét element'] };
    var pages = clampi(w.pages == null ? 1 : w.pages, 1, 4);
    var strs = [], cols = [], out = [];
    function strIdx(s, i, what) {
      s = String(s == null ? '' : s);
      if (!/^[\x20-\x7E]*$/.test(s)) { bad(i, what + ' må kun have almindelige tegn (ASCII)'); return 255; }
      if (s.length === 0) return 255;
      if (s.length > LIM.strLen - 1) { bad(i, what + ' maks ' + (LIM.strLen - 1) + ' tegn'); return 255; }
      var k = strs.indexOf(s);
      if (k < 0) { if (strs.length >= LIM.str) { bad(i, 'For mange forskellige tekster (maks ' + LIM.str + ')'); return 255; } strs.push(s); k = strs.length - 1; }
      return k;
    }
    function colIdx(c, def, i, what) {
      if (c == null) c = def;
      if (typeof c === 'string' && c.charAt(0) === '#') {
        if (!/^#[0-9a-fA-F]{6}$/.test(c)) { bad(i, what + ': farve skal være #rrggbb'); return 0; }
        var v = rgb565(c), k = cols.indexOf(v);
        if (k < 0) { if (cols.length >= LIM.colors) { bad(i, 'For mange egne farver (maks ' + LIM.colors + ')'); return 0; } cols.push(v); k = cols.length - 1; }
        return 16 + k;
      }
      var p = PAL[c];
      if (p === undefined) { bad(i, what + ': ukendt farve'); return 0; }
      return p;
    }
    var bg = colIdx(w.bg, 'bg', -1, 'Baggrund');
    var expanded = [];
    items.forEach(function (it) {
      if (it && it.t === 'modebar') {                       // old mode buttons: four real buttons along the bottom
        ['walk', 'eco', 'drive', 'sport'].forEach(function (m, k) {
          expanded.push({ t: 'button', x: 4 + k * 79, y: 206, w: 75, h: 30, action: m, text: m.toUpperCase(), font: '2', rad: 9, color: 'text', color2: 'panel' });
        });
      } else expanded.push(it);
    });
    if (expanded.length > LIM.items) return { ok: false, errors: ['Højst ' + LIM.items + ' elementer'] };
    expanded.forEach(function (it, i) {
      var tn = TYPE_ALIAS[it.t] || it.t, t = TYPES[tn];
      if (!t) { bad(i, 'Ukendt type'); return; }
      var s = SRC[it.src == null ? 'none' : it.src];
      var action = 0;
      if (t === 9) { action = ACTIONS[it.action == null ? 'none' : it.action]; if (action === undefined) { bad(i, 'Ukendt handling'); return; } s = action; }
      else if (s === undefined) { bad(i, 'Ukendt kilde'); return; }
      var color = colIdx(it.color, 'text', i, 'Farve'), color2 = colIdx(it.color2, 'panel', i, 'Farve 2');
      var font = FONTS[String(it.font == null ? '2' : it.font)];
      if (font === undefined) { bad(i, 'Ukendt skrifttype (1, 2, 4, 6, 7, 8)'); return; }
      var al = ALIGN[it.align == null ? 'left' : it.align];
      if (al === undefined) { bad(i, 'Ukendt justering'); return; }
      var dp = it.dp == null ? 0 : it.dp | 0;
      if (dp < 0 || dp > 2) { bad(i, 'Decimaler 0-2'); return; }
      var pg = it.page == null ? 15 : (it.page === 'all' ? 15 : it.page | 0);
      if (pg !== 15 && (pg < 0 || pg >= pages)) { bad(i, 'Side findes ikke (siderne er 0-' + (pages - 1) + ')'); return; }
      var x = Math.round(+it.x || 0), y = Math.round(+it.y || 0), wd = Math.round(+it.w || 0), ht = Math.round(+it.h || 0);
      var mn = it.min == null ? 0 : Math.round(+it.min), mx = it.max == null ? 0 : Math.round(+it.max);
      var th = Math.round(+it.rad || 0), a0 = Math.round((((+it.a0 == null || isNaN(+it.a0)) ? 135 : +it.a0) % 360 + 360) % 360 / 2), sw = Math.round((it.sweep == null ? 270 : +it.sweep) / 2);
      var flags = 0;
      Object.keys(F).forEach(function (k) { if (it[k]) flags |= F[k]; });
      var str = strIdx(it.t === 'text' || t === 9 ? it.text : it.unit, i, 'Tekst'), str2 = strIdx(it.pre, i, 'Præfiks');
      if (t === 1) {
        if (s !== 0) bad(i, 'Tekst har ingen kilde');
        if (font > 2) bad(i, 'Tekst kan kun bruge skrift 1, 2 eller 4');
        if (str === 255) bad(i, 'Tekst mangler');
      } else if (t === 2) {
        if (s === 0) bad(i, 'Tal skal have en kilde');
        if (s === SRC.mode && font > 2) bad(i, 'Tilstandsnavn kan kun bruge skrift 1, 2 eller 4');
        if (font > 2 && (str !== 255 || str2 !== 255)) bad(i, 'Skrift 6, 7 og 8 kan kun vise tal - brug en tekst ved siden af i stedet for enhed/præfiks');
      } else if (t === 3) {
        if (!NUMERIC(s)) bad(i, 'Bjælke skal have en talkilde');
        if (mx <= mn) bad(i, 'max skal være større end min');
        if (wd < 4 || wd > 320 || ht < 2 || ht > 200) bad(i, 'Bjælke: bredde 4-320, højde 2-200');
        if (th > Math.min(wd, ht) / 2) bad(i, 'Afrunding er for stor');
      } else if (t === 4) {
        var r = Math.round(+it.r || 0), tk = Math.round(+it.th || 0); wd = r; ht = tk;
        if (!NUMERIC(s)) bad(i, 'Bue skal have en talkilde');
        if (mx <= mn) bad(i, 'max skal være større end min');
        if (r < 8 || r > 160 || tk < 1 || tk >= r) bad(i, 'Bue: radius 8-160, tykkelse 1 til under radius');
        if (sw < 1 || sw > 180) bad(i, 'Bue: åbning 2-360 grader');
      } else if (t === 6) {
        if (wd < 2 || wd > 640 || ht < 2 || ht > 640) bad(i, 'Firkant: bredde og højde 2-640');
        if (th > Math.min(wd, ht) / 2) bad(i, 'Afrunding er for stor');
      } else if (t === 7) {
        var cr = Math.round(+it.r || 0); wd = cr; ht = 0;
        if (cr < 2 || cr > 160) bad(i, 'Cirkel: radius 2-160');
        th = Math.round(+it.th || 0);
        if (th > cr) bad(i, 'Kant er tykkere end cirklen');
      } else if (t === 8) {
        th = Math.round(it.th == null ? 2 : +it.th);
        if (th < 1 || th > 12) bad(i, 'Linje: tykkelse 1-12');
        if (wd < 0 || ht < 0 || wd > 640 || ht > 640 || (wd === 0 && ht === 0)) bad(i, 'Linje: længde skal være 1-640');
      } else if (t === 9) {
        if (wd < 16 || wd > 320 || ht < 12 || ht > 200) bad(i, 'Knap: bredde 16-320, højde 12-200');
        if (str === 255) bad(i, 'Knap skal have tekst');
        if (th > Math.min(wd, ht) / 2) bad(i, 'Afrunding er for stor');
        if (font > 2) bad(i, 'Knap kan kun bruge skrift 1, 2 eller 4');
      } else if (t === 10) {
        th = Math.round(+it.n || 0);
        if (!NUMERIC(s)) bad(i, 'Segmenter skal have en talkilde');
        if (mx <= mn) bad(i, 'max skal være større end min');
        if (th < 2 || th > 40) bad(i, 'Segmenter: 2-40');
        if (ht < 2 || ht > 100 || wd < th * 2 || wd > 320) bad(i, 'Segmenter: bredde (mindst 2 pr. segment) og højde 2-100');
      } else if (t === 11) {
        th = Math.round(it.th == null ? 2 : +it.th);
        if (!NUMERIC(s)) bad(i, 'Graf skal have en talkilde');
        if (mx <= mn) bad(i, 'max skal være større end min');
        if (wd < 16 || wd > 320 || ht < 8 || ht > 200) bad(i, 'Graf: bredde 16-320, højde 8-200');
        if (th < 1 || th > 6) bad(i, 'Graf: stregtykkelse 1-6');
      } else if (t === 12) {
        var dr = Math.round(+it.r || 0); wd = dr; ht = 0;
        if (s < 1 || s > 23 || s === SRC.mode) bad(i, 'Prik skal have en kilde');
        if (dr < 2 || dr > 40) bad(i, 'Prik: radius 2-40');
      } else if (t === 13) {
        var ic = ICONS[it.icon == null ? 'bolt' : it.icon]; th = ic;
        wd = Math.round(+it.size || 0); ht = 0;
        if (ic === undefined) bad(i, 'Ukendt ikon');
        if (wd < 8 || wd > 96) bad(i, 'Ikon: størrelse 8-96');
      }
      if (x < -200 || x > 520 || y < -200 || y > 520) bad(i, 'Ligger for langt uden for skærmen');
      // show-only-when rule and colour thresholds
      var showSrc = 0, showOp = 0, showVal = 0, warn = 0, badv = 0;
      if (it.show) {
        showSrc = SRC[it.show.src];
        if (!showSrc || showSrc === SRC.mode) bad(i, 'Vis kun når: ukendt kilde');
        var op = OPS[it.show.op == null ? '>' : it.show.op];
        if (!op) bad(i, 'Vis kun når: ukendt sammenligning'); else showOp = op;
        showVal = clampi(it.show.val || 0, -32768, 32767);
      }
      if (it.warn != null || it.bad != null) {
        showOp |= 8; if (it.below) showOp |= 16;
        warn = clampi(it.warn == null ? (it.below ? -32768 : 32767) : it.warn, -32768, 32767);
        badv = clampi(it.bad == null ? (it.below ? -32768 : 32767) : it.bad, -32768, 32767);
      }
      out.push({ t: t, s: s, color: color, color2: color2, font: font, al: al, dp: dp, pg: pg, str: str, str2: str2, max: mx, min: mn, x: x, y: y, w: wd, h: ht,
                 flags: flags, a0: a0, sw: sw, th: th, showSrc: showSrc, showOp: showOp, showVal: showVal, warn: warn, bad: badv });
    });
    if (err.length) return { ok: false, errors: err };
    var n = out.length, total = LIM.header + strs.length * LIM.strLen + cols.length * 2 + n * LIM.itemLen, b = new Uint8Array(total);
    b[0] = 0x4E; b[1] = 0x57; b[2] = 2; b[3] = n; b[4] = strs.length; b[5] = cols.length; b[6] = total & 255; b[7] = total >> 8; b[8] = pages; b[9] = bg; b[10] = w.dots === false ? 0 : 1; b[11] = 0;
    strs.forEach(function (s, i) { for (var k = 0; k < s.length; k++) b[LIM.header + i * LIM.strLen + k] = s.charCodeAt(k); });
    var co = LIM.header + strs.length * LIM.strLen;
    cols.forEach(function (c, i) { b[co + i * 2] = c & 255; b[co + i * 2 + 1] = c >> 8; });
    var o = co + cols.length * 2;
    function p16(p, v) { b[p] = v & 255; b[p + 1] = (v >> 8) & 255; }
    out.forEach(function (it, i) {
      var p = o + i * LIM.itemLen;
      b[p] = it.t; b[p + 1] = it.s; b[p + 2] = it.color; b[p + 3] = it.color2; b[p + 4] = it.font; b[p + 5] = it.al | (it.dp << 2) | (it.pg << 4); b[p + 6] = it.str; b[p + 7] = it.str2;
      p16(p + 8, it.max); p16(p + 10, it.min); p16(p + 12, it.x); p16(p + 14, it.y); p16(p + 16, it.w); p16(p + 18, it.h);
      b[p + 20] = it.flags; b[p + 21] = it.a0; b[p + 22] = it.sw; b[p + 23] = it.th; b[p + 24] = it.showSrc; b[p + 25] = it.showOp; p16(p + 26, it.showVal); p16(p + 28, it.warn); p16(p + 30, it.bad);
    });
    return { ok: true, bytes: b, errors: [], pages: pages };
  }

  function validateFile(w) {
    var e = [];
    if (!w || typeof w !== 'object') return ['Ikke et JSON-objekt'];
    if (w.v !== 1 && w.v !== 2) e.push('v skal være 1 eller 2');
    if (typeof w.name !== 'string' || !/^[\x20-\x7E]{2,24}$/.test(w.name)) e.push('name: 2-24 almindelige tegn');
    if (typeof w.author !== 'string' || !/^[\x20-\x7E]{1,24}$/.test(w.author)) e.push('author: 1-24 almindelige tegn');
    if (w.desc != null && (typeof w.desc !== 'string' || w.desc.length > 120)) e.push('desc: højst 120 tegn');
    var c = compile(w);
    if (!c.ok) e = e.concat(c.errors);
    return e;
  }

  // ------------------------------------------------------------------ renderer (canvas) -- mirrors src/WidgetDraw.cpp
  var THEME = { text: '#ececec', neon: '#6fa3a8', ok: '#8fd9a8', warn: '#ffb000', bad: '#ff5a6e', dim: '#9a9fa2', panel: '#3b3b3b', bg: '#303030', glow: '#4c6e73', white: '#ffffff', black: '#000000' };
  var FH = { 0: 8, 1: 16, 2: 26, 3: 48, 4: 48, 5: 75 };       // glyph box height of the board's fonts 1, 2, 4, 6, 7, 8
  function mixc(a, b, t) {
    function p(h) { return [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)]; }
    var x = p(a), y = p(b), r = [0, 1, 2].map(function (i) { return Math.round(x[i] + (y[i] - x[i]) * t); });
    return '#' + r.map(function (v) { return ('0' + v.toString(16)).slice(-2); }).join('');
  }
  function colorOf(c, theme) {
    if (typeof c === 'string' && c.charAt(0) === '#') return c;
    return theme[c == null ? 'text' : c] || theme.text;
  }
  function valueOf(src, L) {                                    // L: {speed:{v,ok}...} or flat values; returns null when not available
    var m = { speed: 'speed', power: 'power', battery: 'battery', volt: 'volt', current: 'current', temp: 'temp', motortemp: 'motortemp', trip: 'trip', total: 'total', fault: 'fault',
              maxspeed: 'maxspeed', maxpower: 'maxpower', minvolt: 'minvolt', energy: 'energy', ridetime: 'ridetime' };
    if (src === 'mode') return L.mode == null ? null : L.mode;
    if (m[src]) { var v = L[m[src]]; return v == null ? null : v; }
    if (src === 'ble') return L.ble ? 1 : 0;
    if (src === 'phone') return L.phone ? 1 : 0;
    if (src === 'limit') return L.limit ? 1 : 0;
    if (src === 'demo') return L.demo ? 1 : 0;
    if (src === 'alert') return L.fault ? 1 : 0;
    if (src === 'regen') return L.power != null && L.power < 0 ? 1 : 0;
    if (src === 'moving') return L.speed != null && L.speed > 1 ? 1 : 0;
    return null;
  }
  function fmt(v, dp) { return v == null ? '--' : (dp ? v.toFixed(dp) : String(Math.round(v))); }
  function isVisible(it, L, page) {
    var pg = it.page == null || it.page === 'all' ? 15 : it.page;
    if (pg !== 15 && pg !== page) return false;
    if (!it.show) return true;
    var v = valueOf(it.show.src, L); if (v == null) return false;
    var c = it.show.val || 0;
    switch (it.show.op == null ? '>' : it.show.op) { case '>': return v > c; case '<': return v < c; case '==': return v === c; case '!=': return v !== c; case '>=': return v >= c; case '<=': return v <= c; }
    return true;
  }
  function ruleColor(it, v, theme, base) {
    if (v == null || (it.warn == null && it.bad == null)) return base;
    var below = !!it.below;
    if (it.bad != null && (below ? v <= it.bad : v >= it.bad)) return theme.bad;
    if (it.warn != null && (below ? v <= it.warn : v >= it.warn)) return theme.warn;
    return base;
  }
  function frac(it, v) { if (v == null) return 0; var mn = it.min || 0, mx = it.max || 1; var f = (v - mn) / (mx - mn); return f < 0 ? 0 : f > 1 ? 1 : f; }
  function rr(ctx, x, y, w, h, r) { r = Math.max(0, Math.min(r, w / 2, h / 2)); ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r); ctx.arcTo(x + w, y + h, x, y + h, r); ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath(); }
  function text(ctx, s, x, y, font, align, color) {
    var h = FH[font] || 16, fam = font === 4 ? 'ui-monospace,Menlo,monospace' : '"Roboto Condensed","Arial Narrow",system-ui,sans-serif';
    ctx.font = (font === 0 ? '' : 'bold ') + Math.round(h * (font === 0 ? 1.0 : 0.82)) + 'px ' + fam;
    ctx.textBaseline = 'top'; ctx.textAlign = align === 'center' ? 'center' : align === 'right' ? 'right' : 'left'; ctx.fillStyle = color;
    ctx.fillText(s, x, y + h * 0.08);
  }
  function arcPath(ctx, cx, cy, ro, ri, a0, a1, caps) {
    var rm = (ro + ri) / 2, tk = ro - ri, A = a0 * Math.PI / 180, B = a1 * Math.PI / 180;
    ctx.beginPath(); ctx.arc(cx, cy, rm, A, B, false); ctx.lineWidth = tk; ctx.lineCap = caps ? 'round' : 'butt'; ctx.stroke();
  }
  function drawIcon(ctx, id, x, y, s, col, frc, theme) {
    ctx.save(); ctx.translate(x, y); ctx.strokeStyle = col; ctx.fillStyle = col; ctx.lineWidth = Math.max(1.5, s / 12); ctx.lineJoin = 'round'; ctx.lineCap = 'round';
    var u = s / 24;
    if (id === 0) { ctx.beginPath(); ctx.moveTo(14 * u, 1 * u); ctx.lineTo(5 * u, 14 * u); ctx.lineTo(11 * u, 14 * u); ctx.lineTo(9 * u, 23 * u); ctx.lineTo(19 * u, 9 * u); ctx.lineTo(13 * u, 9 * u); ctx.closePath(); ctx.fill(); }
    else if (id === 1) { rr(ctx, 1 * u, 6 * u, 19 * u, 12 * u, 3 * u); ctx.stroke(); ctx.fillRect(21 * u, 10 * u, 2.5 * u, 4 * u); ctx.fillRect(3.5 * u, 8.5 * u, 14 * u * (frc == null ? 1 : frc), 7 * u); }
    else if (id === 2) { rr(ctx, 9 * u, 1 * u, 6 * u, 15 * u, 3 * u); ctx.stroke(); ctx.beginPath(); ctx.arc(12 * u, 19 * u, 4.5 * u, 0, 7); ctx.fill(); ctx.fillRect(11 * u, 8 * u, 2 * u, 11 * u); }
    else if (id === 3) { ctx.beginPath(); ctx.arc(12 * u, 14 * u, 10 * u, Math.PI * 0.85, Math.PI * 2.15); ctx.stroke(); ctx.beginPath(); ctx.moveTo(12 * u, 14 * u); ctx.lineTo(18 * u, 7 * u); ctx.stroke(); }
    else if (id === 4) { ctx.beginPath(); ctx.moveTo(12 * u, 2 * u); ctx.lineTo(23 * u, 21 * u); ctx.lineTo(1 * u, 21 * u); ctx.closePath(); ctx.stroke(); ctx.fillRect(11 * u, 8 * u, 2 * u, 7 * u); ctx.fillRect(11 * u, 17 * u, 2 * u, 2 * u); }
    else if (id === 5) { rr(ctx, 4 * u, 11 * u, 16 * u, 11 * u, 2 * u); ctx.fill(); ctx.beginPath(); ctx.arc(12 * u, 10 * u, 5 * u, Math.PI, 0); ctx.stroke(); }
    else if (id === 6) { rr(ctx, 6 * u, 1 * u, 12 * u, 22 * u, 3 * u); ctx.stroke(); ctx.fillRect(10 * u, 19 * u, 4 * u, 1.5 * u); }
    else { ctx.beginPath(); ctx.arc(12 * u, 12 * u, 5 * u, 0, 7); ctx.fill(); for (var k = 0; k < 8; k++) { var a = k * Math.PI / 4; ctx.beginPath(); ctx.moveTo(12 * u + Math.cos(a) * 8 * u, 12 * u + Math.sin(a) * 8 * u); ctx.lineTo(12 * u + Math.cos(a) * 11 * u, 12 * u + Math.sin(a) * 11 * u); ctx.stroke(); } }
    ctx.restore();
  }

  // Draws one item. L = live values; opts: {theme, page, hist:{src:[...]}, mode:0..3, selected}
  function drawItem(ctx, it, L, opts) {
    var th = opts.theme, t = it.t, src = it.src, v = (t === 'button' || t === 'icon' && !src) ? null : valueOf(src, L), col = colorOf(it.color, th), col2 = colorOf(it.color2 == null ? 'panel' : it.color2, th);
    if (v != null && !STATUS(SRC[src] || 0)) col = ruleColor(it, v, th, col);
    var x = it.x || 0, y = it.y || 0, w = it.w || 0, h = it.h || 0, rad = it.rad || 0;
    ctx.save();
    if (t === 'text') text(ctx, it.text || '', x, y, FONTS[String(it.font || '2')], it.align, col);
    else if (t === 'num') {
      var s;
      if (src === 'mode') s = L.mode == null ? '--' : ['WALK', 'ECO', 'DRIVE', 'SPORT'][L.mode] || '--';
      else if (STATUS(SRC[src])) s = v ? 'ON' : 'OFF';
      else s = fmt(v, it.dp || 0);
      var ft = FONTS[String(it.font || '2')]; if (v == null && src !== 'mode') { col = th.dim; if (ft > 2) ft = 2; }
      text(ctx, (it.pre || '') + s + (v != null || src === 'mode' ? (it.unit || '') : ''), x, y, ft, it.align, col);
    } else if (t === 'bar') {
      var f = frac(it, v), vert = h > w, r = it.round ? Math.max(rad, 0) : rad;
      rr(ctx, x, y, w, h, r); ctx.fillStyle = col2; ctx.fill();
      if (it.border) { ctx.strokeStyle = mixc(th.bg, th.text, 0.5); ctx.lineWidth = 1; rr(ctx, x + .5, y + .5, w - 1, h - 1, r); ctx.stroke(); }
      var ix = it.border ? 2 : 0, fw = (vert ? h : w) - ix * 2, a = 0, b = f;
      if (it.bidir) { var z = (-(it.min || 0)) / ((it.max || 1) - (it.min || 0)); var vv = v == null ? 0 : (v - (it.min || 0)) / ((it.max || 1) - (it.min || 0)); a = Math.min(z, vv); b = Math.max(z, vv); }
      else if (it.rev) { a = 1 - f; b = 1; } else { a = 0; b = f; }
      ctx.fillStyle = col;
      if (b - a > 0.002) {
        if (vert) rr(ctx, x + ix, y + ix + fw * (1 - b), w - ix * 2, Math.max(1, fw * (b - a)), Math.max(0, r - ix));
        else rr(ctx, x + ix + fw * a, y + ix, Math.max(1, fw * (b - a)), h - ix * 2, Math.max(0, r - ix));
        ctx.fill();
      }
    } else if (t === 'arc') {
      var r2 = it.r, tk = it.th, a0 = it.a0 == null ? 135 : it.a0, sw = it.sweep == null ? 270 : it.sweep, fr = frac(it, v);
      ctx.strokeStyle = col2; arcPath(ctx, x, y, r2, r2 - tk, a0, a0 + sw, !!it.caps);
      if (fr > 0.003) { ctx.strokeStyle = col; var e = it.rev ? a0 + sw - sw * fr : a0 + sw * fr; if (it.rev) arcPath(ctx, x, y, r2, r2 - tk, e, a0 + sw, !!it.caps); else arcPath(ctx, x, y, r2, r2 - tk, a0, e, !!it.caps); }
      if (it.ticks) { ctx.strokeStyle = mixc(th.bg, th.text, 0.55); ctx.lineWidth = 1.5; for (var k = 0; k <= 10; k++) { var q = (a0 + sw * k / 10) * Math.PI / 180; ctx.beginPath(); ctx.moveTo(x + Math.cos(q) * (r2 + 3), y + Math.sin(q) * (r2 + 3)); ctx.lineTo(x + Math.cos(q) * (r2 + 8), y + Math.sin(q) * (r2 + 8)); ctx.stroke(); } }
      if (it.knob) { var qa = (it.rev ? a0 + sw - sw * fr : a0 + sw * fr) * Math.PI / 180, rm = r2 - tk / 2; ctx.fillStyle = th.text; ctx.beginPath(); ctx.arc(x + Math.cos(qa) * rm, y + Math.sin(qa) * rm, Math.max(4, tk * 0.8), 0, 7); ctx.fill(); }
    } else if (t === 'rect') {
      rr(ctx, x, y, w, h, rad);
      if (it.fill || !it.border) { ctx.fillStyle = it.fill ? col : col2; ctx.fill(); }
      if (it.border) { ctx.strokeStyle = col; ctx.lineWidth = 2; rr(ctx, x + 1, y + 1, w - 2, h - 2, Math.max(0, rad - 1)); ctx.stroke(); }
    } else if (t === 'circle') {
      var cr = it.r || 0; ctx.beginPath(); ctx.arc(x, y, cr, 0, 7);
      if (it.fill || !it.border) { ctx.fillStyle = col; ctx.fill(); }
      if (it.border) { ctx.strokeStyle = col; ctx.lineWidth = Math.max(1, it.th || 2); ctx.beginPath(); ctx.arc(x, y, Math.max(1, cr - (it.th || 2) / 2), 0, 7); ctx.stroke(); }
    } else if (t === 'line') {
      ctx.strokeStyle = col; ctx.lineWidth = it.th == null ? 2 : it.th; ctx.lineCap = 'round'; ctx.beginPath();
      if (it.rev) { ctx.moveTo(x, y + h); ctx.lineTo(x + w, y); } else { ctx.moveTo(x, y); ctx.lineTo(x + w, y + h); } ctx.stroke();
    } else if (t === 'button') {
      var act = it.action || 'none', M = { walk: 0, eco: 1, drive: 2, sport: 3 }, sel = M[act] != null && opts.mode === M[act];
      rr(ctx, x, y, w, h, rad || 9); ctx.fillStyle = sel ? colorOf(it.color2 == null ? 'neon' : it.color2, th) : 'rgba(0,0,0,0)'; if (sel || it.fill) ctx.fill();
      ctx.strokeStyle = col; ctx.lineWidth = 2; rr(ctx, x + 1, y + 1, w - 2, h - 2, Math.max(0, (rad || 9) - 1)); ctx.stroke();
      var bf = FONTS[String(it.font || '2')]; text(ctx, it.text || '', x + w / 2, y + h / 2 - (FH[bf] || 16) / 2, bf, 'center', sel ? th.bg : col);
    } else if (t === 'segbar') {
      var n = it.n || 10, gap = Math.max(1, Math.round(w / n * 0.18)), sw2 = (w - gap * (n - 1)) / n, lit = Math.round(frac(it, v) * n);
      for (var j = 0; j < n; j++) { ctx.fillStyle = j < lit ? col : col2; rr(ctx, x + j * (sw2 + gap), y, sw2, h, it.round ? Math.min(3, sw2 / 2) : 0); ctx.fill(); }
    } else if (t === 'spark') {
      var hs = (opts.hist && opts.hist[src]) || [], tkn = it.th == null ? 2 : it.th; ctx.strokeStyle = col2; ctx.lineWidth = 1; ctx.strokeRect(x + .5, y + .5, w - 1, h - 1);
      if (hs.length > 1) {
        ctx.beginPath(); hs.forEach(function (q, k) { var px = x + (w - 1) * k / (hs.length - 1), py = y + h - 1 - (h - 2) * frac(it, q); if (k) ctx.lineTo(px, py); else ctx.moveTo(px, py); });
        if (it.fill) { ctx.lineTo(x + w - 1, y + h - 1); ctx.lineTo(x, y + h - 1); ctx.closePath(); ctx.fillStyle = mixc(th.bg, col, 0.35); ctx.fill(); ctx.beginPath(); hs.forEach(function (q, k) { var px = x + (w - 1) * k / (hs.length - 1), py = y + h - 1 - (h - 2) * frac(it, q); if (k) ctx.lineTo(px, py); else ctx.moveTo(px, py); }); }
        ctx.strokeStyle = col; ctx.lineWidth = tkn; ctx.lineJoin = 'round'; ctx.stroke();
      }
    } else if (t === 'dot') {
      var on = v != null && v > 0; ctx.fillStyle = on ? col : col2; ctx.beginPath(); ctx.arc(x, y, it.r || 4, 0, 7); ctx.fill();
    } else if (t === 'icon') {
      drawIcon(ctx, ICONS[it.icon == null ? 'bolt' : it.icon] || 0, x, y, it.size || 24, col, it.icon === 'battery' && src ? frac(it, v) : 1, th);
    }
    ctx.restore();
  }

  function render(ctx, w0, L, opts) {
    opts = opts || {}; var w = upgrade(w0), th = opts.theme || THEME, page = opts.page || 0;
    var bgc = colorOf(w.bg == null ? 'bg' : w.bg, th);
    ctx.save(); ctx.fillStyle = bgc; ctx.fillRect(0, 0, 320, 240);
    var list = [];
    (w.items || []).forEach(function (it) {
      if (it.t === 'modebar') ['walk', 'eco', 'drive', 'sport'].forEach(function (m, k) { list.push({ t: 'button', x: 4 + k * 79, y: 206, w: 75, h: 30, action: m, text: m.toUpperCase(), font: '2', rad: 9, color: 'text', color2: 'neon' }); });
      else list.push(it);
    });
    list.forEach(function (it, i) { var pgv = it.page == null || it.page === 'all' ? 15 : it.page; if (opts.ignoreShow ? (pgv === 15 || pgv === page) : isVisible(it, L, page)) drawItem(ctx, it, L, { theme: th, hist: opts.hist, mode: opts.mode == null ? L.modeIdx : opts.mode }); });
    ctx.restore();
    return list;
  }

  // box that surrounds an item in the 320x240 picture (editor selection / hit test)
  function bounds(it, ctx) {
    var t = it.t, x = it.x || 0, y = it.y || 0, w = it.w || 0, h = it.h || 0;
    if (t === 'arc') return { x: x - it.r, y: y - it.r, w: it.r * 2, h: it.r * 2 };
    if (t === 'circle' || t === 'dot') { var r = it.r || 4; return { x: x - r, y: y - r, w: r * 2, h: r * 2 }; }
    if (t === 'icon') { var s = it.size || 24; return { x: x, y: y, w: s, h: s }; }
    if (t === 'line') return { x: x, y: y, w: Math.max(w, 6), h: Math.max(h, 6) };
    if (t === 'text' || t === 'num') {
      var fh = FH[FONTS[String(it.font || '2')]] || 16, tw = 0;
      if (ctx) { text(ctx, '', 0, 0, FONTS[String(it.font || '2')], 'left', '#000'); var sample = t === 'text' ? (it.text || '') : ((it.pre || '') + '0000' + (it.unit || '')); var mt = ctx.measureText ? ctx.measureText(sample) : null; tw = mt && mt.width ? mt.width : fh * 0.55 * Math.max(3, sample.length); }
      else tw = fh * 0.55 * Math.max(3, (t === 'text' ? (it.text || '').length : 6));
      var bx = it.align === 'center' ? x - tw / 2 : it.align === 'right' ? x - tw : x;
      return { x: bx, y: y, w: Math.max(tw, 10), h: fh };
    }
    return { x: x, y: y, w: Math.max(w, 6), h: Math.max(h, 6) };
  }

  // animated sample data so layouts can be previewed without a scooter
  function sample(t, opts) {
    opts = opts || {};
    var sp = opts.still ? 1 : 17 + 14 * Math.sin(t * 0.5), pw = opts.still ? 1890 : Math.round(sp * 62 + 140 * Math.sin(t * 1.3)), pct = opts.still ? 60 : Math.max(8, Math.round(86 - t * 0.2) % 100), V = opts.still ? 78.7 : 67.5 + pct * 0.19;
    return { speed: sp, power: pw, battery: pct, volt: V, current: opts.still ? 22 : pw / V, temp: 40 + sp * 0.5, motortemp: 52 + sp * 0.6, trip: 12.4 + t * 0.01, total: 20660, fault: 0,
             maxspeed: Math.max(sp, 31.4), maxpower: Math.max(pw, 2650), minvolt: 71.2, energy: 148 + t * 0.4, ridetime: 18 + Math.floor(t / 60), mode: 2, modeIdx: 2, ble: true, phone: Math.sin(t * 0.2) > -0.3, limit: !!opts.limit, demo: true };
  }
  function history(L, n) { var h = {}; ['speed', 'power', 'battery', 'volt', 'current', 'temp', 'motortemp'].forEach(function (k) { h[k] = []; for (var i = 0; i < n; i++) { var s = sample(i * 0.7 + L._t0 || 0); h[k].push(s[k]); } }); return h; }

  var api = { compile: compile, validateFile: validateFile, upgrade: upgrade, render: render, drawItem: drawItem, bounds: bounds, sample: sample, history: history, isVisible: isVisible, mixc: mixc,
              TYPES: TYPES, SRC: SRC, PAL: PAL, FONTS: FONTS, ALIGN: ALIGN, ACTIONS: ACTIONS, ICONS: ICONS, OPS: OPS, FLAGS: F, LIM: LIM, THEME: THEME,
              SRC_NAMES: inv(SRC), TYPE_NAMES: inv(TYPES), COLOR_NAMES: inv(PAL), NUMERIC: NUMERIC, STATUS: STATUS, MAX_ITEMS: LIM.items };
  root.NovaWidget = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
