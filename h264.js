/* Cosmos: oculta VP8/VP9/AV1 a YouTube para que mande H.264 (avc1), que la GPU/CPU vieja decodifica mejor.
   (Mismo truco que la extensión h264ify.) YouTube no sirve H.265, así que H.264 es lo que se puede forzar. */
(function () {
  var bad = /vp0?[89]|vp08|vp09|av01|av1/i;
  try {
    if (window.MediaSource && MediaSource.isTypeSupported) {
      var ims = MediaSource.isTypeSupported.bind(MediaSource);
      MediaSource.isTypeSupported = function (t) { return bad.test(String(t)) ? false : ims(t); };
    }
    var cpt = HTMLMediaElement.prototype.canPlayType;
    HTMLMediaElement.prototype.canPlayType = function (t) { return bad.test(String(t)) ? "" : cpt.call(this, t); };
    var mc = navigator.mediaCapabilities;
    if (mc && mc.decodingInfo) {
      var di = mc.decodingInfo.bind(mc);
      mc.decodingInfo = function (c) {
        var s = ""; try { s = JSON.stringify(c); } catch (e) {}
        return bad.test(s) ? Promise.resolve({ supported: false, smooth: false, powerEfficient: false }) : di(c);
      };
    }
    window.__cosmosH264 = true;
  } catch (e) {}
})();
