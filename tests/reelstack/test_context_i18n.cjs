"use strict";
const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const ROOT = path.resolve(__dirname, "../..");
const WWW = path.join(ROOT, "apps/bundled/reelstack/src/www");
const I18N = fs.readFileSync(path.join(WWW, "reelstack_context_i18n.js"), "utf8");
const SOURCE = fs.readFileSync(path.join(WWW, "reelstack_context_menu.js"), "utf8");
const translations = require(path.join(WWW, "reelstack_context_i18n.js"));
const LANGS = ["en", "fi", "zh", "sv", "uk", "de", "et", "pl", "es", "fr", "it", "tr"];
const KEYS = Object.keys(translations.dictionaries.en);
const itemKeys = ["reelstack.play", "reelstack.context.edit_metadata", "reelstack.rename", "reelstack.share_link", "common.download", "common.delete"];
const viewModes = ["all", "folders", "recent_added", "recent_watched", "favorites", "unrated", "missing_thumbnails"];
const placeholders = text => Array.from(String(text).matchAll(/\{([A-Za-z][A-Za-z0-9_]*)\}/g), match => match[1]).sort();

for (const lang of LANGS) {
  test(`${lang}: all 20 menu translations and placeholders are present`, () => {
    const d = translations.dictionaries[lang];
    assert.deepEqual(Object.keys(d), KEYS);
    assert.equal(KEYS.length, 20);
    for (const key of KEYS) {
      assert.equal(typeof d[key], "string");
      assert.ok(d[key].trim(), key);
      assert.deepEqual(placeholders(d[key]), placeholders(translations.dictionaries.en[key]), key);
      assert.equal(translations.text(lang, key), d[key], key);
    }
  });
}

test("language set matches the actual server language registry", () => {
  const source = fs.readFileSync(path.join(ROOT, "server/src/static/i18n.js"), "utf8");
  const match = source.match(/const\s+LANGS\s*=\s*new Set\((\[[\s\S]*?\])\)/);
  assert.ok(match, "server language registry not found");
  assert.deepEqual(JSON.parse(match[1]).sort(), [...LANGS].sort());
  assert.deepEqual([...translations.languages].sort(), [...LANGS].sort());
});
test("regional tags and unknown languages are normalized safely", () => {
  for (const [input, expected] of [[" FI_fi ", "fi"], ["zh-Hant", "zh"], ["sv-SE", "sv"], ["uk-UA", "uk"], ["de_DE", "de"], ["es-MX", "es"], ["xx", "en"], ["constructor", "en"], ["__proto__", "en"]]) {
    assert.equal(translations.normalizeLanguage(input), expected);
  }
});
test("only known keys use the app dictionary; fallbacks remain plain text", () => {
  assert.equal(translations.hasKey("common.delete"), true);
  assert.equal(translations.hasKey("common.close"), false);
  assert.equal(translations.hasKey("__proto__"), false);
  assert.equal(translations.hasKey("constructor"), false);
  assert.equal(translations.text("fi", "unknown", null, "Fallback"), "Fallback");
  assert.equal(translations.text("fi", "reelstack.context.view_mode", { view: "$&<b>", mode: "{view}" }), "$&<b>: {view}");
});
test("language is read from the shell API, then document, then saved preference", () => {
  assert.equal(translations.selectedLanguage({PQNAS_I18N:{getLanguage:()=>"sv-SE"},document:{documentElement:{lang:"fi"}}}), "sv");
  assert.equal(translations.selectedLanguage({PQNAS_I18N:{getLanguage:()=>{throw Error("not ready");}},document:{documentElement:{lang:"fi-FI"}}}), "fi");
  assert.equal(translations.selectedLanguage({localStorage:{getItem:()=>"pl"}}), "pl");
  assert.equal(translations.selectedLanguage({}), "en");
});

// Execute the production menu sections, not a rewritten renderer. Upload,
// playback, sharing, delete and network boundaries are mocked; no real media
// or API mutation occurs in these tests.
function section(start, end) {
  const a = SOURCE.indexOf(start);
  const b = SOURCE.indexOf(end, a + start.length);
  assert.ok(a >= 0 && b > a, `missing source region: ${start}`);
  return SOURCE.slice(a, b);
}
const menuSource =
  'let api=null; let menu=null; let installed=false;\n' +
  section("  function reelT(", "  function cleanRelPath(") +
  section("  // Rebuild only an already-open menu", "  const reelStackUploadApi = Object.freeze(") +
  '\nwindow.__test = { install, renderMenu, renderAppMenu, placeMenu, hideMenu, reelT, onContextMenu, onKeydown };';

function harness(lang = "fi", { globalI18n = true, dictionary = () => "STALE ENGLISH", ready = null } = {}) {
  const calls = [];
  const observers = [];
  const windows = new Map(), events = new Map();
  const doc = { activeElement:null };
  class Element {
    constructor(tag) {
      this.tagName = tag.toUpperCase(); this.children=[]; this.parentNode=null;
      this.dataset={}; this.attrs={}; this.style={}; this.hidden=false; this.className=""; this.listeners=new Map(); this._text="";
      this.classList={add:(value)=>{this.className += " " + value;}, contains:(value)=>this.className.split(/\s+/).includes(value)};
    }
    set innerHTML(value) { assert.equal(value, "", "renderer must not interpolate HTML"); this.children=[]; this._text=""; }
    get textContent() { return this._text + this.children.map(c=>c.textContent).join(""); }
    set textContent(value) { this._text=String(value); this.children=[]; }
    setAttribute(key,value) { this.attrs[key]=String(value); }
    getAttribute(key) { return this.attrs[key] || null; }
    appendChild(child) { child.parentNode=this; this.children.push(child); return child; }
    remove() { if(this.parentNode) this.parentNode.children=this.parentNode.children.filter(x=>x!==this); }
    addEventListener(type,fn) { const arr=this.listeners.get(type)||[]; arr.push(fn); this.listeners.set(type,arr); }
    async click() { if(this.tagName==="A") calls.push(["anchor",this.download,this.href]); for(const fn of this.listeners.get("click")||[]) await fn(event(this)); }
    focus() { doc.activeElement=this; }
    contains(target) { return target===this || this.children.some(c=>c.contains(target)); }
    getBoundingClientRect() { return { left:20,top:20,width:Math.max(240,this.textContent.length/2),height:360 }; }
    matches(selector) {
      if (selector.includes(",")) return selector.split(",").some(s=>this.matches(s));
      if (selector==='button[role="menuitem"]') return this.tagName==="BUTTON" && this.attrs.role==="menuitem";
      if (selector==='.rsCard[data-rs-path]') return this.classList.contains("rsCard") && !!this.dataset.rsPath;
      if (selector.startsWith(".rsCard[data-rs-path=")) return this.classList.contains("rsCard");
      if (selector.startsWith(".")) return this.classList.contains(selector.slice(1));
      return this.tagName===selector.toUpperCase();
    }
    querySelectorAll(selector) { return this.children.flatMap(c=>[...(c.matches(selector)?[c]:[]),...c.querySelectorAll(selector)]); }
    closest(selector) { return this.matches(selector)?this:this.parentNode?this.parentNode.closest(selector):null; }
  }
  doc.body=new Element("body"); doc.documentElement={lang}; doc.activeElement=doc.body;
  doc.createElement=tag=>new Element(tag);
  doc.querySelector=selector=>doc.body.querySelectorAll(selector)[0]||null;
  doc.addEventListener=(type,fn)=>{const arr=events.get(type)||[];arr.push(fn);events.set(type,arr);};
  const w={document:doc, innerWidth:900,innerHeight:700, localStorage:{getItem:()=>lang},
    addEventListener:(type,fn)=>{const arr=windows.get(type)||[];arr.push(fn);windows.set(type,arr);}};
  let selected=lang;
  if(globalI18n) w.PQNAS_I18N={getLanguage:()=>selected,t:dictionary,ready:()=>ready||Promise.resolve(selected)};
  const api={videoByPath:p=>({path:p}),openPlayer:v=>calls.push(["play",v.path]),editMetadata:v=>calls.push(["metadata",v.path]),
    renameVideo:v=>calls.push(["rename",v.path]),shareVideo:v=>calls.push(["share",v.path]),
    downloadUrl:p=>{calls.push(["download",p]);return '/download?path='+encodeURIComponent(p);},
    deleteVideo:v=>calls.push(["delete",v.path]),deleteVideos:v=>calls.push(["deleteMany",Array.from(v,x=>x.path)]),
    selectPath:p=>calls.push(["select",p]),selectedVideo:()=>({path:"Kuvia/video.mp4"}),setStatus:v=>calls.push(["status",v])};
  const context=vm.createContext({window:w,document:doc,console,Promise,Array,Map,Set,
    CSS:{escape:x=>x},MutationObserver:class{constructor(fn){this.fn=fn;observers.push(this);}observe(target,options){this.target=target;this.options=options;}},
    uploadVideosFromEmptyAreaMenu:async()=>calls.push(["upload"]),refreshIndexFromMenu:async()=>calls.push(["refresh"]),
    setViewModeFromMenu:value=>calls.push(["view",value])});
  vm.runInContext(I18N,context);
  vm.runInContext(menuSource,context);
  w.__test.install(api);
  const m=()=>doc.querySelector(".rsContextMenu");
  const buttons=()=>m().querySelectorAll('button[role="menuitem"]');
  return {w,doc,calls,observers,api,events,windows,buttons,menu:m,test:w.__test,
    openVideo:(p="Kuvia/Jouluvideo3.3gp")=>{w.__test.renderMenu(p);w.__test.placeMenu(880,680);},
    openApp:()=>{w.__test.renderAppMenu();w.__test.placeMenu(880,680);},
    changeLanguage:(value, eventOnly=false)=>{selected=value;doc.documentElement.lang=value;if(!eventOnly) for(const o of observers)o.fn([]);for(const fn of windows.get("pqnas-language-changed")||[])fn({detail:{lang:value}});},
    setApiLanguage:value=>{selected=value;},
    event:event};
}
function event(target,extra={}) {return {target,clientX:100,clientY:100,preventDefault(){},stopPropagation(){},...extra};}

for (const lang of LANGS) {
  test(`${lang}: video menu renders translated labels despite stale global dictionary`, () => {
    const h=harness(lang); h.openVideo();
    const d=translations.dictionaries[lang];
    assert.deepEqual(h.buttons().map(b=>b.children[0].textContent),itemKeys.map(k=>d[k]));
    assert.equal(h.buttons()[0].children[1].textContent,d['reelstack.context.key_enter']);
    assert.equal(h.buttons()[1].children[1].textContent,d['reelstack.context.key_space']);
    assert.equal(h.menu().children[0].textContent,"Jouluvideo3.3gp");
    assert.deepEqual(h.calls,[]);
  });
  test(`${lang}: background menu translates all seven view choices`, () => {
    const h=harness(lang);h.openApp();const d=translations.dictionaries[lang];
    const expected=[d['reelstack.context.upload_video'],d['reelstack.refresh_index'],...viewModes.map(mode=>translations.text(lang,'reelstack.context.view_mode',{view:d['reelstack.view'],mode:d['reelstack.view.'+mode]}))];
    assert.deepEqual(h.buttons().map(b=>b.children[0].textContent),expected);
    assert.equal(h.menu().children[0].textContent,"Reel Stack");
    assert.deepEqual(h.calls,[]);
  });
}

test("open video menu follows language change without triggering actions or losing focus/path", () => {
  const h=harness("en");h.openVideo("Kuvia/ä space <img>.mp4");h.buttons()[1].focus();
  h.changeLanguage("fi");
  assert.equal(h.buttons()[1].children[0].textContent,"Muokkaa metatietoja");
  assert.equal(h.buttons()[1].children[1].textContent,"Välilyönti");
  assert.equal(h.doc.activeElement,h.buttons()[1]);
  assert.equal(h.menu().dataset.rsPath,"Kuvia/ä space <img>.mp4");
  assert.equal(h.menu().children[0].textContent,"ä space <img>.mp4");
  assert.equal(h.menu().hidden,false);
  assert.ok(parseInt(h.menu().style.left)>=8);
  assert.ok(parseInt(h.menu().style.top)>=8);
  assert.deepEqual(h.calls,[]);
});
test("background menu follows live language change without switching view", () => {
  const h=harness("fi");h.openApp();h.buttons()[4].focus();h.changeLanguage("de");
  assert.equal(h.buttons()[0].children[0].textContent,"Video hochladen");
  assert.equal(h.buttons()[4].children[0].textContent,"Ansicht: Kürzlich hinzugefügt");
  assert.equal(h.doc.activeElement,h.buttons()[4]);assert.deepEqual(h.calls,[]);
});
test("document language change works when shell language API is unavailable", () => {
  const h=harness("fi",{globalI18n:false});h.openVideo();h.changeLanguage("sv-SE");
  assert.equal(h.buttons()[0].children[0].textContent,"Spela upp");
});
test("closed menu stays closed after language change", () => {
  const h=harness("en");h.openVideo();h.test.hideMenu();h.changeLanguage("fr");
  assert.equal(h.menu().hidden,true);assert.deepEqual(h.calls,[]);
  h.openVideo();assert.equal(h.buttons()[0].children[0].textContent,"Lire");
});
test("late dictionary readiness refreshes an already open menu", async () => {
  let finish;const ready=new Promise(resolve=>{finish=resolve;});
  const h=harness("en",{ready});h.openApp();h.setApiLanguage("it");finish();await ready;await Promise.resolve();
  assert.equal(h.buttons()[0].children[0].textContent,"Carica video");assert.deepEqual(h.calls,[]);
});
test("unrelated global translations are not overridden", () => {
  const h=harness("fi",{dictionary:(k,p,f)=>k==='common.close'?'Sulje':f});
  assert.equal(h.test.reelT('common.close',null,'Close'),'Sulje');
  assert.equal(h.w.PQNAS_I18N.t('unrelated',null,'original'),'original');
});
test("menu installation is idempotent and does not duplicate observers or listeners", () => {
  const h=harness();h.test.install(h.api);h.test.install(h.api);
  assert.equal(h.events.get("contextmenu").length,1);
  assert.equal(h.windows.get("pqnas-language-changed").length,1);
  assert.equal(h.observers.length,1);
});
test("Escape closes the context menu without invoking a video action", () => {
  const h=harness();h.openVideo();h.test.onKeydown(event(h.doc.body,{key:'Escape'}));
  assert.equal(h.menu().hidden,true);assert.deepEqual(h.calls,[]);
});
test("keyboard context menu still opens for the selected video", () => {
  const h=harness();h.test.onKeydown(event(h.doc.body,{key:'F10',shiftKey:true}));
  assert.equal(h.menu().hidden,false);assert.equal(h.menu().dataset.rsPath,'Kuvia/video.mp4');
  assert.equal(h.buttons()[0].children[0].textContent,'Toista');
});
for (const [index, action] of [[0,'play'],[1,'metadata'],[2,'rename'],[3,'share'],[5,'delete']]) {
  test(`${action}: translated video action preserves the exact selected path`, async () => {
    const h=harness('zh');const p='Kuvia/työ/ä "$& <video>.mp4';h.openVideo(p);h.changeLanguage('pl');await h.buttons()[index].click();
    assert.deepEqual(h.calls,[[action,p]]);assert.equal(h.menu().hidden,true);
  });
}
test("download keeps filename and URL semantics", async () => {
  const h=harness('tr');const p='Kuvia/ä space.mp4';h.openVideo(p);await h.buttons()[4].click();
  assert.deepEqual(h.calls,[['download',p],['anchor','ä space.mp4','/download?path='+encodeURIComponent(p)]]);
});
test("multi-delete still uses existing selection handler, not one-file delete", async () => {
  const h=harness('fi');h.w.PQNAS_REELSTACK_SELECTION={getSelectedPaths:()=>['a.mp4','b.mp4']};h.openVideo('a.mp4');h.changeLanguage('de');await h.buttons()[5].click();
  assert.deepEqual(h.calls,[['deleteMany',['a.mp4','b.mp4']]]);
});
for (const [index, expected] of [[0,['upload']],[1,['refresh']],...viewModes.map((m,i)=>[i+2,['view',m]])]) {
  test(`background action ${expected.join('/')}: language change preserves its callback`, async () => {
    const h=harness('en');h.openApp();h.changeLanguage('fi');await h.buttons()[index].click();
    assert.deepEqual(h.calls,[expected]);
  });
}
test("HTML loads bundled translations before the menu; app API and minimum version unchanged", () => {
  const html=fs.readFileSync(path.join(WWW,'index.html'),'utf8');
  const a=html.indexOf('reelstack_context_i18n.js?v=1.0.3');
  const b=html.indexOf('reelstack_context_menu.js?v=1.0.3');
  assert.ok(a>=0&&b>a);
  const manifest=JSON.parse(fs.readFileSync(path.join(WWW,'../manifest.json'),'utf8'));
  assert.equal(manifest.id,'reelstack');assert.equal(manifest.version,'1.0.3');
  assert.equal(manifest.api_base,'/api/v4/files');assert.equal(manifest.min_server_version,'1.1.3');
});
