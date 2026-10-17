/* Cosmos: salta los anuncios de video de YouTube (silencia, acelera y salta) y oculta los huecos de anuncios.
   Puede dejar de funcionar cuando YouTube cambie su página. */
(function () {
  var css = "ytd-ad-slot-renderer,ytd-in-feed-ad-layout-renderer,ytd-banner-promo-renderer,ytd-statement-banner-renderer," +
            "#player-ads,#masthead-ad,ytd-display-ad-renderer,ytd-promoted-sparkles-web-renderer,ytd-promoted-video-renderer," +
            "ytd-player-legacy-desktop-watch-ads-renderer,.ytp-ad-overlay-container,.ytp-ad-image-overlay{display:none!important}";
  function addStyle() {
    if (document.getElementById("cosmos-adcss")) return;
    var s = document.createElement("style"); s.id = "cosmos-adcss"; s.textContent = css;
    (document.head || document.documentElement).appendChild(s);
  }
  function tick() {
    addStyle();
    var p = document.querySelector(".html5-video-player"); if (!p) return;
    var ad = p.classList.contains("ad-showing") || p.classList.contains("ad-interrupting");
    var v = p.querySelector("video.html5-main-video") || document.querySelector("video");
    var skip = document.querySelector(".ytp-ad-skip-button,.ytp-ad-skip-button-modern,.ytp-skip-ad-button,.ytp-ad-skip-button-container button");
    if (skip) skip.click();
    var x = document.querySelector(".ytp-ad-overlay-close-button"); if (x) x.click();
    if (ad && v) {
      if (!v.__cm) v.__cm = { m: v.muted, r: v.playbackRate };
      v.muted = true; v.playbackRate = 16;
      if (isFinite(v.duration) && v.duration > 0 && v.currentTime < v.duration - 0.2) { try { v.currentTime = v.duration - 0.1; } catch (e) {} }
    } else if (v && v.__cm) { v.muted = v.__cm.m; v.playbackRate = v.__cm.r; v.__cm = null; }
  }
  setInterval(tick, 300);
})();
