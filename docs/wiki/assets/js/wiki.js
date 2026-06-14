document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("img.wiki-img").forEach((img) => {
    if (img.closest("a.wiki-img-link")) {
      return;
    }

    const link = document.createElement("a");
    link.className = "wiki-img-link";
    link.href = img.currentSrc || img.src;
    link.target = "_blank";
    link.rel = "noopener noreferrer";
    link.title = "Open full-size image";

    img.parentNode.insertBefore(link, img);
    link.appendChild(img);
  });
});
