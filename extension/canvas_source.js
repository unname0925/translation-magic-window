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
        // 這一頁的指紋標在 canvas 上：閱讀器回收 canvas 輪流畫不同頁（soraraw 的縦読み）時，
        // content.js 靠它認出是哪一頁（同一頁畫在別的 canvas 上也認得）
        const page = fingerprint(image);
        if (page) {
          target.setAttribute("data-tmw-page", page);
        } else {
          target.removeAttribute("data-tmw-page");
        }
        // 換了一頁：content.js 馬上讀走（canvas 可能很快又被拿去畫別頁）
        queueMicrotask(() => target.dispatchEvent(new CustomEvent("tmw-canvas-drawn")));
      }
    }
  }

  // 縮成 16×16 的像素加上原大小，算一個雜湊。來源也讀不到時回傳空字串
  const probe = document.createElement("canvas");
  probe.width = probe.height = 16;
  const probeContext = probe.getContext("2d", { willReadFrequently: true });
  function fingerprint(image) {
    try {
      probeContext.clearRect(0, 0, 16, 16);
      drawImage.call(probeContext, image, 0, 0, 16, 16);
      const data = probeContext.getImageData(0, 0, 16, 16).data;
      let hash = 0x811c9dc5;
      for (let i = 0; i < data.length; i += 1) {
        hash = Math.imul(hash ^ data[i], 0x01000193);
      }
      return `${image.width}x${image.height}-${(hash >>> 0).toString(36)}`;
    } catch {
      return "";
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

  // 「載入整頁」碰到左右翻頁的閱讀器（Swiper，例如 soraraw 的 1 頁／2 頁模式）：沒有東西可以捲，
  // 網站只載入目前這頁附近的圖。content.js 送出 tmw-flip-swiper 時，在這裡一頁一頁往後翻，每翻一次等
  // 那幾頁的圖載好（最多 2.5 秒；content.js 趁這時候讀走），翻到最後一頁再回原來那頁。
  // 結果寫在 <html data-tmw-flip>：翻了幾次，-1 是沒有 Swiper（content.js 改用往下捲）
  document.addEventListener("tmw-flip-swiper", async () => {
    const root = document.documentElement;
    const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
    const findSwiper = () => [...document.querySelectorAll(".swiper, .swiper-container")]
      .map((element) => element.swiper)
      .filter((swiper) => swiper && !swiper.destroyed && swiper.slides?.length > 1)
      .sort((a, b) => b.slides.length - a.slides.length)[0];
    // 換章時網頁剛載入完、閱讀器還沒建好：等一下（有 .swiper 元素才等，其他網站馬上回答）
    let swiper = findSwiper();
    for (let waited = 0; !swiper && waited < 8000 && document.querySelector(".swiper, .swiper-container");
         waited += 250) {
      await sleep(250);
      swiper = findSwiper();
    }
    if (!swiper) {
      root.setAttribute("data-tmw-flip", "-1");
      root.dispatchEvent(new CustomEvent("tmw-flip-done"));
      return;
    }
    const start = swiper.activeIndex;
    const url = location.href;
    const ready = () => {
      const perView = Math.max(1, Math.ceil(Number(swiper.params.slidesPerView) || 1));
      const slides = [...swiper.slides].slice(swiper.activeIndex, swiper.activeIndex + perView);
      return slides.every((slide) => {
        const image = [...slide.querySelectorAll("img")].find((img) => img.naturalWidth > 300);
        const canvas = [...slide.querySelectorAll("canvas")].find((c) => c.width > 300 && versions.get(c));
        return Boolean(image || canvas);
      });
    };
    let steps = 0;
    while (!swiper.isEnd && steps < 100 && location.href === url) {
      swiper.slideNext(0);
      steps += 1;
      for (let waited = 0; waited < 2500 && !ready(); waited += 150) {
        await sleep(150);
      }
      await sleep(400); // content.js 掃描、讀走這幾頁
    }
    if (location.href === url) {
      swiper.slideTo(start, 0);
    }
    root.setAttribute("data-tmw-flip", String(steps));
    root.dispatchEvent(new CustomEvent("tmw-flip-done"));
  }, true);
})();
