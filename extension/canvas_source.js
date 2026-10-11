// 在網頁自己的環境（MAIN world）執行：記下每張 canvas 是從哪一張「整頁」的 canvas 縮放畫過來的。
//
// 有些閱讀器（例如 soraraw）先把整頁畫在一張看不見的原尺寸 canvas 上，再縮小畫到畫面上的 canvas，
// 最後在畫面上的那張多畫一張別的網域的小圖，畫面上的 canvas 就讀不到了（被跨網域的圖汙染）。
// 原尺寸的那張常常沒被汙染：content.js 讀不到畫面上的 canvas 時，問這裡要它的來源，讀得到就用它翻譯，
// 不必等那一頁捲到畫面上截圖；閱讀器已經先畫好的後面幾頁也能先翻。
// 其他網站不受影響：沒有這種「整張畫過來」的來源、或來源也讀不到時，照舊直接讀或截圖。
(() => {
  if (window.__tmwCanvasSource) {
    return;
  }
  window.__tmwCanvasSource = true;
  document.documentElement.setAttribute("data-tmw-canvas-hook", ""); // 讓 content.js 知道可以問

  const sources = new WeakMap(); // 畫面上的 canvas → { source, version }
  const versions = new WeakMap(); // canvas → 被畫過幾次（來源後來又被畫上別的東西就不算數）
  const proto = CanvasRenderingContext2D.prototype;
  const drawImage = proto.drawImage;

  proto.drawImage = function (image, ...args) {
    try {
      remember(this.canvas, image, args);
    } catch {
      // 記不到就算了，畫照樣畫
    }
    return drawImage.call(this, image, ...args);
  };

  function remember(target, image, args) {
    if (!(target instanceof HTMLCanvasElement)) {
      return;
    }
    versions.set(target, (versions.get(target) || 0) + 1);
    if (!(image instanceof HTMLCanvasElement) || image === target || image.width < 64 || image.height < 64) {
      return;
    }
    // 整張來源畫到整個目標上：drawImage(image, dx, dy[, dw, dh]) 或 (image, 0, 0, 全寬, 全高, dx, dy, dw, dh)
    let dx, dy, dw, dh;
    if (args.length === 8) {
      const [sx, sy, sw, sh] = args;
      if (sx > 1 || sy > 1 || sw < image.width - 1 || sh < image.height - 1) {
        return; // 只畫來源的一部分（拼圖的一塊）：不算
      }
      [dx, dy, dw, dh] = args.slice(4);
    } else if (args.length === 4) {
      [dx, dy, dw, dh] = args;
    } else {
      [dx, dy] = args;
      [dw, dh] = [image.width, image.height];
    }
    if (dx <= 1 && dy <= 1 && dx + dw >= target.width - 1 && dy + dh >= target.height - 1) {
      const known = sources.get(target);
      const version = versions.get(image) || 0;
      if (known?.source !== image || known.version !== version) {
        sources.set(target, { source: image, version });
        // 換了一頁：content.js 等著的話（不在畫面上、截不到）現在就能翻
        queueMicrotask(() => target.dispatchEvent(new CustomEvent("tmw-canvas-drawn")));
      }
    }
  }

  // content.js 在畫面上的 canvas 送出 tmw-canvas-source；這裡把來源轉成 blob: 網址寫在
  // data-tmw-source，再送回 tmw-canvas-source-ready（沒有來源、或來源也讀不到時沒有網址）
  document.addEventListener("tmw-canvas-source", (event) => {
    const target = event.target;
    if (!(target instanceof HTMLCanvasElement)) {
      return;
    }
    const reply = (url) => {
      if (url) {
        target.setAttribute("data-tmw-source", url);
        setTimeout(() => URL.revokeObjectURL(url), 60_000);
      }
      target.dispatchEvent(new CustomEvent("tmw-canvas-source-ready"));
    };
    const known = sources.get(target);
    if (!known || (versions.get(known.source) || 0) !== known.version) {
      reply(""); // 沒有記到，或來源後來被畫上了別的東西
      return;
    }
    try {
      known.source.toBlob((blob) => reply(blob ? URL.createObjectURL(blob) : ""), "image/jpeg", 0.95);
    } catch {
      reply(""); // 來源也被汙染了
    }
  }, true);
})();
