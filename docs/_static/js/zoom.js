/*
 * Click-to-zoom for the docs' own figures (architecture/sequence/FSM
 * diagrams rendered from tikz), generalizing the pan/scroll-to-zoom
 * already available on Mermaid diagrams (mermaid_d3_zoom, conf.py) to
 * every other <img> the docs embed via `.. figure::`.
 *
 * Mermaid diagrams render as inline <svg>, not <img>, and already ship
 * their own in-place zoom -- this script only ever wires plain <img>
 * elements, so the two never compete for the same wheel/drag events.
 */
(function () {
  "use strict";

  function buildOverlay() {
    var overlay = document.createElement("div");
    overlay.className = "pgaf-zoom-overlay";
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    overlay.innerHTML =
      '<button type="button" class="pgaf-zoom-close" aria-label="Close">×</button>' +
      '<div class="pgaf-zoom-viewport">' +
      '<img class="pgaf-zoom-img" alt="" draggable="false" />' +
      "</div>" +
      '<div class="pgaf-zoom-hint">scroll to zoom &middot; drag to pan &middot; ' +
      "double-click to reset &middot; Esc to close</div>";
    document.body.appendChild(overlay);

    var viewport = overlay.querySelector(".pgaf-zoom-viewport");
    var img = overlay.querySelector(".pgaf-zoom-img");
    var closeBtn = overlay.querySelector(".pgaf-zoom-close");

    var scale = 1;
    var panX = 0;
    var panY = 0;
    var dragging = false;
    var startX = 0;
    var startY = 0;
    var startPanX = 0;
    var startPanY = 0;
    var lastFocused = null;

    function applyTransform() {
      img.style.transform =
        "translate(" + panX + "px, " + panY + "px) scale(" + scale + ")";
    }

    function reset() {
      scale = 1;
      panX = 0;
      panY = 0;
      applyTransform();
    }

    function isOpen() {
      return overlay.classList.contains("pgaf-zoom-open");
    }

    function open(src, alt) {
      lastFocused = document.activeElement;
      img.src = src;
      img.alt = alt || "";
      reset();
      overlay.classList.add("pgaf-zoom-open");
      document.documentElement.classList.add("pgaf-zoom-locked");
      closeBtn.focus();
    }

    function close() {
      overlay.classList.remove("pgaf-zoom-open");
      document.documentElement.classList.remove("pgaf-zoom-locked");
      img.removeAttribute("src");
      if (lastFocused && typeof lastFocused.focus === "function") {
        lastFocused.focus();
      }
    }

    closeBtn.addEventListener("click", close);

    overlay.addEventListener("click", function (event) {
      if (event.target === overlay) {
        close();
      }
    });

    document.addEventListener("keydown", function (event) {
      if (event.key === "Escape" && isOpen()) {
        close();
      }
    });

    viewport.addEventListener(
      "wheel",
      function (event) {
        if (!isOpen()) {
          return;
        }
        event.preventDefault();
        var factor = event.deltaY < 0 ? 1.15 : 1 / 1.15;
        scale = Math.min(8, Math.max(0.5, scale * factor));
        applyTransform();
      },
      { passive: false }
    );

    viewport.addEventListener("dblclick", function (event) {
      event.preventDefault();
      reset();
    });

    viewport.addEventListener("mousedown", function (event) {
      dragging = true;
      startX = event.clientX;
      startY = event.clientY;
      startPanX = panX;
      startPanY = panY;
      viewport.classList.add("pgaf-zoom-dragging");
      event.preventDefault();
    });

    window.addEventListener("mousemove", function (event) {
      if (!dragging) {
        return;
      }
      panX = startPanX + (event.clientX - startX);
      panY = startPanY + (event.clientY - startY);
      applyTransform();
    });

    window.addEventListener("mouseup", function () {
      dragging = false;
      viewport.classList.remove("pgaf-zoom-dragging");
    });

    return { open: open, close: close };
  }

  function isZoomable(img) {
    if (img.closest(".pgaf-zoom-overlay")) {
      return false;
    }
    if (img.classList.contains("no-zoom")) {
      return false;
    }
    return !!img.closest("figure");
  }

  function wireImages(zoom) {
    var images = document.querySelectorAll("figure img");
    images.forEach(function (img) {
      if (!isZoomable(img) || img.dataset.pgafZoomWired) {
        return;
      }
      img.dataset.pgafZoomWired = "1";
      img.classList.add("pgaf-zoomable-img");
      img.tabIndex = 0;
      img.setAttribute("role", "button");
      img.setAttribute("aria-label", "Click to zoom: " + (img.alt || "image"));
      img.addEventListener("click", function () {
        zoom.open(img.currentSrc || img.src, img.alt);
      });
      img.addEventListener("keydown", function (event) {
        if (event.key === "Enter" || event.key === " ") {
          event.preventDefault();
          zoom.open(img.currentSrc || img.src, img.alt);
        }
      });
    });
  }

  document.addEventListener("DOMContentLoaded", function () {
    var zoom = buildOverlay();
    wireImages(zoom);
  });
})();
