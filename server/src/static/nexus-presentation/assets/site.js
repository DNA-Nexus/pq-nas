(function(){
  const body=document.body;
  const toggle=document.querySelector("[data-nav-toggle]");
  const links=document.querySelectorAll(".nav-links a");
  if(toggle){
    toggle.addEventListener("click",()=>{
      const open=body.classList.toggle("menu-open");
      toggle.setAttribute("aria-expanded",open?"true":"false");
    });
  }
  links.forEach(link=>link.addEventListener("click",()=>{
    body.classList.remove("menu-open");
    if(toggle)toggle.setAttribute("aria-expanded","false");
  }));
  const current=(location.pathname.split("/").pop()||"index.html").toLowerCase();
  links.forEach(link=>{
    const href=(link.getAttribute("href")||"").toLowerCase();
    if(href===current || (current===""&&href==="index.html")) link.classList.add("active");
  });

  const languageMeta={
    en:{badge:"GB",name:"English"},fi:{badge:"FI",name:"Suomi"},zh:{badge:"CN",name:"简体中文"},
    sv:{badge:"SE",name:"Svenska"},uk:{badge:"UA",name:"Українська"},de:{badge:"DE",name:"Deutsch"},
    et:{badge:"EE",name:"Eesti"},pl:{badge:"PL",name:"Polski"},es:{badge:"ES",name:"Español"},
    fr:{badge:"FR",name:"Français"},it:{badge:"IT",name:"Italiano"},tr:{badge:"TR",name:"Türkçe"}
  };
  const select=document.querySelector("[data-language-select]");
  const badge=document.querySelector("[data-language-badge]");
  if(select&&badge){
    const saved=localStorage.getItem("dnaNexusPresentationLang");
    if(saved&&languageMeta[saved]) select.value=saved;
    const update=()=>{
      const meta=languageMeta[select.value]||languageMeta.en;
      badge.textContent=meta.badge;
      select.setAttribute("aria-label","Language: "+meta.name);
    };
    update();
    select.addEventListener("change",()=>{
      localStorage.setItem("dnaNexusPresentationLang",select.value);
      update();
      // Placeholder only. Later this can redirect to translated pages or load translated strings.
    });
  }
})();
