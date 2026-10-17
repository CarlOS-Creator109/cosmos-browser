/* Cosmos Browser - prelude.js
 * API web escrita en JavaScript sobre las primitivas nativas (__Node, __create, __fetch...). */
(function (g) {
'use strict';
var P = __Node.prototype;
var T0 = __now();
var VW = __vw, VH = __vh;
var HAS = function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); };
function log(l, a) { try { __log(l, a.map(function (x) { try { return typeof x === 'string' ? x : (x && x.stack) ? (x.name + ': ' + x.message + '\n' + String(x.stack)) : JSON.stringify(x); } catch (e) { return String(x); } }).join(' ')); } catch (e) {} }
function defn(o, k, v) { Object.defineProperty(o, k, { value: v, writable: true, configurable: true, enumerable: false }); }
function acc(o, k, get, set) { Object.defineProperty(o, k, { get: get, set: set, configurable: true, enumerable: false }); }

/* ---------- console ---------- */
var counts = {}, timers_ = {};
g.console = {
  log: function () { log('log', [].slice.call(arguments)); }, info: function () { log('info', [].slice.call(arguments)); },
  warn: function () { log('warn', [].slice.call(arguments)); }, error: function () { log('error', [].slice.call(arguments)); },
  debug: function () { log('debug', [].slice.call(arguments)); }, trace: function () {}, dir: function () {}, table: function () {},
  group: function () {}, groupEnd: function () {}, groupCollapsed: function () {}, clear: function () {},
  assert: function (c) { if (!c) log('assert', [].slice.call(arguments, 1)); },
  count: function (l) { l = l || 'default'; counts[l] = (counts[l] || 0) + 1; },
  time: function (l) { timers_[l || 'default'] = __now(); }, timeEnd: function () {}, timeLog: function () {}
};

/* ---------- eventos ---------- */
function Event(type, init) {
  init = init || {};
  this.type = String(type); this.bubbles = !!init.bubbles; this.cancelable = !!init.cancelable; this.composed = !!init.composed;
  this.defaultPrevented = false; this.target = null; this.currentTarget = null; this.srcElement = null; this.eventPhase = 0;
  this.timeStamp = __now() - T0; this.isTrusted = false;
  defn(this, '__stop', false); defn(this, '__imm', false); defn(this, '__path', null);
}
Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;
Event.prototype.preventDefault = function () { if (this.cancelable) this.defaultPrevented = true; };
Event.prototype.stopPropagation = function () { this.__stop = true; };
Event.prototype.stopImmediatePropagation = function () { this.__stop = true; this.__imm = true; };
Event.prototype.composedPath = function () { return this.__path ? this.__path.slice() : []; };
Event.prototype.initEvent = function (t, b, c) { this.type = t; this.bubbles = !!b; this.cancelable = !!c; };
acc(Event.prototype, 'returnValue', function () { return !this.defaultPrevented; }, function (v) { if (!v) this.preventDefault(); });
acc(Event.prototype, 'cancelBubble', function () { return this.__stop; }, function (v) { if (v) this.__stop = true; });
g.Event = Event;
function sub(name, defs) {
  var C = function (type, init) {
    init = init || {};
    Event.call(this, type, init);
    for (var k in defs) this[k] = (init[k] !== undefined) ? init[k] : defs[k];
  };
  C.prototype = Object.create(Event.prototype); C.prototype.constructor = C;
  Object.defineProperty(C, 'name', { value: name });
  g[name] = C; return C;
}
var MOUSE = { detail: 0, view: null, screenX: 0, screenY: 0, clientX: 0, clientY: 0, pageX: 0, pageY: 0, offsetX: 0, offsetY: 0, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, button: 0, buttons: 0, relatedTarget: null };
sub('CustomEvent', { detail: null });
sub('UIEvent', { detail: 0, view: null });
sub('MouseEvent', MOUSE);
sub('PointerEvent', Object.assign({ pointerId: 1, pointerType: 'mouse', isPrimary: true, width: 1, height: 1, pressure: 0 }, MOUSE));
sub('WheelEvent', Object.assign({ deltaX: 0, deltaY: 0, deltaZ: 0, deltaMode: 0 }, MOUSE));
sub('KeyboardEvent', { key: '', code: '', keyCode: 0, which: 0, charCode: 0, location: 0, repeat: false, ctrlKey: false, shiftKey: false, altKey: false, metaKey: false, isComposing: false });
sub('FocusEvent', { relatedTarget: null });
sub('InputEvent', { data: null, inputType: '', isComposing: false });
sub('MessageEvent', { data: null, origin: '', lastEventId: '', source: null, ports: [] });
sub('ErrorEvent', { message: '', filename: '', lineno: 0, colno: 0, error: null });
sub('PopStateEvent', { state: null });
sub('HashChangeEvent', { oldURL: '', newURL: '' });
sub('ProgressEvent', { lengthComputable: false, loaded: 0, total: 0 });
sub('TouchEvent', { touches: [], targetTouches: [], changedTouches: [] });
sub('PageTransitionEvent', { persisted: false });
sub('StorageEvent', { key: null, oldValue: null, newValue: null, url: '' });
sub('TransitionEvent', { propertyName: '', elapsedTime: 0 });
sub('AnimationEvent', { animationName: '', elapsedTime: 0 });
sub('SubmitEvent', { submitter: null });

function addL(t, type, fn, opts) {
  if (!fn) return;
  var cap = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
  var once = !!(opts && typeof opts === 'object' && opts.once);
  var L = t.__l || (defn(t, '__l', {}), t.__l);
  var a = L[type] || (L[type] = []);
  for (var i = 0; i < a.length; i++) if (a[i].f === fn && a[i].c === cap) return;
  a.push({ f: fn, c: cap, o: once });
  if (opts && opts.signal && opts.signal.addEventListener) opts.signal.addEventListener('abort', function () { remL(t, type, fn, cap); });
}
function remL(t, type, fn, opts) {
  var cap = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
  var a = t.__l && t.__l[type]; if (!a) return;
  for (var i = 0; i < a.length; i++) if (a[i].f === fn && a[i].c === cap) { a.splice(i, 1); return; }
}
function callh(t, f, ev) {
  try {
    if (typeof f === 'function') f.call(t, ev);
    else if (f && typeof f.handleEvent === 'function') f.handleEvent(ev);
  } catch (e) { log('error', ['Uncaught (en evento ' + ev.type + ')', e]); }
}
function attrHandler(t, type) {
  if (typeof t._attr !== 'function') return null;
  var code = t._attr('on' + type);
  if (!code) return null;
  var c = t.__ah || (defn(t, '__ah', {}), t.__ah);
  if (!c[type] || c[type].code !== code) {
    var f = null;
    try { f = new Function('event', code); } catch (e) { log('error', ['on' + type + ' inválido', e]); }
    c[type] = { code: code, f: f };
  }
  return c[type].f;
}
function invoke(t, ev, phase) {
  ev.currentTarget = t; ev.eventPhase = phase;
  if (phase !== 1) {
    var h = t['on' + ev.type];
    if (typeof h !== 'function') h = attrHandler(t, ev.type);
    if (typeof h === 'function') {
      try { var r = h.call(t, ev); if (r === false) ev.preventDefault(); } catch (e) { log('error', ['Uncaught (en on' + ev.type + ')', e]); }
    }
  }
  var a = t.__l && t.__l[ev.type];
  if (a) {
    var cp = a.slice();
    for (var i = 0; i < cp.length; i++) {
      var r = cp[i];
      if (phase !== 2 && r.c !== (phase === 1)) continue;
      if (r.o) remL(t, ev.type, r.f, r.c);
      callh(t, r.f, ev);
      if (ev.__imm) break;
    }
  }
}
function dispatch(t, ev) {
  ev.target = ev.srcElement = t;
  var path = [], n = t;
  while (n) {
    path.push(n);
    n = (typeof n._parent === 'function') ? n._parent() : null;
  }
  if (path[path.length - 1] === document) path.push(g);
  ev.__path = path.slice();
  var i;
  for (i = path.length - 1; i > 0 && !ev.__stop; i--) invoke(path[i], ev, 1);
  if (!ev.__stop) invoke(t, ev, 2);
  if (ev.bubbles) for (i = 1; i < path.length && !ev.__stop; i++) invoke(path[i], ev, 3);
  ev.eventPhase = 0; ev.currentTarget = null;
  return !ev.defaultPrevented;
}
function mixET(o) {
  defn(o, 'addEventListener', function (t, f, c) { addL(this, t, f, c); });
  defn(o, 'removeEventListener', function (t, f, c) { remL(this, t, f, c); });
  defn(o, 'dispatchEvent', function (ev) { return dispatch(this, ev); });
}
mixET(P); mixET(g);
function EventTarget() { }
mixET(EventTarget.prototype);
g.EventTarget = EventTarget;

/* ---------- temporizadores (reloj virtual) ---------- */
var vt = 0, tid = 1, tq = [], fired = 0;
function addT(fn, ms, args, rep) {
  if (typeof fn === 'string') { var code = fn; fn = function () { (0, eval)(code); }; }
  if (typeof fn !== 'function') return 0;
  ms = +ms || 0; if (ms < 0) ms = 0;
  var id = tid++;
  tq.push({ id: id, at: vt + (rep && ms < 4 ? 4 : ms), fn: fn, args: args, rep: rep ? Math.max(ms, 4) : -1, seq: id });
  return id;
}
g.setTimeout = function (f, ms) { return addT(f, ms, [].slice.call(arguments, 2), false); };
g.setInterval = function (f, ms) { return addT(f, ms, [].slice.call(arguments, 2), true); };
g.clearTimeout = g.clearInterval = function (id) { for (var i = 0; i < tq.length; i++) if (tq[i].id === id) { tq.splice(i, 1); return; } };
g.requestAnimationFrame = function (f) { return addT(function () { f(vt); }, 16, [], false); };
g.cancelAnimationFrame = g.clearTimeout;
g.requestIdleCallback = function (f) { return addT(function () { f({ didTimeout: false, timeRemaining: function () { return 20; } }); }, 1, [], false); };
g.cancelIdleCallback = g.clearTimeout;
if (typeof g.queueMicrotask !== 'function') g.queueMicrotask = function (f) { Promise.resolve().then(f); };
g.__hasTimers = function () { return tq.length > 0; };
g.__advance = function (ms) {
  var end = vt + ms, guard = 0;
  for (;;) {
    var bi = -1;
    for (var i = 0; i < tq.length; i++) if (tq[i].at <= end && (bi < 0 || tq[i].at < tq[bi].at || (tq[i].at === tq[bi].at && tq[i].seq < tq[bi].seq))) bi = i;
    if (bi < 0 || ++guard > 2500) break;
    var t = tq[bi];
    if (t.at > vt) vt = t.at;
    if (t.rep >= 0) { t.at = vt + t.rep; t.seq = tid++; } else tq.splice(bi, 1);
    try { t.fn.apply(g, t.args); } catch (e) { log('error', ['Uncaught (temporizador)', e]); }
    __pump();
  }
  if (end > vt) vt = end;
  return tq.length > 0;
};

/* ---------- utilidades de texto / codificación ---------- */
if (typeof g.TextEncoder === 'undefined') {
  g.TextEncoder = function () { this.encoding = 'utf-8'; };
  g.TextEncoder.prototype.encode = function (s) {
    s = String(s === undefined ? '' : s); var o = [];
    for (var i = 0; i < s.length; i++) {
      var c = s.charCodeAt(i);
      if (c >= 0xD800 && c < 0xDC00 && i + 1 < s.length) { var d = s.charCodeAt(i + 1); if (d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); i++; } }
      if (c < 0x80) o.push(c);
      else if (c < 0x800) o.push(0xC0 | (c >> 6), 0x80 | (c & 63));
      else if (c < 0x10000) o.push(0xE0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
      else o.push(0xF0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    }
    return new Uint8Array(o);
  };
}
if (typeof g.TextDecoder === 'undefined') {
  g.TextDecoder = function (enc) { this.encoding = enc || 'utf-8'; };
  g.TextDecoder.prototype.decode = function (b) {
    if (!b) return '';
    var u = b instanceof ArrayBuffer ? new Uint8Array(b) : new Uint8Array(b.buffer || b, b.byteOffset || 0, b.byteLength);
    var s = '', i = 0;
    while (i < u.length) {
      var c = u[i++], cp;
      if (c < 0x80) cp = c;
      else if (c >= 0xF0) { cp = ((c & 7) << 18) | ((u[i++] & 63) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63); }
      else if (c >= 0xE0) { cp = ((c & 15) << 12) | ((u[i++] & 63) << 6) | (u[i++] & 63); }
      else if (c >= 0xC0) { cp = ((c & 31) << 6) | (u[i++] & 63); }
      else cp = 0xFFFD;
      s += String.fromCodePoint(cp > 0x10FFFF ? 0xFFFD : cp);
    }
    return s;
  };
}
var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
g.btoa = function (s) {
  s = String(s); var o = '';
  for (var i = 0; i < s.length; i += 3) {
    var a = s.charCodeAt(i), b = s.charCodeAt(i + 1), c = s.charCodeAt(i + 2);
    if (a > 255 || b > 255 || c > 255) throw new Error('InvalidCharacterError');
    var n = (a << 16) | ((b || 0) << 8) | (c || 0);
    o += B64[(n >> 18) & 63] + B64[(n >> 12) & 63] + (i + 1 < s.length ? B64[(n >> 6) & 63] : '=') + (i + 2 < s.length ? B64[n & 63] : '=');
  }
  return o;
};
g.atob = function (s) {
  s = String(s).replace(/[\s=]+/g, ''); var o = '', acc_ = 0, bits = 0;
  for (var i = 0; i < s.length; i++) {
    var v = B64.indexOf(s[i]); if (v < 0) throw new Error('InvalidCharacterError');
    acc_ = (acc_ << 6) | v; bits += 6;
    if (bits >= 8) { bits -= 8; o += String.fromCharCode((acc_ >> bits) & 255); }
  }
  return o;
};
if (typeof g.structuredClone !== 'function') g.structuredClone = function (v) { return v === undefined ? v : JSON.parse(JSON.stringify(v)); };


/* ---------- Intl mínimo (QuickJS no lo trae) ---------- */
if (typeof g.Intl === 'undefined') {
  var pad = function (n, k) { n = String(n); while (n.length < (k || 2)) n = '0' + n; return n; };
  var MESES = ['enero', 'febrero', 'marzo', 'abril', 'mayo', 'junio', 'julio', 'agosto', 'septiembre', 'octubre', 'noviembre', 'diciembre'];
  var DateTimeFormat = function (loc, o) { this.o = o || {}; this.l = loc; };
  DateTimeFormat.prototype.format = function (d) {
    d = d === undefined ? new Date() : new Date(d); var o = this.o, s = [];
    if (o.dateStyle || o.year || o.month || o.day || (!o.timeStyle && !o.hour && !o.minute)) {
      if (o.month === 'long' || o.dateStyle === 'long' || o.dateStyle === 'full') s.push(d.getUTCDate() + ' de ' + MESES[d.getUTCMonth()] + ' de ' + d.getUTCFullYear());
      else s.push(pad(d.getUTCDate()) + '/' + pad(d.getUTCMonth() + 1) + '/' + d.getUTCFullYear());
    }
    if (o.timeStyle || o.hour || o.minute) s.push(pad(d.getUTCHours()) + ':' + pad(d.getUTCMinutes()) + (o.second || o.timeStyle === 'medium' ? ':' + pad(d.getUTCSeconds()) : ''));
    return s.join(', ');
  };
  DateTimeFormat.prototype.formatToParts = function (d) { return [{ type: 'literal', value: this.format(d) }]; };
  DateTimeFormat.prototype.resolvedOptions = function () { return { locale: 'es-MX', calendar: 'gregory', numberingSystem: 'latn', timeZone: 'UTC' }; };
  var NumberFormat = function (loc, o) { this.o = o || {}; };
  NumberFormat.prototype.format = function (n) {
    var o = this.o; n = +n; var mx = o.maximumFractionDigits, mn = o.minimumFractionDigits;
    if (mx === undefined) mx = o.style === 'currency' ? 2 : (mn !== undefined ? Math.max(mn, 3) : 3);
    if (mn === undefined) mn = o.style === 'currency' ? 2 : 0;
    var neg = n < 0; if (neg) n = -n;
    if (o.style === 'percent') n *= 100;
    var s = n.toFixed(Math.min(mx, 20));
    if (s.indexOf('.') >= 0) { var parts = s.split('.'), fr = parts[1]; while (fr.length > mn && fr[fr.length - 1] === '0') fr = fr.slice(0, -1); s = parts[0] + (fr ? '.' + fr : ''); }
    var ip = s.split('.'); if (o.useGrouping !== false) ip[0] = ip[0].replace(/\B(?=(\d{3})+(?!\d))/g, ',');
    s = ip.join('.');
    if (o.style === 'currency') s = (o.currency === 'EUR' ? '€' : o.currency === 'GBP' ? '£' : o.currency === 'MXN' ? 'MX$' : '$') + s;
    if (o.style === 'percent') s += '%';
    return (neg ? '-' : '') + s;
  };
  NumberFormat.prototype.formatToParts = function (n) { return [{ type: 'literal', value: this.format(n) }]; };
  NumberFormat.prototype.resolvedOptions = function () { return { locale: 'es-MX', numberingSystem: 'latn' }; };
  var Collator = function () { }; Collator.prototype.compare = function (a, b) { a = String(a); b = String(b); return a < b ? -1 : a > b ? 1 : 0; };
  Collator.prototype.resolvedOptions = function () { return { locale: 'es-MX' }; };
  var PluralRules = function (l, o) { this.o = o || {}; }; PluralRules.prototype.select = function (n) { return n === 1 ? 'one' : 'other'; };
  PluralRules.prototype.resolvedOptions = function () { return { locale: 'es-MX' }; };
  var RelativeTimeFormat = function () { }; RelativeTimeFormat.prototype.format = function (v, u) { u = String(u).replace(/s$/, ''); return v < 0 ? 'hace ' + (-v) + ' ' + u : 'en ' + v + ' ' + u; };
  var ListFormat = function (l, o) { this.o = o || {}; }; ListFormat.prototype.format = function (a) { a = [].slice.call(a); if (a.length < 2) return a.join(''); return a.slice(0, -1).join(', ') + (this.o.type === 'disjunction' ? ' o ' : ' y ') + a[a.length - 1]; };
  var Segmenter = function (l, o) { this.o = o || {}; };
  Segmenter.prototype.segment = function (s) { s = String(s); var gr = this.o.granularity || 'grapheme'; var parts = gr === 'word' ? s.split(/(\s+)/).filter(Boolean) : Array.from(s); var i = 0; return parts.map(function (p) { var r = { segment: p, index: i, input: s, isWordLike: /\w/.test(p) }; i += p.length; return r; }); };
  var Locale = function (t) { this.baseName = String(t); this.language = String(t).split('-')[0]; }; Locale.prototype.toString = function () { return this.baseName; }; Locale.prototype.maximize = Locale.prototype.minimize = function () { return this; }; Locale.prototype.getTextInfo = function () { return { direction: 'ltr' }; }; Locale.prototype.textInfo = { direction: 'ltr' };
  var sup = function (l) { return [].concat(l || []); };
  [DateTimeFormat, NumberFormat, Collator, PluralRules, RelativeTimeFormat, ListFormat, Segmenter].forEach(function (C) { C.supportedLocalesOf = sup; });
  g.Intl = { DateTimeFormat: DateTimeFormat, NumberFormat: NumberFormat, Collator: Collator, PluralRules: PluralRules, RelativeTimeFormat: RelativeTimeFormat, ListFormat: ListFormat, Segmenter: Segmenter, Locale: Locale,
    getCanonicalLocales: sup, supportedValuesOf: function () { return []; } };
}

/* ---------- AbortController / Headers / Blob / FormData ---------- */
function AbortSignal() { this.aborted = false; this.reason = undefined; this.onabort = null; }
AbortSignal.prototype = Object.create(EventTarget.prototype);
AbortSignal.prototype.throwIfAborted = function () { if (this.aborted) throw this.reason; };
AbortSignal.abort = function (r) { var s = new AbortSignal(); s.aborted = true; s.reason = r; return s; };
AbortSignal.timeout = function (ms) { var s = new AbortSignal(); setTimeout(function () { s.aborted = true; s.reason = new Error('TimeoutError'); s.dispatchEvent(new Event('abort')); }, ms); return s; };
function AbortController() { this.signal = new AbortSignal(); }
AbortController.prototype.abort = function (r) {
  var s = this.signal; if (s.aborted) return;
  s.aborted = true; s.reason = r === undefined ? new Error('AbortError') : r;
  var ev = new Event('abort'); if (typeof s.onabort === 'function') s.onabort(ev); s.dispatchEvent(ev);
};
g.AbortSignal = AbortSignal; g.AbortController = AbortController;

function Headers(init) {
  var m = {}; defn(this, '_m', m);
  if (init instanceof Headers) init.forEach(function (v, k) { m[k] = v; });
  else if (Array.isArray(init)) init.forEach(function (p) { m[String(p[0]).toLowerCase()] = String(p[1]); });
  else if (init && typeof init === 'object') for (var k in init) m[k.toLowerCase()] = String(init[k]);
}
Headers.prototype.get = function (k) { k = String(k).toLowerCase(); return HAS(this._m, k) ? this._m[k] : null; };
Headers.prototype.set = function (k, v) { this._m[String(k).toLowerCase()] = String(v); };
Headers.prototype.append = function (k, v) { k = String(k).toLowerCase(); this._m[k] = HAS(this._m, k) ? this._m[k] + ', ' + v : String(v); };
Headers.prototype.has = function (k) { return HAS(this._m, String(k).toLowerCase()); };
Headers.prototype.delete = function (k) { delete this._m[String(k).toLowerCase()]; };
Headers.prototype.forEach = function (f) { for (var k in this._m) f(this._m[k], k, this); };
Headers.prototype.entries = function () { var m = this._m; return Object.keys(m).map(function (k) { return [k, m[k]]; })[Symbol.iterator](); };
Headers.prototype.keys = function () { return Object.keys(this._m)[Symbol.iterator](); };
Headers.prototype.values = function () { var m = this._m; return Object.keys(m).map(function (k) { return m[k]; })[Symbol.iterator](); };
Headers.prototype[Symbol.iterator] = Headers.prototype.entries;
g.Headers = Headers;

function Blob(parts, opts) {
  var s = ''; (parts || []).forEach(function (p) { s += (p instanceof Blob) ? p._s : (p && p.byteLength !== undefined ? new TextDecoder().decode(p) : String(p)); });
  defn(this, '_s', s); this.type = (opts && opts.type) || ''; this.size = new TextEncoder().encode(s).length;
}
Blob.prototype.text = function () { return Promise.resolve(this._s); };
Blob.prototype.arrayBuffer = function () { return Promise.resolve(new TextEncoder().encode(this._s).buffer); };
Blob.prototype.slice = function () { return new Blob([this._s], { type: this.type }); };
g.Blob = Blob; g.File = function (parts, name, o) { var b = new Blob(parts, o); b.name = name; b.lastModified = Date.now(); return b; };
var blobN = 0;
g.URL = g.URL || {};

function FormData(form) {
  defn(this, '_e', []);
  if (form && form.elements) { var self = this; [].forEach.call(form._qsa('input,select,textarea', false), function (e) { var n = e.getAttribute('name'); if (n) self.append(n, e.value); }); }
}
FormData.prototype.append = function (k, v) { this._e.push([String(k), v]); };
FormData.prototype.set = function (k, v) { this.delete(k); this.append(k, v); };
FormData.prototype.get = function (k) { for (var i = 0; i < this._e.length; i++) if (this._e[i][0] === k) return this._e[i][1]; return null; };
FormData.prototype.getAll = function (k) { return this._e.filter(function (p) { return p[0] === k; }).map(function (p) { return p[1]; }); };
FormData.prototype.has = function (k) { return this.get(k) !== null; };
FormData.prototype.delete = function (k) { this._e = this._e.filter(function (p) { return p[0] !== k; }); };
FormData.prototype.forEach = function (f) { this._e.forEach(function (p) { f(p[1], p[0]); }); };
FormData.prototype.entries = function () { return this._e.slice()[Symbol.iterator](); };
FormData.prototype[Symbol.iterator] = FormData.prototype.entries;
g.FormData = FormData;

/* ---------- URL / URLSearchParams ---------- */
function enc(s) { return encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, function (c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); }); }
function dec(s) { try { return decodeURIComponent(s.replace(/\+/g, ' ')); } catch (e) { return s; } }
function URLSearchParams(init) {
  defn(this, '_l', []); defn(this, '_u', null);
  if (typeof init === 'string') {
    if (init[0] === '?') init = init.slice(1);
    if (init) init.split('&').forEach(function (p) { if (!p) return; var i = p.indexOf('='); this._l.push(i < 0 ? [dec(p), ''] : [dec(p.slice(0, i)), dec(p.slice(i + 1))]); }, this);
  } else if (init instanceof URLSearchParams) this._l = init._l.map(function (p) { return p.slice(); });
  else if (Array.isArray(init)) init.forEach(function (p) { this._l.push([String(p[0]), String(p[1])]); }, this);
  else if (init && typeof init === 'object') Object.keys(init).forEach(function (k) { this._l.push([k, String(init[k])]); }, this);
}
var UP = URLSearchParams.prototype;
UP._sync = function () { if (this._u) this._u._setSearch(this.toString()); };
UP.append = function (k, v) { this._l.push([String(k), String(v)]); this._sync(); };
UP.delete = function (k) { this._l = this._l.filter(function (p) { return p[0] !== k; }); this._sync(); };
UP.get = function (k) { for (var i = 0; i < this._l.length; i++) if (this._l[i][0] === k) return this._l[i][1]; return null; };
UP.getAll = function (k) { return this._l.filter(function (p) { return p[0] === k; }).map(function (p) { return p[1]; }); };
UP.has = function (k) { return this.get(k) !== null; };
UP.set = function (k, v) { k = String(k); v = String(v); var f = false, o = []; this._l.forEach(function (p) { if (p[0] === k) { if (!f) { f = true; o.push([k, v]); } } else o.push(p); }); if (!f) o.push([k, v]); this._l = o; this._sync(); };
UP.sort = function () { this._l.sort(function (a, b) { return a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0; }); this._sync(); };
UP.forEach = function (f, t) { this._l.forEach(function (p) { f.call(t, p[1], p[0], this); }, this); };
UP.keys = function () { return this._l.map(function (p) { return p[0]; })[Symbol.iterator](); };
UP.values = function () { return this._l.map(function (p) { return p[1]; })[Symbol.iterator](); };
UP.entries = function () { return this._l.map(function (p) { return [p[0], p[1]]; })[Symbol.iterator](); };
UP[Symbol.iterator] = UP.entries;
UP.toString = function () { return this._l.map(function (p) { return enc(p[0]) + '=' + enc(p[1]); }).join('&'); };
acc(UP, 'size', function () { return this._l.length; });
g.URLSearchParams = URLSearchParams;

function URL(u, base) {
  u = String(u);
  var abs = u;
  if (!/^[a-zA-Z][a-zA-Z0-9+.-]*:/.test(u)) {
    if (base === undefined) throw new TypeError("Invalid URL: " + u);
    abs = __resolve(String(base), u);
  }
  var m = /^([a-zA-Z][a-zA-Z0-9+.-]*:)(?:\/\/([^\/?#]*))?([^?#]*)(\?[^#]*)?(#.*)?$/.exec(abs);
  if (!m) throw new TypeError("Invalid URL: " + u);
  var auth = m[2] || '', at = auth.lastIndexOf('@'), cred = at >= 0 ? auth.slice(0, at) : '', host = at >= 0 ? auth.slice(at + 1) : auth;
  var pm = /^(.*?)(?::(\d*))?$/.exec(host);
  this._p = m[1].toLowerCase(); this._h = (pm[1] || '').toLowerCase(); this._port = pm[2] || '';
  if ((this._p === 'http:' && this._port === '80') || (this._p === 'https:' && this._port === '443')) this._port = '';
  this._path = m[3] || (m[2] !== undefined ? '/' : ''); this._q = m[4] && m[4] !== '?' ? m[4] : ''; this._f = m[5] && m[5] !== '#' ? m[5] : '';
  this._cred = cred; this._sp = null;
}
var UPr = URL.prototype;
UPr._setSearch = function (s) { this._q = s ? '?' + s : ''; };
function ro(k, get, set) { acc(UPr, k, get, set); }
ro('protocol', function () { return this._p; }, function (v) { this._p = String(v).replace(/:?$/, ':'); });
ro('hostname', function () { return this._h; }, function (v) { this._h = String(v); });
ro('port', function () { return this._port; }, function (v) { this._port = String(v); });
ro('host', function () { return this._h + (this._port ? ':' + this._port : ''); }, function (v) { var m = /^(.*?)(?::(\d*))?$/.exec(String(v)); this._h = m[1]; this._port = m[2] || ''; });
ro('pathname', function () { return this._path; }, function (v) { v = String(v); this._path = v[0] === '/' ? v : '/' + v; });
ro('search', function () { return this._q; }, function (v) { v = String(v); this._q = v && v !== '?' ? (v[0] === '?' ? v : '?' + v) : ''; this._sp = null; });
ro('hash', function () { return this._f; }, function (v) { v = String(v); this._f = v && v !== '#' ? (v[0] === '#' ? v : '#' + v) : ''; });
ro('username', function () { return this._cred.split(':')[0]; });
ro('password', function () { return this._cred.split(':')[1] || ''; });
ro('origin', function () { return /^https?:$/.test(this._p) ? this._p + '//' + this.host : 'null'; });
ro('searchParams', function () { if (!this._sp) { this._sp = new URLSearchParams(this._q); this._sp._u = this; } return this._sp; });
ro('href', function () {
  var hasAuth = this._h || this._p === 'file:' || /^https?:$/.test(this._p);
  return this._p + (hasAuth ? '//' : '') + (this._cred ? this._cred + '@' : '') + this.host + this._path + this._q + this._f;
}, function (v) { var n = new URL(v); this._p = n._p; this._h = n._h; this._port = n._port; this._path = n._path; this._q = n._q; this._f = n._f; this._cred = n._cred; this._sp = null; });
UPr.toString = function () { return this.href; };
UPr.toJSON = function () { return this.href; };
URL.canParse = function (u, b) { try { new URL(u, b); return true; } catch (e) { return false; } };
URL.createObjectURL = function (b) { var u = 'blob:cosmos/' + (++blobN); (URL._blobs || (URL._blobs = {}))[u] = b; return u; };
URL.revokeObjectURL = function (u) { if (URL._blobs) delete URL._blobs[u]; };
g.URL = URL;

/* ---------- fetch / XHR (síncronos por debajo) ---------- */
var PUBASE = function () { return document.baseURI; };
function Response(body, init) {
  init = init || {};
  defn(this, '_b', body === undefined || body === null ? '' : (body instanceof Blob ? body._s : (typeof body === 'object' && body.byteLength !== undefined ? new TextDecoder().decode(body) : String(body))));
  this.status = init.status !== undefined ? init.status : 200; this.statusText = init.statusText || '';
  this.ok = this.status >= 200 && this.status < 300; this.headers = new Headers(init.headers); this.url = init.url || '';
  this.redirected = false; this.type = 'basic'; this.bodyUsed = false;
}
Response.prototype.text = function () { this.bodyUsed = true; return Promise.resolve(this._b); };
Response.prototype.json = function () { var b = this._b; this.bodyUsed = true; return new Promise(function (res, rej) { try { res(JSON.parse(b)); } catch (e) { rej(e); } }); };
Response.prototype.arrayBuffer = function () { this.bodyUsed = true; return Promise.resolve(new TextEncoder().encode(this._b).buffer); };
Response.prototype.blob = function () { this.bodyUsed = true; return Promise.resolve(new Blob([this._b], { type: this.headers.get('content-type') || '' })); };
Response.prototype.formData = function () { return Promise.resolve(new FormData()); };
Response.prototype.clone = function () { return new Response(this._b, { status: this.status, headers: this.headers, url: this.url }); };
Response.error = function () { var r = new Response('', { status: 0 }); r.type = 'error'; return r; };
Response.json = function (d, i) { return new Response(JSON.stringify(d), i); };
g.Response = Response;
function Request(input, init) {
  init = init || {};
  this.url = input instanceof Request ? input.url : String(input);
  this.method = (init.method || (input instanceof Request && input.method) || 'GET').toUpperCase();
  this.headers = new Headers(init.headers); this.body = init.body === undefined ? null : init.body; this.signal = init.signal || null;
  this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors';
}
Request.prototype.clone = function () { return new Request(this); };
g.Request = Request;
function doFetch(method, url, body) {
  var r = __fetch(method, url, body === undefined || body === null ? '' : String(body));
  if (!r.status) return null;
  var res = new Response(r.body, { status: r.status, url: r.url, headers: r.ctype ? { 'content-type': r.ctype } : {} });
  return res;
}
g.fetch = function (input, init) {
  var req = input instanceof Request ? input : new Request(input, init);
  return new Promise(function (resolve, reject) {
    if (req.signal && req.signal.aborted) { reject(req.signal.reason || new Error('AbortError')); return; }
    var r = doFetch(req.method, __resolve(PUBASE(), req.url), req.body);
    setTimeout(function () { if (r) resolve(r); else reject(new TypeError('Failed to fetch')); }, 0);
  });
};
function XHR() {
  this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.response = null; this.responseType = ''; this.responseURL = '';
  this.timeout = 0; this.withCredentials = false; this.upload = new EventTarget();
  defn(this, '_h', {}); defn(this, '_rh', {});
}
XHR.prototype = Object.create(EventTarget.prototype);
XHR.UNSENT = 0; XHR.OPENED = 1; XHR.HEADERS_RECEIVED = 2; XHR.LOADING = 3; XHR.DONE = 4;
XHR.prototype.open = function (m, u, a) { defn(this, '_m', String(m).toUpperCase()); defn(this, '_u', String(u)); defn(this, '_async', a !== false); this.readyState = 1; this._ev('readystatechange'); };
XHR.prototype.setRequestHeader = function (k, v) { this._h[k.toLowerCase()] = v; };
XHR.prototype.getResponseHeader = function (k) { k = k.toLowerCase(); return HAS(this._rh, k) ? this._rh[k] : null; };
XHR.prototype.getAllResponseHeaders = function () { var s = ''; for (var k in this._rh) s += k + ': ' + this._rh[k] + '\r\n'; return s; };
XHR.prototype.overrideMimeType = function () { };
XHR.prototype.abort = function () { this._ab = true; this.readyState = 0; this._ev('abort'); };
XHR.prototype._ev = function (t) {
  var ev = new ProgressEvent(t, { loaded: 1, total: 1 }); ev.target = this;
  var h = this['on' + t]; if (typeof h === 'function') { try { h.call(this, ev); } catch (e) { log('error', ['Uncaught (XHR)', e]); } }
  var a = this.__l && this.__l[t]; if (a) a.slice().forEach(function (r) { callh(this, r.f, ev); }, this);
};
XHR.prototype.send = function (body) {
  var self = this;
  var finish = function () {
    if (self._ab) return;
    var r = __fetch(self._m, __resolve(PUBASE(), self._u), body === undefined || body === null ? '' : String(body));
    if (!r.status) { self.readyState = 4; self._ev('readystatechange'); self._ev('error'); self._ev('loadend'); return; }
    self.status = r.status; self.statusText = r.ok ? 'OK' : ''; self.responseURL = r.url;
    if (r.ctype) self._rh['content-type'] = r.ctype;
    self.responseText = r.body;
    if (self.responseType === 'json') { try { self.response = JSON.parse(r.body); } catch (e) { self.response = null; } }
    else if (self.responseType === 'arraybuffer') self.response = new TextEncoder().encode(r.body).buffer;
    else if (self.responseType === 'blob') self.response = new Blob([r.body], { type: r.ctype });
    else self.response = r.body;
    self.readyState = 2; self._ev('readystatechange'); self.readyState = 3; self._ev('readystatechange');
    self.readyState = 4; self._ev('readystatechange'); self._ev('load'); self._ev('loadend');
  };
  if (this._async) setTimeout(finish, 0); else finish();
};
g.XMLHttpRequest = XHR;
g.WebSocket = function () { throw new Error('WebSocket no soportado'); };
g.EventSource = function () { throw new Error('EventSource no soportado'); };
g.Worker = function () { throw new Error('Worker no soportado'); };

/* ---------- almacenamiento ---------- */
function mkStorage() {
  var m = {};
  var api = {
    getItem: function (k) { k = String(k); return HAS(m, k) ? m[k] : null; },
    setItem: function (k, v) { m[String(k)] = String(v); },
    removeItem: function (k) { delete m[String(k)]; },
    clear: function () { m = {}; },
    key: function (i) { var ks = Object.keys(m); return i < ks.length ? ks[i] : null; }
  };
  return new Proxy(api, {
    get: function (t, k) { if (k === 'length') return Object.keys(m).length; if (k in t) return t[k]; if (typeof k === 'symbol') return undefined; return HAS(m, k) ? m[k] : undefined; },
    set: function (t, k, v) { m[k] = String(v); return true; },
    has: function (t, k) { return k in t || HAS(m, k); },
    deleteProperty: function (t, k) { delete m[k]; return true; },
    ownKeys: function () { return Object.keys(m); },
    getOwnPropertyDescriptor: function (t, k) { return HAS(m, k) ? { value: m[k], enumerable: true, configurable: true, writable: true } : undefined; }
  });
}
g.localStorage = mkStorage(); g.sessionStorage = mkStorage();

/* ---------- Node / Element ---------- */
function kebab(s) { return s.replace(/[A-Z]/g, function (m) { return '-' + m.toLowerCase(); }); }
function camel(s) { return s.replace(/-([a-z])/g, function (m, c) { return c.toUpperCase(); }); }
function mkList(a) { defn(a, 'item', function (i) { return this[i] || null; }); return a; }
function Node() { throw new TypeError('Illegal constructor'); }
Node.prototype = P; defn(P, 'constructor', Node);
Node.ELEMENT_NODE = 1; Node.ATTRIBUTE_NODE = 2; Node.TEXT_NODE = 3; Node.CDATA_SECTION_NODE = 4; Node.COMMENT_NODE = 8; Node.DOCUMENT_NODE = 9; Node.DOCUMENT_TYPE_NODE = 10; Node.DOCUMENT_FRAGMENT_NODE = 11;
Node.DOCUMENT_POSITION_DISCONNECTED = 1; Node.DOCUMENT_POSITION_PRECEDING = 2; Node.DOCUMENT_POSITION_FOLLOWING = 4; Node.DOCUMENT_POSITION_CONTAINS = 8; Node.DOCUMENT_POSITION_CONTAINED_BY = 16;
g.Node = Node;
function isNode(o) { return o !== null && typeof o === 'object' && typeof o._type === 'function'; }
function nt(o) { return isNode(o) ? o.nodeType : 0; }
var SHARED = { Element: 1, HTMLElement: 1, Document: 1 };
function cls(name, test) {
  var C = function () { throw new TypeError('Illegal constructor'); };
  Object.defineProperty(C, 'name', { value: name });
  C.prototype = SHARED[name] ? P : Object.create(P);
  Object.defineProperty(C, Symbol.hasInstance, { value: test });
  g[name] = C; return C;
}
cls('Element', function (o) { return nt(o) === 1; });
cls('CharacterData', function (o) { return nt(o) === 3 || nt(o) === 8; });
cls('Text', function (o) { return nt(o) === 3; });
cls('Comment', function (o) { return nt(o) === 8; });
cls('Document', function (o) { return nt(o) === 9; });
cls('DocumentFragment', function (o) { return nt(o) === 11; });
cls('ShadowRoot', function (o) { return nt(o) === 11 && !!o.host; });
cls('HTMLElement', function (o) { return nt(o) === 1; });
cls('SVGElement', function (o) { return nt(o) === 1 && o.__svg === true; });
var TAGS = { HTMLDivElement: 'div', HTMLSpanElement: 'span', HTMLAnchorElement: 'a', HTMLImageElement: 'img', HTMLInputElement: 'input', HTMLButtonElement: 'button', HTMLFormElement: 'form',
  HTMLSelectElement: 'select', HTMLOptionElement: 'option', HTMLTextAreaElement: 'textarea', HTMLScriptElement: 'script', HTMLStyleElement: 'style', HTMLLinkElement: 'link', HTMLCanvasElement: 'canvas',
  HTMLVideoElement: 'video', HTMLAudioElement: 'audio', HTMLIFrameElement: 'iframe', HTMLTableElement: 'table', HTMLParagraphElement: 'p', HTMLBodyElement: 'body', HTMLHeadElement: 'head',
  HTMLHtmlElement: 'html', HTMLLabelElement: 'label', HTMLUListElement: 'ul', HTMLOListElement: 'ol', HTMLLIElement: 'li', HTMLTemplateElement: 'template', HTMLMetaElement: 'meta',
  HTMLTitleElement: 'title', HTMLBRElement: 'br', HTMLHRElement: 'hr', HTMLPreElement: 'pre', HTMLTableRowElement: 'tr', HTMLTableCellElement: 'td', HTMLSourceElement: 'source' };
Object.keys(TAGS).forEach(function (k) { var t = TAGS[k]; cls(k, function (o) { return nt(o) === 1 && o._name() === t; }); });
g.HTMLHeadingElement = cls('HTMLHeadingElement', function (o) { return nt(o) === 1 && /^h[1-6]$/.test(o._name()); });
g.HTMLMediaElement = cls('HTMLMediaElement', function (o) { return nt(o) === 1 && /^(video|audio)$/.test(o._name()); });
g.NodeList = cls('NodeList', function (o) { return Array.isArray(o); });
g.HTMLCollection = cls('HTMLCollection', function (o) { return Array.isArray(o); });

acc(P, 'nodeType', function () {
  var t = this._type();
  return t === 3 && this.__comment ? 8 : t;
});
acc(P, 'nodeName', function () {
  var t = this._type();
  if (t === 3) return this.__comment ? '#comment' : '#text';
  if (t === 9) return '#document';
  if (t === 11) return '#document-fragment';
  var n = this._name(); return this.__svg ? n : n.toUpperCase();
});
acc(P, 'tagName', function () { return this._type() === 1 ? this.nodeName : undefined; });
acc(P, 'localName', function () { return this._type() === 1 ? this._name() : undefined; });
acc(P, 'namespaceURI', function () { return this._type() === 1 ? (this.__svg ? 'http://www.w3.org/2000/svg' : 'http://www.w3.org/1999/xhtml') : null; });
acc(P, 'prefix', function () { return null; });
acc(P, 'ownerDocument', function () { return this._type() === 9 ? null : document; });
acc(P, 'parentNode', function () { return this._parent(); });
acc(P, 'parentElement', function () { var p = this._parent(); return p && p._type() === 1 ? p : null; });
acc(P, 'firstChild', function () { return this._first(); });
acc(P, 'lastChild', function () { return this._last(); });
acc(P, 'nextSibling', function () { return this._next(); });
acc(P, 'previousSibling', function () { return this._prev(); });
acc(P, 'childNodes', function () { var a = [], c = this._first(); while (c) { a.push(c); c = c._next(); } return mkList(a); });
acc(P, 'children', function () { var a = [], c = this._first(); while (c) { if (c._type() === 1) a.push(c); c = c._next(); } return mkList(a); });
acc(P, 'firstElementChild', function () { var c = this._first(); while (c && c._type() !== 1) c = c._next(); return c; });
acc(P, 'lastElementChild', function () { var c = this._last(); while (c && c._type() !== 1) c = c._prev(); return c; });
acc(P, 'nextElementSibling', function () { var c = this._next(); while (c && c._type() !== 1) c = c._next(); return c; });
acc(P, 'previousElementSibling', function () { var c = this._prev(); while (c && c._type() !== 1) c = c._prev(); return c; });
acc(P, 'childElementCount', function () { return this.children.length; });
acc(P, 'isConnected', function () { var n = this; while (n) { if (n === document) return true; n = n._parent(); } return false; });
P.hasChildNodes = function () { return this._first() !== null; };
P.getRootNode = function () { var n = this; while (n._parent()) n = n._parent(); return n; };
P.contains = function (o) { while (o) { if (o === this) return true; o = isNode(o) ? o._parent() : null; } return false; };
P.isSameNode = function (o) { return o === this; };
P.isEqualNode = function (o) { return !!o && this._outer() === o._outer(); };
P.normalize = function () { };
P.compareDocumentPosition = function (o) {
  if (o === this) return 0;
  if (this.contains(o)) return 20;
  if (o.contains(this)) return 10;
  var all = document._qsa('*', false), a = all.indexOf(this), b = all.indexOf(o);
  return b < 0 || a < 0 ? 1 : (b > a ? 4 : 2);
};
P.cloneNode = function (deep) { var c = this._clone(!!deep); if (this.__comment) c.__comment = true; return c; };
P.lookupNamespaceURI = function () { return null; };

function runScriptIfNeeded(n) {
  if (!isNode(n) || n._type() !== 1 || n._name() !== 'script' || n.__ran) return;
  if (!n.isConnected) return;
  defn(n, '__ran', true);
  var ok = __runScript(n);
  if (ok) { var ev = new Event('load'); dispatch(n, ev); }
}
function afterInsert(n) {
  var t = n._type();
  if (t === 1) runScriptIfNeeded(n);
}
function takeFragment(n) {
  var kids = [], c = n._first();
  while (c) { kids.push(c); c = c._next(); }
  return kids;
}
function insertOne(parent, node, ref) {
  if (!isNode(node)) node = document.createTextNode(String(node));
  if (node._type() === 11) {
    var kids = takeFragment(node);
    kids.forEach(function (k) { parent._insert(k, ref); });
    kids.forEach(afterInsert);
    return node;
  }
  parent._insert(node, ref);
  afterInsert(node);
  return node;
}
P.appendChild = function (n) { return insertOne(this, n, null); };
P.insertBefore = function (n, ref) { return insertOne(this, n, ref || null); };
P.removeChild = function (c) { if (c._parent() !== this) throw new Error('NotFoundError: el nodo no es hijo'); c._detach(); return c; };
P.replaceChild = function (n, old) { if (old._parent() !== this) throw new Error('NotFoundError'); var ref = old._next(); old._detach(); insertOne(this, n, ref); return old; };
P.remove = function () { if (this._parent()) this._detach(); };
function nodesOf(args) { return [].map.call(args, function (a) { return isNode(a) ? a : document.createTextNode(String(a)); }); }
P.append = function () { var s = this; nodesOf(arguments).forEach(function (n) { insertOne(s, n, null); }); };
P.prepend = function () { var s = this, ref = s._first(); nodesOf(arguments).forEach(function (n) { insertOne(s, n, ref); }); };
P.before = function () { var p = this._parent(), s = this; if (p) nodesOf(arguments).forEach(function (n) { insertOne(p, n, s); }); };
P.after = function () { var p = this._parent(), r = this._next(); if (p) nodesOf(arguments).forEach(function (n) { insertOne(p, n, r); }); };
P.replaceWith = function () { var p = this._parent(), r = this._next(); if (!p) return; this._detach(); nodesOf(arguments).forEach(function (n) { insertOne(p, n, r); }); };
P.replaceChildren = function () { var s = this; takeFragment(s).forEach(function (c) { c._detach(); }); nodesOf(arguments).forEach(function (n) { insertOne(s, n, null); }); };

acc(P, 'textContent', function () { var t = this._type(); return t === 9 ? null : this._text(); }, function (v) { this._setText(v == null ? '' : String(v)); });
acc(P, 'innerText', function () { return this._text(); }, function (v) { this._setText(v == null ? '' : String(v)); });
acc(P, 'outerText', function () { return this._text(); });
acc(P, 'innerHTML', function () { return this._html(); }, function (v) { this._setHtml(v == null ? '' : String(v)); });
acc(P, 'outerHTML', function () { return this._outer(); }, function (v) {
  var p = this._parent(); if (!p) return;
  var tmp = document.createElement('div'); tmp._setHtml(String(v));
  var r = this._next(); this._detach();
  takeFragment(tmp).forEach(function (k) { p._insert(k, r); });
});
acc(P, 'nodeValue', function () { return this._type() === 3 ? this._data() : null; }, function (v) { if (this._type() === 3) this._setData(String(v)); });
acc(P, 'data', function () { return this._type() === 3 ? this._data() : undefined; }, function (v) { if (this._type() === 3) this._setData(String(v)); });
acc(P, 'length', function () { return this._type() === 3 ? this._data().length : undefined; });
acc(P, 'wholeText', function () { return this._data(); });
P.appendData = function (s) { this._setData(this._data() + s); };
P.insertAdjacentHTML = function (pos, html) {
  var tmp = document.createElement('div'); tmp._setHtml(String(html)); var kids = takeFragment(tmp), p;
  pos = String(pos).toLowerCase();
  if (pos === 'beforeend') { var s = this; kids.forEach(function (k) { s._insert(k, null); }); }
  else if (pos === 'afterbegin') { var f = this._first(), s2 = this; kids.forEach(function (k) { s2._insert(k, f); }); }
  else if (pos === 'beforebegin') { p = this._parent(); var s3 = this; if (p) kids.forEach(function (k) { p._insert(k, s3); }); }
  else if (pos === 'afterend') { p = this._parent(); var nx = this._next(); if (p) kids.forEach(function (k) { p._insert(k, nx); }); }
};
P.insertAdjacentElement = function (pos, el) { var h = document.createDocumentFragment(); h.appendChild(el); pos = String(pos).toLowerCase(); var p = this._parent(); if (pos === 'beforeend') this.appendChild(el); else if (pos === 'afterbegin') this.insertBefore(el, this._first()); else if (pos === 'beforebegin' && p) p.insertBefore(el, this); else if (pos === 'afterend' && p) p.insertBefore(el, this._next()); return el; };
P.insertAdjacentText = function (pos, t) { this.insertAdjacentElement(pos, document.createTextNode(String(t))); };

/* atributos */
P.getAttribute = function (k) { return this._attr(String(k).toLowerCase()); };
P.getAttributeNS = function (ns, k) { return this._attr(String(k).toLowerCase()); };
P.setAttribute = function (k, v) { this._setAttr(String(k), String(v)); };
P.setAttributeNS = function (ns, k, v) { this._setAttr(String(k), String(v)); };
P.removeAttribute = function (k) { this._rmAttr(String(k).toLowerCase()); };
P.removeAttributeNS = function (ns, k) { this._rmAttr(String(k).toLowerCase()); };
P.hasAttribute = function (k) { return this._attr(String(k).toLowerCase()) !== null; };
P.hasAttributes = function () { return this._attrNames().length > 0; };
P.getAttributeNames = function () { return this._attrNames(); };
P.toggleAttribute = function (k, f) { var h = this.hasAttribute(k); if (f === undefined) f = !h; if (f && !h) this.setAttribute(k, ''); else if (!f && h) this.removeAttribute(k); return f; };
acc(P, 'attributes', function () {
  var s = this;
  return mkList(this._attrNames().map(function (n) { var v = s._attr(n); return { name: n, value: v, nodeName: n, nodeValue: v, specified: true, ownerElement: s }; }));
});
function reflectStr(prop, attr) { acc(P, prop, function () { var v = this._attr(attr); return v === null ? '' : v; }, function (v) { this._setAttr(attr, String(v)); }); }
function reflectUrl(prop, attr) { acc(P, prop, function () { var v = this._attr(attr); return v === null ? '' : (v === '' ? '' : __resolve(document.baseURI, v)); }, function (v) { this._setAttr(attr, String(v)); }); }
function reflectBool(prop, attr) { acc(P, prop, function () { return this._attr(attr) !== null; }, function (v) { if (v) this._setAttr(attr, ''); else this._rmAttr(attr); }); }
function reflectNum(prop, attr, def) { acc(P, prop, function () { var v = parseInt(this._attr(attr), 10); return isNaN(v) ? def : v; }, function (v) { this._setAttr(attr, String(v)); }); }
['id', 'title', 'lang', 'dir', 'alt', 'name', 'placeholder', 'rel', 'target', 'download', 'media', 'content', 'charset', 'method', 'action', 'accept', 'autocomplete', 'role', 'slot', 'integrity', 'crossOrigin:crossorigin', 'referrerPolicy:referrerpolicy', 'httpEquiv:http-equiv', 'accessKey:accesskey', 'srcset', 'sizes', 'loading', 'decoding', 'pattern', 'min', 'max', 'step', 'wrap', 'label', 'as', 'hreflang', 'enctype', 'ping', 'poster', 'preload']
  .forEach(function (k) { var p = k.split(':'); reflectStr(p[0], p[1] || p[0].toLowerCase()); });
reflectStr('className', 'class'); reflectStr('htmlFor', 'for');
['href', 'src', 'currentSrc:src', 'formAction:formaction'].forEach(function (k) { var p = k.split(':'); reflectUrl(p[0], p[1] || p[0]); });
['hidden', 'disabled', 'readOnly:readonly', 'required', 'multiple', 'autofocus', 'async', 'defer', 'noModule:nomodule', 'controls', 'autoplay', 'loop', 'muted', 'open', 'draggable', 'isMap:ismap', 'noValidate:novalidate', 'selected']
  .forEach(function (k) { var p = k.split(':'); reflectBool(p[0], p[1] || p[0].toLowerCase()); });
reflectNum('tabIndex', 'tabindex', -1); reflectNum('colSpan', 'colspan', 1); reflectNum('rowSpan', 'rowspan', 1); reflectNum('maxLength', 'maxlength', -1); reflectNum('size', 'size', 0);
acc(P, 'type', function () { var v = this._attr('type'); return v === null ? (this._name() === 'input' ? 'text' : (this._name() === 'button' ? 'submit' : '')) : v; }, function (v) { this._setAttr('type', String(v)); });
acc(P, 'width', function () { var v = parseInt(this._attr('width'), 10); return isNaN(v) ? 0 : v; }, function (v) { this._setAttr('width', String(v)); });
acc(P, 'height', function () { var v = parseInt(this._attr('height'), 10); return isNaN(v) ? 0 : v; }, function (v) { this._setAttr('height', String(v)); });
acc(P, 'naturalWidth', function () { return 0; }); acc(P, 'naturalHeight', function () { return 0; });
acc(P, 'complete', function () { return true; });
['protocol', 'host', 'hostname', 'port', 'pathname', 'search', 'hash', 'origin'].forEach(function (k) {
  var d = Object.getOwnPropertyDescriptor(P, k);
  acc(P, k, function () { if (this._name() !== 'a' && this._name() !== 'area') return undefined; try { return new URL(this.href)[k]; } catch (e) { return ''; } }, d && d.set);
});
acc(P, 'value', function () {
  if (HAS(this, '__value')) return this.__value;
  var n = this._name();
  if (n === 'textarea') return this._text();
  if (n === 'select') { var o = this._qsa('option', false), s = null; for (var i = 0; i < o.length; i++) if (o[i].hasAttribute('selected')) { s = o[i]; break; } if (!s) s = o[0]; return s ? s.value : ''; }
  if (n === 'option') { var v = this._attr('value'); return v === null ? this._text() : v; }
  var a = this._attr('value'); return a === null ? '' : a;
}, function (v) {
  v = v == null ? '' : String(v); var n = this._name();
  if (n === 'select') { this._qsa('option', false).forEach(function (o) { if (o.value === v) o.setAttribute('selected', ''); else o.removeAttribute('selected'); }); return; }
  defn(this, '__value', v);
});
acc(P, 'defaultValue', function () { var a = this._attr('value'); return a === null ? '' : a; });
acc(P, 'checked', function () { return HAS(this, '__checked') ? this.__checked : this._attr('checked') !== null; }, function (v) { defn(this, '__checked', !!v); });
acc(P, 'selectedIndex', function () { var o = this._qsa('option', false); for (var i = 0; i < o.length; i++) if (o[i].hasAttribute('selected')) return i; return o.length ? 0 : -1; }, function (i) { this._qsa('option', false).forEach(function (o, k) { if (k === i) o.setAttribute('selected', ''); else o.removeAttribute('selected'); }); });
acc(P, 'options', function () { return this._qsa('option', false); });
acc(P, 'form', function () { var n = this._parent(); while (n && !(n._type() === 1 && n._name() === 'form')) n = n._parent(); return n; });
acc(P, 'elements', function () { return this._qsa('input,select,textarea,button', false); });
acc(P, 'label', function () { return this._attr('label') || this._text(); });
acc(P, 'text', function () { return this._text(); }, function (v) { this._setText(String(v)); });
acc(P, 'content', function () {
  if (this._name() !== 'template') { var v = this._attr('content'); return v === null ? '' : v; }
  if (!HAS(this, '__content')) { var f = document.createDocumentFragment(); takeFragment(this).forEach(function (k) { f._insert(k, null); }); defn(this, '__content', f); }
  return this.__content;
}, function (v) { this._setAttr('content', String(v)); });
acc(P, 'host', function () { return this.__host || (this._type() === 1 && this._name() === 'a' ? (function (s) { try { return new URL(s.href).host; } catch (e) { return ''; } })(this) : undefined); });
P.submit = function () { }; P.reset = function () { }; P.checkValidity = function () { return true; }; P.reportValidity = function () { return true; }; P.setCustomValidity = function () { };
P.focus = function () { if (isNode(this)) defn(document, '__active', this); }; P.blur = function () { }; P.select = function () { }; P.setSelectionRange = function () { };
P.click = function () { var ev = new MouseEvent('click', { bubbles: true, cancelable: true }); dispatch(this, ev); };
P.scrollIntoView = P.scrollTo = P.scroll = P.scrollBy = P.scrollIntoViewIfNeeded = function () { };
P.showModal = function () { this.setAttribute('open', ''); }; P.show = P.showModal; P.close = function () { this.removeAttribute('open'); };
P.play = function () { return Promise.resolve(); }; P.pause = function () { }; P.load = function () { };
P.canPlayType = function () { return ''; };
P.getContext = function () { return null; }; P.toDataURL = function () { return 'data:,'; };
P.animate = function () { return { cancel: function () { }, finish: function () { }, play: function () { }, pause: function () { }, finished: Promise.resolve(), onfinish: null, addEventListener: function () { } }; };
P.getAnimations = function () { return []; };
P.attachShadow = function (o) { var f = document.createDocumentFragment(); defn(f, '__host', this); defn(this, 'shadowRoot', o && o.mode === 'closed' ? null : f); return f; };
P.requestFullscreen = P.requestPointerLock = P.setPointerCapture = P.releasePointerCapture = function () { return Promise.resolve(); };
P.getBoundingClientRect = function () { return { x: 0, y: 0, width: 0, height: 0, top: 0, left: 0, right: 0, bottom: 0, toJSON: function () { return {}; } }; };
P.getClientRects = function () { return []; };
P.closest = function (sel) { var n = this; while (n && n._type() === 1) { if (n._matches(sel)) return n; n = n._parent(); } return null; };
P.matches = P.webkitMatchesSelector = P.msMatchesSelector = function (sel) { return this._type() === 1 && this._matches(String(sel)); };
['offsetWidth', 'offsetHeight', 'offsetTop', 'offsetLeft', 'clientTop', 'clientLeft', 'scrollTop', 'scrollLeft', 'scrollWidth', 'scrollHeight'].forEach(function (k) {
  acc(P, k, function () { return HAS(this, '__' + k) ? this['__' + k] : 0; }, function (v) { defn(this, '__' + k, +v || 0); });
});
acc(P, 'clientWidth', function () { var n = this._name(); return (n === 'html' || n === 'body') ? VW : 0; });
acc(P, 'clientHeight', function () { var n = this._name(); return (n === 'html' || n === 'body') ? VH : 0; });
acc(P, 'offsetParent', function () { return null; });
acc(P, 'isContentEditable', function () { return false; });
acc(P, 'contentEditable', function () { return 'inherit'; }, function () { });
acc(P, 'sheet', function () { return { cssRules: [], rules: [], insertRule: function () { return 0; }, deleteRule: function () { }, disabled: false }; });
acc(P, 'contentWindow', function () { return null; }); acc(P, 'contentDocument', function () { return null; });
acc(P, 'validity', function () { return { valid: true }; });
acc(P, 'files', function () { return []; });
acc(P, 'labels', function () { return []; });
acc(P, 'assignedSlot', function () { return null; });
acc(P, 'currentTime', function () { return HAS(this, '__ct') ? this.__ct : 0; }, function (v) { defn(this, '__ct', +v || 0); });
acc(P, 'duration', function () { return NaN; }); acc(P, 'paused', function () { return true; }); acc(P, 'volume', function () { return 1; }, function () { });

/* búsqueda */
P.querySelector = function (s) { var r = this._qsa(String(s), true); return r.length ? r[0] : null; };
P.querySelectorAll = function (s) { return mkList(this._qsa(String(s), false)); };
function cssq(v) { return '"' + String(v).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"'; }
P.getElementById = function (id) { var r = this._qsa('[id=' + cssq(id) + ']', true); return r.length ? r[0] : null; };
P.getElementsByTagName = function (t) { return mkList(this._qsa(String(t).toLowerCase() === '*' ? '*' : String(t).toLowerCase(), false)); };
P.getElementsByTagNameNS = function (ns, t) { return this.getElementsByTagName(t); };
P.getElementsByClassName = function (c) { var parts = String(c).trim().split(/\s+/).filter(Boolean); if (!parts.length) return mkList([]); return mkList(this._qsa(parts.map(function (x) { return '.' + x.replace(/[^\w-]/g, '\\$&'); }).join(''), false)); };
P.getElementsByName = function (n) { return mkList(this._qsa('[name=' + cssq(n) + ']', false)); };

/* classList */
function mkClassList(el) {
  var get = function () { return (el._attr('class') || '').split(/\s+/).filter(Boolean); };
  var set = function (a) { el._setAttr('class', a.join(' ')); };
  var o = {
    add: function () { var a = get(); [].forEach.call(arguments, function (c) { if (a.indexOf(c) < 0) a.push(c); }); set(a); },
    remove: function () { var a = get(), r = [].slice.call(arguments); set(a.filter(function (c) { return r.indexOf(c) < 0; })); },
    contains: function (c) { return get().indexOf(c) >= 0; },
    toggle: function (c, f) { var h = get().indexOf(c) >= 0; if (f === undefined) f = !h; if (f && !h) o.add(c); else if (!f && h) o.remove(c); return f; },
    replace: function (a, b) { var l = get(), i = l.indexOf(a); if (i < 0) return false; l[i] = b; set(l); return true; },
    item: function (i) { return get()[i] || null; },
    forEach: function (f) { get().forEach(f); },
    entries: function () { return get().entries(); }, keys: function () { return get().keys(); }, values: function () { return get()[Symbol.iterator](); },
    toString: function () { return el._attr('class') || ''; },
    supports: function () { return true; }
  };
  o[Symbol.iterator] = function () { return get()[Symbol.iterator](); };
  acc(o, 'length', function () { return get().length; });
  acc(o, 'value', function () { return el._attr('class') || ''; }, function (v) { el._setAttr('class', String(v)); });
  return o;
}
acc(P, 'classList', function () { return HAS(this, '__cl') ? this.__cl : (defn(this, '__cl', mkClassList(this)), this.__cl); });

/* style */
function parseStyle(s) {
  var m = new Map(), depth = 0, cur = '', q = '';
  var push = function () { var i = cur.indexOf(':'); if (i > 0) { var k = cur.slice(0, i).trim().toLowerCase(), v = cur.slice(i + 1).trim(); if (k && v) m.set(k, v); } cur = ''; };
  for (var i = 0; i < s.length; i++) {
    var c = s[i];
    if (q) { if (c === q) q = ''; } else if (c === '"' || c === "'") q = c;
    else if (c === '(') depth++; else if (c === ')') depth--;
    else if (c === ';' && depth <= 0) { push(); continue; }
    cur += c;
  }
  push(); return m;
}
function serStyle(m) { var a = []; m.forEach(function (v, k) { a.push(k + ': ' + v); }); return a.join('; ') + (a.length ? ';' : ''); }
function normProp(p) { p = String(p); if (p.indexOf('-') >= 0 || p === 'float') return p.toLowerCase(); if (p === 'cssFloat') return 'float'; p = kebab(p); if (/^(webkit|moz|ms)-/.test(p)) p = '-' + p; return p; }
function mkStyle(el) {
  var rd = function () { return parseStyle(el._attr('style') || ''); };
  var api = {
    getPropertyValue: function (p) { var v = rd().get(normProp(p)); return v === undefined ? '' : v.replace(/\s*!important$/, ''); },
    getPropertyPriority: function (p) { var v = rd().get(normProp(p)); return v && /!important$/.test(v) ? 'important' : ''; },
    setProperty: function (p, v, pr) { var m = rd(); p = normProp(p); if (v === '' || v == null) m.delete(p); else m.set(p, String(v) + (pr === 'important' ? ' !important' : '')); el._setAttr('style', serStyle(m)); },
    removeProperty: function (p) { var m = rd(); p = normProp(p); var o = m.get(p) || ''; m.delete(p); el._setAttr('style', serStyle(m)); return o; },
    item: function (i) { return Array.from(rd().keys())[i] || ''; }
  };
  acc(api, 'cssText', function () { return el._attr('style') || ''; }, function (v) { el._setAttr('style', String(v)); });
  acc(api, 'length', function () { return rd().size; });
  return new Proxy(api, {
    get: function (t, k) { if (typeof k === 'symbol') return undefined; if (k in t) return t[k]; return t.getPropertyValue(k); },
    set: function (t, k, v) { if (k === 'cssText') t.cssText = v; else t.setProperty(k, v); return true; },
    has: function (t, k) { return k in t; }
  });
}
acc(P, 'style', function () { return HAS(this, '__style') ? this.__style : (defn(this, '__style', mkStyle(this)), this.__style); }, function (v) { this._setAttr('style', String(v)); });

/* dataset */
function mkDataset(el) {
  return new Proxy({}, {
    get: function (t, k) { if (typeof k === 'symbol') return undefined; var v = el._attr('data-' + kebab(k)); return v === null ? undefined : v; },
    set: function (t, k, v) { el._setAttr('data-' + kebab(k), String(v)); return true; },
    has: function (t, k) { return el._attr('data-' + kebab(k)) !== null; },
    deleteProperty: function (t, k) { el._rmAttr('data-' + kebab(k)); return true; },
    ownKeys: function () { return el._attrNames().filter(function (n) { return n.indexOf('data-') === 0; }).map(function (n) { return camel(n.slice(5)); }); },
    getOwnPropertyDescriptor: function (t, k) { var v = typeof k === 'string' ? el._attr('data-' + kebab(k)) : null; return v === null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; }
  });
}
acc(P, 'dataset', function () { return HAS(this, '__ds') ? this.__ds : (defn(this, '__ds', mkDataset(this)), this.__ds); });

/* ---------- document ---------- */
var doc = __root();
g.document = doc;
function dacc(k, get, set) { Object.defineProperty(doc, k, { get: get, set: set, configurable: true, enumerable: false }); }
function child(p, name) { var c = p._first(); while (c) { if (c._type() === 1 && c._name() === name) return c; c = c._next(); } return null; }
function ensure(p, name) { var c = child(p, name); if (!c) { c = doc.createElement(name); p.appendChild(c); } return c; }
dacc('documentElement', function () { return child(doc, 'html'); });
dacc('head', function () { var h = child(doc, 'html'); return h ? child(h, 'head') : null; });
dacc('body', function () { var h = child(doc, 'html'); return h ? child(h, 'body') : null; }, function () { });
dacc('title', function () { var t = doc._qsa('title', true)[0]; return t ? t._text().replace(/\s+/g, ' ').trim() : ''; }, function (v) { var t = doc._qsa('title', true)[0]; if (!t) { var h = doc.head; if (!h) return; t = doc.createElement('title'); h.appendChild(t); } t._setText(String(v)); });
dacc('URL', function () { return location.href; }); dacc('documentURI', function () { return location.href; });
dacc('baseURI', function () { var b = doc._qsa('base[href]', true)[0]; return b ? __resolve(location.href, b._attr('href')) : location.href; });
dacc('referrer', function () { return ''; }); dacc('domain', function () { return location.hostname; });
dacc('characterSet', function () { return 'UTF-8'; }); dacc('charset', function () { return 'UTF-8'; }); dacc('inputEncoding', function () { return 'UTF-8'; });
dacc('contentType', function () { return 'text/html'; }); dacc('compatMode', function () { return 'CSS1Compat'; });
dacc('doctype', function () { return null; }); dacc('defaultView', function () { return g; });
dacc('hidden', function () { return false; }); dacc('visibilityState', function () { return 'visible'; });
dacc('activeElement', function () { return doc.__active || doc.body; });
dacc('scrollingElement', function () { return doc.documentElement; });
dacc('forms', function () { return mkList(doc._qsa('form', false)); }); dacc('images', function () { return mkList(doc._qsa('img', false)); });
dacc('links', function () { return mkList(doc._qsa('a[href],area[href]', false)); }); dacc('scripts', function () { return mkList(doc._qsa('script', false)); });
dacc('all', function () { return mkList(doc._qsa('*', false)); }); dacc('styleSheets', function () { return mkList([]); });
dacc('fonts', function () { return { ready: Promise.resolve(), load: function () { return Promise.resolve([]); }, check: function () { return true; }, add: function () { }, forEach: function () { }, addEventListener: function () { }, status: 'loaded' }; });
var cookieJar = {};
dacc('cookie', function () { return Object.keys(cookieJar).map(function (k) { return k + '=' + cookieJar[k]; }).join('; '); }, function (v) {
  var p = String(v).split(';')[0], i = p.indexOf('=');
  if (i < 0) return; var k = p.slice(0, i).trim(), val = p.slice(i + 1).trim();
  if (/expires=.*19(70|69)|max-age=0|max-age=-/i.test(String(v))) delete cookieJar[k]; else cookieJar[k] = val;
});
var readyState = 'loading';
dacc('readyState', function () { return readyState; });
defn(doc, 'createElement', function (n) { return __create(String(n)); });
defn(doc, 'createElementNS', function (ns, n) { var e = __create(String(n).replace(/^.*:/, '')); if (ns === 'http://www.w3.org/2000/svg') defn(e, '__svg', true); return e; });
defn(doc, 'createTextNode', function (s) { return __createText(String(s)); });
defn(doc, 'createComment', function (s) { var t = __createText(''); defn(t, '__comment', true); return t; });
defn(doc, 'createDocumentFragment', function () { return __create('#fragment'); });
defn(doc, 'createEvent', function (t) { return new Event(''); });
defn(doc, 'createRange', function () { return { setStart: function () { }, setEnd: function () { }, selectNodeContents: function () { }, selectNode: function () { }, collapse: function () { }, detach: function () { }, getBoundingClientRect: P.getBoundingClientRect, getClientRects: function () { return []; }, createContextualFragment: function (h) { var f = doc.createDocumentFragment(), d = doc.createElement('div'); d._setHtml(h); takeFragment(d).forEach(function (k) { f._insert(k, null); }); return f; }, commonAncestorContainer: doc, startContainer: doc, endContainer: doc, startOffset: 0, endOffset: 0, collapsed: true, cloneRange: function () { return doc.createRange(); } }; });
defn(doc, 'createTreeWalker', function (root) { var l = [root].concat(root._qsa('*', false)), i = 0; return { currentNode: root, nextNode: function () { i++; return i < l.length ? (this.currentNode = l[i]) : null; }, root: root }; });
defn(doc, 'createNodeIterator', function (root) { var l = [root].concat(root._qsa('*', false)), i = -1; return { nextNode: function () { i++; return i < l.length ? l[i] : null; }, root: root }; });
defn(doc, 'importNode', function (n, deep) { return n.cloneNode(!!deep); });
defn(doc, 'adoptNode', function (n) { return n; });
defn(doc, 'hasFocus', function () { return true; });
defn(doc, 'elementFromPoint', function () { return null; }); defn(doc, 'elementsFromPoint', function () { return []; });
defn(doc, 'execCommand', function () { return false; }); defn(doc, 'queryCommandSupported', function () { return false; });
defn(doc, 'getSelection', function () { return g.getSelection(); });
defn(doc, 'open', function () { return doc; }); defn(doc, 'close', function () { });
defn(doc, 'write', function () {
  var html = [].join.call(arguments, ''), tmp = doc.createElement('div'); tmp._setHtml(html);
  var cs = doc.currentScript, p = cs ? cs._parent() : doc.body, ref = cs ? cs._next() : null;
  if (!p) return;
  takeFragment(tmp).forEach(function (k) { insertOne(p, k, ref); });
});
defn(doc, 'writeln', function () { doc.write([].join.call(arguments, '') + '\n'); });
defn(doc, 'currentScript', null);
defn(doc, 'getElementById', function (id) { var r = doc._qsa('[id=' + cssq(id) + ']', true); return r.length ? r[0] : null; });
defn(doc, 'appendChild', function (n) { return insertOne(doc, n, null); });

defn(doc, 'implementation', {
  hasFeature: function () { return true; },
  createHTMLDocument: function (t) {
    var html = __create('html'), head = __create('head'), body = __create('body');
    html._insert(head, null); html._insert(body, null);
    var d = { nodeType: 9, documentElement: html, head: head, body: body, title: t || '', baseURI: doc.baseURI, location: g.location, defaultView: null, readyState: 'complete' };
    ['createElement', 'createElementNS', 'createTextNode', 'createComment', 'createDocumentFragment', 'createEvent', 'importNode', 'adoptNode', 'createRange'].forEach(function (k) { d[k] = doc[k]; });
    ['querySelector', 'querySelectorAll', 'getElementById', 'getElementsByTagName', 'getElementsByClassName'].forEach(function (k) { d[k] = function () { return html[k].apply(html, arguments); }; });
    d.appendChild = function (n) { return html.appendChild(n); };
    d.implementation = doc.implementation;
    return d;
  },
  createDocument: function () { return doc.implementation.createHTMLDocument(''); }
});

/* ---------- location / history / navigator ---------- */
var loc; try { loc = new URL(__pageurl); } catch (e) { loc = new URL('about:blank'); }
var locObj = {
  get href() { return loc.href; }, set href(v) { __navigate(String(v)); },
  get origin() { return loc.origin; }, get protocol() { return loc.protocol; }, get host() { return loc.host; }, get hostname() { return loc.hostname; },
  get port() { return loc.port; }, get pathname() { return loc.pathname; }, set pathname(v) { var u = new URL(loc.href); u.pathname = v; __navigate(u.href); },
  get search() { return loc.search; }, set search(v) { var u = new URL(loc.href); u.search = v; __navigate(u.href); },
  get hash() { return loc.hash; }, set hash(v) { var o = loc.href; loc.hash = v; fireHash(o); },
  assign: function (u) { __navigate(String(u)); }, replace: function (u) { __navigate(String(u)); }, reload: function () { __navigate(loc.href); },
  toString: function () { return loc.href; }, ancestorOrigins: []
};
function fireHash(old) { setTimeout(function () { var e = new HashChangeEvent('hashchange', { oldURL: old, newURL: loc.href }); dispatch(g, e); }, 0); }
Object.defineProperty(g, 'location', { get: function () { return locObj; }, set: function (v) { __navigate(String(v)); }, configurable: true });
Object.defineProperty(doc, 'location', { get: function () { return locObj; }, set: function (v) { __navigate(String(v)); }, configurable: true });
var hist = { state: null, length: 1, scrollRestoration: 'auto' };
hist.pushState = function (s, t, u) { hist.state = s; hist.length++; if (u !== undefined && u !== null) { try { loc = new URL(String(u), loc.href); } catch (e) { } } };
hist.replaceState = function (s, t, u) { hist.state = s; if (u !== undefined && u !== null) { try { loc = new URL(String(u), loc.href); } catch (e) { } } };
hist.back = hist.forward = hist.go = function () { };
g.history = hist;
g.navigator = {
  userAgent: 'Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Cosmos/0.5', appName: 'Netscape', appVersion: '5.0 (X11)', appCodeName: 'Mozilla', product: 'Gecko',
  vendor: 'Cosmos', platform: 'Linux x86_64', language: 'es-MX', languages: ['es-MX', 'es', 'en'], cookieEnabled: true, onLine: true, doNotTrack: '1',
  hardwareConcurrency: 2, deviceMemory: 4, maxTouchPoints: 0, webdriver: false, pdfViewerEnabled: false,
  sendBeacon: function () { return true; }, javaEnabled: function () { return false; },
  clipboard: { writeText: function () { return Promise.resolve(); }, readText: function () { return Promise.resolve(''); } },
  permissions: { query: function () { return Promise.resolve({ state: 'denied', addEventListener: function () { } }); } },
  mediaSession: { setActionHandler: function () { }, metadata: null, playbackState: 'none' },
  getBattery: function () { return Promise.reject(new Error('no')); },
  connection: { effectiveType: '4g', downlink: 10, rtt: 50, saveData: false, addEventListener: function () { } },
  userAgentData: undefined
};

/* ---------- ventana ---------- */
g.window = g.self = g.top = g.parent = g.frames = g.globalThis = g;
g.name = ''; g.closed = false; g.length = 0; g.opener = null; g.frameElement = null; g.origin = loc.origin;
g.innerWidth = VW; g.innerHeight = VH; g.outerWidth = VW; g.outerHeight = VH; g.devicePixelRatio = 1;
g.scrollX = g.scrollY = g.pageXOffset = g.pageYOffset = 0; g.screenX = g.screenY = g.screenLeft = g.screenTop = 0;
g.screen = { width: VW, height: VH, availWidth: VW, availHeight: VH, colorDepth: 24, pixelDepth: 24, orientation: { type: 'landscape-primary', angle: 0, addEventListener: function () { } } };
g.visualViewport = { width: VW, height: VH, scale: 1, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0, addEventListener: function () { }, removeEventListener: function () { } };
g.scrollTo = g.scroll = g.scrollBy = g.focus = g.blur = g.print = g.stop = g.moveTo = g.resizeTo = function () { };
g.close = function () { };
g.open = function () { return null; };
g.alert = function () { }; g.confirm = function () { return false; }; g.prompt = function () { return null; };
g.postMessage = function (data, origin) { setTimeout(function () { var e = new MessageEvent('message', { data: data, origin: loc.origin, source: g }); dispatch(g, e); }, 0); };
g.getSelection = function () { return { rangeCount: 0, isCollapsed: true, type: 'None', toString: function () { return ''; }, removeAllRanges: function () { }, addRange: function () { }, getRangeAt: function () { return doc.createRange(); }, collapse: function () { }, anchorNode: null, focusNode: null }; };
g.getComputedStyle = function (el) {
  var st = el && el._type && el._type() === 1 ? el.style : mkStyle(doc.createElement('div'));
  var inlineTags = /^(a|span|b|i|em|strong|small|label|code|img|input|button|select|textarea|sub|sup|abbr|u|s|br)$/;
  return new Proxy({}, {
    get: function (t, k) {
      if (typeof k === 'symbol') return undefined;
      if (k === 'getPropertyValue') return function (p) { return this[camel(p)]; };
      if (k === 'length') return 0;
      var v = st[k];
      if (v) return v;
      if (k === 'display') return el && el._name && inlineTags.test(el._name()) ? 'inline' : 'block';
      if (k === 'visibility') return 'visible'; if (k === 'position') return 'static'; if (k === 'opacity') return '1';
      if (k === 'overflow' || k === 'overflowX' || k === 'overflowY') return 'visible';
      if (k === 'fontSize') return '16px'; if (k === 'color') return 'rgb(0, 0, 0)'; if (k === 'direction') return 'ltr';
      return '';
    }
  });
};
g.matchMedia = function (q) {
  q = String(q).toLowerCase();
  var ok = function (c) {
    var m = /\(\s*([\w-]+)\s*(?::\s*([^)]*))?\)/.exec(c); if (!m) return c.trim() === 'all' || c.trim() === 'screen';
    var f = m[1], v = (m[2] || '').trim(), px = parseFloat(v);
    switch (f) {
      case 'min-width': return VW >= px; case 'max-width': return VW <= px; case 'min-height': return VH >= px; case 'max-height': return VH <= px;
      case 'width': return VW === px; case 'height': return VH === px;
      case 'orientation': return v === 'landscape' ? VW >= VH : VW < VH;
      case 'prefers-color-scheme': return v === 'light'; case 'prefers-reduced-motion': return v === 'no-preference';
      case 'hover': case 'any-hover': return v === 'hover'; case 'pointer': case 'any-pointer': return v === 'fine';
      case 'min-resolution': case 'min-device-pixel-ratio': case '-webkit-min-device-pixel-ratio': return px <= 1;
      case 'display-mode': return v === 'browser'; default: return false;
    }
  };
  var matches = q.split(',').some(function (grp) { var neg = /^\s*not\s/.test(grp); var r = grp.replace(/^\s*(not|only)\s+/, '').split(/\band\b/).every(ok); return neg ? !r : r; });
  return { matches: matches, media: q, onchange: null, addListener: function () { }, removeListener: function () { }, addEventListener: function () { }, removeEventListener: function () { }, dispatchEvent: function () { return true; } };
};
var tm0 = T0;
g.performance = {
  now: function () { return __now() - T0; }, timeOrigin: Date.now(), mark: function () { }, measure: function () { }, clearMarks: function () { }, clearMeasures: function () { },
  getEntries: function () { return []; }, getEntriesByType: function () { return []; }, getEntriesByName: function () { return []; },
  timing: { navigationStart: Date.now(), fetchStart: Date.now(), domainLookupStart: 0, connectStart: 0, responseStart: 0, domLoading: 0, domInteractive: 0, domComplete: 0, loadEventEnd: 0 },
  navigation: { type: 0, redirectCount: 0 }, toJSON: function () { return {}; }
};
g.crypto = {
  getRandomValues: function (a) { for (var i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 4294967296); return a; },
  randomUUID: function () { return 'xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx'.replace(/[xy]/g, function (c) { var r = Math.random() * 16 | 0; return (c === 'x' ? r : (r & 3 | 8)).toString(16); }); },
  subtle: {}
};
g.CSS = { supports: function () { return false; }, escape: function (s) { return String(s).replace(/[^\w-]/g, '\\$&'); } };
g.customElements = (function () {
  var reg = {};
  return { define: function (n, c) { reg[n] = c; }, get: function (n) { return reg[n]; }, whenDefined: function (n) { return Promise.resolve(reg[n]); }, upgrade: function () { } };
})();
function Obs(name, fire) {
  var C = function (cb) { defn(this, '_cb', cb); this.root = null; this.rootMargin = '0px'; this.thresholds = [0]; };
  C.prototype.observe = function (el) { var s = this; if (fire) setTimeout(function () { try { s._cb([fire(el)], s); } catch (e) { log('error', ['Uncaught (' + name + ')', e]); } }, 0); };
  C.prototype.unobserve = C.prototype.disconnect = function () { };
  C.prototype.takeRecords = function () { return []; };
  Object.defineProperty(C, 'name', { value: name });
  g[name] = C;
}
Obs('IntersectionObserver', function (el) { var r = el.getBoundingClientRect(); return { target: el, isIntersecting: true, intersectionRatio: 1, boundingClientRect: r, intersectionRect: r, rootBounds: null, time: __now() - T0 }; });
Obs('MutationObserver', null); Obs('ResizeObserver', null); Obs('PerformanceObserver', null); Obs('ReportingObserver', null);
g.PerformanceObserver.supportedEntryTypes = [];
g.DOMParser = function () { };
g.DOMParser.prototype.parseFromString = function (s) {
  var d = doc.createElement('html'); d._setHtml(String(s));
  var b = d._qsa('body', true)[0] || d;
  return { documentElement: d, body: b, head: d._qsa('head', true)[0] || null, title: '', querySelector: function (q) { return d.querySelector(q); }, querySelectorAll: function (q) { return d.querySelectorAll(q); }, getElementById: function (i) { return d.getElementById(i); }, getElementsByTagName: function (t) { return d.getElementsByTagName(t); }, nodeType: 9 };
};
g.XMLSerializer = function () { }; g.XMLSerializer.prototype.serializeToString = function (n) { return n._outer(); };
g.Image = function (w, h) { var i = doc.createElement('img'); if (w) i.setAttribute('width', w); if (h) i.setAttribute('height', h); return i; };
g.Option = function (t, v, d, s) { var o = doc.createElement('option'); if (t !== undefined) o._setText(String(t)); if (v !== undefined) o.setAttribute('value', v); if (s) o.setAttribute('selected', ''); return o; };
g.Audio = function (src) { var a = doc.createElement('audio'); if (src) a.setAttribute('src', src); return a; };
g.DOMException = g.DOMException || function (m, n) { this.message = m; this.name = n || 'Error'; };
g.reportError = function (e) { log('error', [e]); };
g.addEventListener = function (t, f, c) { addL(g, t, f, c); }; g.removeEventListener = function (t, f, c) { remL(g, t, f, c); }; g.dispatchEvent = function (ev) { return dispatch(g, ev); };
['atob', 'btoa'].forEach(function (k) { defn(g, k, g[k]); });

/* elementos con id como globales (acceso con nombre de window) */
(function () {
  var els = doc._qsa('[id]', false), n = 0;
  for (var i = 0; i < els.length && n < 3000; i++) {
    var id = els[i]._attr('id');
    if (!id || (id in g)) continue;
    (function (id) { Object.defineProperty(g, id, { get: function () { return doc.getElementById(id); }, set: function (v) { Object.defineProperty(g, id, { value: v, writable: true, configurable: true, enumerable: true }); }, configurable: true, enumerable: false }); })(id);
    n++;
  }
})();
/* los atributos de solo lectura no deben lanzar al asignarse (modo estricto) */
Object.getOwnPropertyNames(P).forEach(function (k) {
  var d = Object.getOwnPropertyDescriptor(P, k);
  if (d && d.get && !d.set && k !== 'constructor') acc(P, k, d.get, function () { });
});
acc(P, 'defaultValue', function () { var a = this._attr('value'); return a === null ? '' : a; }, function (v) { this._setAttr('value', String(v)); });
acc(P, 'defaultChecked', function () { return this._attr('checked') !== null; }, function (v) { if (v) this._setAttr('checked', ''); else this._rmAttr('checked'); });

/* ---------- ciclo de vida ---------- */
g.__dispatchNative = function (node, type) {
  var ev;
  if (/^(click|dblclick|mousedown|mouseup|mouseover|mouseout|mousemove|contextmenu)$/.test(type)) ev = new MouseEvent(type, { bubbles: true, cancelable: true });
  else if (/^key/.test(type)) ev = new KeyboardEvent(type, { bubbles: true, cancelable: true });
  else ev = new Event(type, { bubbles: true, cancelable: true });
  ev.isTrusted = true;
  dispatch(node, ev);
  return ev.defaultPrevented;
};
g.__loaded = function () {
  readyState = 'interactive';
  dispatch(doc, new Event('readystatechange'));
  dispatch(doc, new Event('DOMContentLoaded', { bubbles: true }));
  g.__advance(50);
  readyState = 'complete';
  dispatch(doc, new Event('readystatechange'));
  var le = new Event('load'); le.target = g; invoke(g, le, 2);
  dispatch(g, new PageTransitionEvent('pageshow', { persisted: false }));
  g.__advance(1200);
};
})(globalThis);
