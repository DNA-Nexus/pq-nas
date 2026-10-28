/* Reel Stack 1.0.3: application-bundled context-menu translations.
 * The server still owns the user's language selection. Only these menu labels
 * are bundled here, so an older server dictionary cannot leave them in English.
 * No API calls, settings writes, or changes to other apps' translations.
 */
(function (root, factory) {
  "use strict";
  const translations = factory();
  if (typeof module === "object" && module.exports) module.exports = translations;
  if (root) root.PQNAS_REELSTACK_CONTEXT_I18N = translations;
})(typeof window !== "undefined" ? window : null, function () {
  "use strict";
  const dictionaries = {
  "en": {
    "reelstack.video": "Video",
    "reelstack.play": "Play",
    "reelstack.context.edit_metadata": "Edit metadata",
    "reelstack.rename": "Rename",
    "reelstack.share_link": "Share link",
    "common.download": "Download",
    "common.delete": "Delete",
    "reelstack.context.upload_video": "Upload video",
    "reelstack.refresh_index": "Refresh index",
    "reelstack.view": "View",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "All videos",
    "reelstack.view.folders": "By folder",
    "reelstack.view.recent_added": "Recently added",
    "reelstack.view.recent_watched": "Recently watched",
    "reelstack.view.favorites": "Favorites",
    "reelstack.view.unrated": "Unrated",
    "reelstack.view.missing_thumbnails": "Missing thumbnails",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Space"
  },
  "fi": {
    "reelstack.video": "Video",
    "reelstack.play": "Toista",
    "reelstack.context.edit_metadata": "Muokkaa metatietoja",
    "reelstack.rename": "Nimeä uudelleen",
    "reelstack.share_link": "Jaa linkki",
    "common.download": "Lataa",
    "common.delete": "Poista",
    "reelstack.context.upload_video": "Lähetä video",
    "reelstack.refresh_index": "Päivitä indeksi",
    "reelstack.view": "Näkymä",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Kaikki videot",
    "reelstack.view.folders": "Kansioittain",
    "reelstack.view.recent_added": "Viimeksi lisätyt",
    "reelstack.view.recent_watched": "Viimeksi katsotut",
    "reelstack.view.favorites": "Suosikit",
    "reelstack.view.unrated": "Arvioimattomat",
    "reelstack.view.missing_thumbnails": "Puuttuvat pikkukuvat",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Välilyönti"
  },
  "zh": {
    "reelstack.video": "视频",
    "reelstack.play": "播放",
    "reelstack.context.edit_metadata": "编辑元数据",
    "reelstack.rename": "重命名",
    "reelstack.share_link": "分享链接",
    "common.download": "下载",
    "common.delete": "删除",
    "reelstack.context.upload_video": "上传视频",
    "reelstack.refresh_index": "刷新索引",
    "reelstack.view": "视图",
    "reelstack.context.view_mode": "{view}：{mode}",
    "reelstack.view.all": "所有视频",
    "reelstack.view.folders": "按文件夹",
    "reelstack.view.recent_added": "最近添加",
    "reelstack.view.recent_watched": "最近观看",
    "reelstack.view.favorites": "收藏",
    "reelstack.view.unrated": "未评分",
    "reelstack.view.missing_thumbnails": "缺少缩略图",
    "reelstack.context.key_enter": "回车",
    "reelstack.context.key_space": "空格"
  },
  "sv": {
    "reelstack.video": "Video",
    "reelstack.play": "Spela upp",
    "reelstack.context.edit_metadata": "Redigera metadata",
    "reelstack.rename": "Byt namn",
    "reelstack.share_link": "Dela länk",
    "common.download": "Ladda ner",
    "common.delete": "Ta bort",
    "reelstack.context.upload_video": "Ladda upp video",
    "reelstack.refresh_index": "Uppdatera index",
    "reelstack.view": "Visa",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Alla videor",
    "reelstack.view.folders": "Efter mapp",
    "reelstack.view.recent_added": "Nyligen tillagda",
    "reelstack.view.recent_watched": "Nyligen visade",
    "reelstack.view.favorites": "Favoriter",
    "reelstack.view.unrated": "Utan betyg",
    "reelstack.view.missing_thumbnails": "Saknade miniatyrbilder",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Mellanslag"
  },
  "uk": {
    "reelstack.video": "Відео",
    "reelstack.play": "Відтворити",
    "reelstack.context.edit_metadata": "Редагувати метадані",
    "reelstack.rename": "Перейменувати",
    "reelstack.share_link": "Поділитися посиланням",
    "common.download": "Завантажити",
    "common.delete": "Видалити",
    "reelstack.context.upload_video": "Вивантажити відео",
    "reelstack.refresh_index": "Оновити індекс",
    "reelstack.view": "Перегляд",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Усі відео",
    "reelstack.view.folders": "За папками",
    "reelstack.view.recent_added": "Нещодавно додані",
    "reelstack.view.recent_watched": "Нещодавно переглянуті",
    "reelstack.view.favorites": "Улюблені",
    "reelstack.view.unrated": "Без оцінки",
    "reelstack.view.missing_thumbnails": "Без мініатюр",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Пробіл"
  },
  "de": {
    "reelstack.video": "Video",
    "reelstack.play": "Abspielen",
    "reelstack.context.edit_metadata": "Metadaten bearbeiten",
    "reelstack.rename": "Umbenennen",
    "reelstack.share_link": "Link teilen",
    "common.download": "Herunterladen",
    "common.delete": "Löschen",
    "reelstack.context.upload_video": "Video hochladen",
    "reelstack.refresh_index": "Index aktualisieren",
    "reelstack.view": "Ansicht",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Alle Videos",
    "reelstack.view.folders": "Nach Ordner",
    "reelstack.view.recent_added": "Kürzlich hinzugefügt",
    "reelstack.view.recent_watched": "Kürzlich angesehen",
    "reelstack.view.favorites": "Favoriten",
    "reelstack.view.unrated": "Unbewertet",
    "reelstack.view.missing_thumbnails": "Fehlende Vorschaubilder",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Leertaste"
  },
  "et": {
    "reelstack.video": "Video",
    "reelstack.play": "Esita",
    "reelstack.context.edit_metadata": "Muuda metaandmeid",
    "reelstack.rename": "Nimeta ümber",
    "reelstack.share_link": "Jaga linki",
    "common.download": "Laadi alla",
    "common.delete": "Kustuta",
    "reelstack.context.upload_video": "Laadi video üles",
    "reelstack.refresh_index": "Värskenda indeksit",
    "reelstack.view": "Vaade",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Kõik videod",
    "reelstack.view.folders": "Kaustade kaupa",
    "reelstack.view.recent_added": "Hiljuti lisatud",
    "reelstack.view.recent_watched": "Hiljuti vaadatud",
    "reelstack.view.favorites": "Lemmikud",
    "reelstack.view.unrated": "Hindamata",
    "reelstack.view.missing_thumbnails": "Puuduvad pisipildid",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Tühik"
  },
  "pl": {
    "reelstack.video": "Wideo",
    "reelstack.play": "Odtwórz",
    "reelstack.context.edit_metadata": "Edytuj metadane",
    "reelstack.rename": "Zmień nazwę",
    "reelstack.share_link": "Udostępnij link",
    "common.download": "Pobierz",
    "common.delete": "Usuń",
    "reelstack.context.upload_video": "Prześlij wideo",
    "reelstack.refresh_index": "Odśwież indeks",
    "reelstack.view": "Widok",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Wszystkie filmy",
    "reelstack.view.folders": "Według folderów",
    "reelstack.view.recent_added": "Ostatnio dodane",
    "reelstack.view.recent_watched": "Ostatnio oglądane",
    "reelstack.view.favorites": "Ulubione",
    "reelstack.view.unrated": "Bez oceny",
    "reelstack.view.missing_thumbnails": "Brak miniatur",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Spacja"
  },
  "es": {
    "reelstack.video": "Vídeo",
    "reelstack.play": "Reproducir",
    "reelstack.context.edit_metadata": "Editar metadatos",
    "reelstack.rename": "Cambiar nombre",
    "reelstack.share_link": "Compartir enlace",
    "common.download": "Descargar",
    "common.delete": "Eliminar",
    "reelstack.context.upload_video": "Subir vídeo",
    "reelstack.refresh_index": "Actualizar índice",
    "reelstack.view": "Vista",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Todos los vídeos",
    "reelstack.view.folders": "Por carpeta",
    "reelstack.view.recent_added": "Añadidos recientemente",
    "reelstack.view.recent_watched": "Vistos recientemente",
    "reelstack.view.favorites": "Favoritos",
    "reelstack.view.unrated": "Sin valorar",
    "reelstack.view.missing_thumbnails": "Sin miniaturas",
    "reelstack.context.key_enter": "Intro",
    "reelstack.context.key_space": "Espacio"
  },
  "fr": {
    "reelstack.video": "Vidéo",
    "reelstack.play": "Lire",
    "reelstack.context.edit_metadata": "Modifier les métadonnées",
    "reelstack.rename": "Renommer",
    "reelstack.share_link": "Partager le lien",
    "common.download": "Télécharger",
    "common.delete": "Supprimer",
    "reelstack.context.upload_video": "Envoyer une vidéo",
    "reelstack.refresh_index": "Actualiser l’index",
    "reelstack.view": "Affichage",
    "reelstack.context.view_mode": "{view} : {mode}",
    "reelstack.view.all": "Toutes les vidéos",
    "reelstack.view.folders": "Par dossier",
    "reelstack.view.recent_added": "Ajoutées récemment",
    "reelstack.view.recent_watched": "Vues récemment",
    "reelstack.view.favorites": "Favoris",
    "reelstack.view.unrated": "Non notées",
    "reelstack.view.missing_thumbnails": "Miniatures manquantes",
    "reelstack.context.key_enter": "Entrée",
    "reelstack.context.key_space": "Espace"
  },
  "it": {
    "reelstack.video": "Video",
    "reelstack.play": "Riproduci",
    "reelstack.context.edit_metadata": "Modifica metadati",
    "reelstack.rename": "Rinomina",
    "reelstack.share_link": "Condividi link",
    "common.download": "Scarica",
    "common.delete": "Elimina",
    "reelstack.context.upload_video": "Carica video",
    "reelstack.refresh_index": "Aggiorna indice",
    "reelstack.view": "Vista",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Tutti i video",
    "reelstack.view.folders": "Per cartella",
    "reelstack.view.recent_added": "Aggiunti di recente",
    "reelstack.view.recent_watched": "Visti di recente",
    "reelstack.view.favorites": "Preferiti",
    "reelstack.view.unrated": "Senza valutazione",
    "reelstack.view.missing_thumbnails": "Miniature mancanti",
    "reelstack.context.key_enter": "Invio",
    "reelstack.context.key_space": "Spazio"
  },
  "tr": {
    "reelstack.video": "Video",
    "reelstack.play": "Oynat",
    "reelstack.context.edit_metadata": "Meta verileri düzenle",
    "reelstack.rename": "Yeniden adlandır",
    "reelstack.share_link": "Bağlantıyı paylaş",
    "common.download": "İndir",
    "common.delete": "Sil",
    "reelstack.context.upload_video": "Video yükle",
    "reelstack.refresh_index": "Dizini yenile",
    "reelstack.view": "Görünüm",
    "reelstack.context.view_mode": "{view}: {mode}",
    "reelstack.view.all": "Tüm videolar",
    "reelstack.view.folders": "Klasöre göre",
    "reelstack.view.recent_added": "Son eklenenler",
    "reelstack.view.recent_watched": "Son izlenenler",
    "reelstack.view.favorites": "Favoriler",
    "reelstack.view.unrated": "Puanlanmamış",
    "reelstack.view.missing_thumbnails": "Küçük resmi eksik olanlar",
    "reelstack.context.key_enter": "Enter",
    "reelstack.context.key_space": "Boşluk"
  }
};
  for (const dictionary of Object.values(dictionaries)) Object.freeze(dictionary);
  Object.freeze(dictionaries);
  const languages = Object.freeze(Object.keys(dictionaries));
  const hasOwn = (value, key) => Object.prototype.hasOwnProperty.call(value, key);

  function normalizeLanguage(value) {
    const raw = String(value || "").trim().toLowerCase().replace(/_/g, "-");
    const base = raw.split("-")[0];
    return hasOwn(dictionaries, raw) ? raw : hasOwn(dictionaries, base) ? base : "en";
  }

  function selectedLanguage(host) {
    const w = host || (typeof window !== "undefined" ? window : null);
    try {
      const value = w && w.PQNAS_I18N && typeof w.PQNAS_I18N.getLanguage === "function"
        ? w.PQNAS_I18N.getLanguage() : "";
      if (typeof value === "string" && value.trim()) return normalizeLanguage(value);
    } catch (_) {}
    try {
      const value = w && w.document && w.document.documentElement.lang;
      if (typeof value === "string" && value.trim()) return normalizeLanguage(value);
    } catch (_) {}
    try {
      const value = w && w.localStorage && w.localStorage.getItem("pqnas_lang");
      if (value) return normalizeLanguage(value);
    } catch (_) {}
    return "en";
  }

  function hasKey(key) {
    return typeof key === "string" && hasOwn(dictionaries.en, key);
  }

  function text(lang, key, params, fallback) {
    const dictionary = dictionaries[normalizeLanguage(lang)];
    const template = hasKey(key) ? dictionary[key] : String(fallback == null ? key || "" : fallback);
    const values = params && typeof params === "object" ? params : {};
    // One pass preserves literal '$', HTML-like input, and nested placeholders.
    // Callers render with textContent, never innerHTML.
    return template.replace(/\{([A-Za-z][A-Za-z0-9_]*)\}/g, (match, name) =>
      hasOwn(values, name) ? String(values[name] == null ? "" : values[name]) : match
    );
  }

  function t(key, params, fallback) {
    return text(selectedLanguage(), key, params, fallback);
  }

  return Object.freeze({ languages, dictionaries, normalizeLanguage, selectedLanguage, hasKey, text, t });
});
