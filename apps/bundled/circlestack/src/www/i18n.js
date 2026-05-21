const CS_I18N = {
  en: {
    "app.title": "Circle Stack",
    "app.tagline": "Moments become circles. Circles become memories.",
    "compose.placeholder": "Share a moment...",
    "compose.mediaPath": "/Photos/... media path",
    "compose.post": "Post",
    "feed.empty": "No moments yet."
  },
  fi: {
    "app.title": "Circle Stack",
    "app.tagline": "Hetkistä syntyy piirejä. Piireistä syntyy muistoja.",
    "compose.placeholder": "Jaa hetki...",
    "compose.mediaPath": "/Photos/... mediapolku",
    "compose.post": "Julkaise",
    "feed.empty": "Ei hetkiä vielä."
  }
};

function csLang() {
  const lang = (navigator.language || "en").toLowerCase();
  return lang.startsWith("fi") ? "fi" : "en";
}

function csT(key) {
  const lang = csLang();
  return (CS_I18N[lang] && CS_I18N[lang][key]) || CS_I18N.en[key] || key;
}

async function csApplyI18n() {
  document.querySelectorAll("[data-i18n]").forEach(el => {
    el.textContent = csT(el.getAttribute("data-i18n"));
  });

  document.querySelectorAll("[data-i18n-placeholder]").forEach(el => {
    el.placeholder = csT(el.getAttribute("data-i18n-placeholder"));
  });
}
