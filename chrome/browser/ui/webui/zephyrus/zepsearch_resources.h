// Copyright 2026 The Zephyrus Authors
// Use of this source code is governed by a BSD-style license.

#ifndef CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_RESOURCES_H_
#define CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_RESOURCES_H_

namespace zephyrus::search {

inline constexpr char kSearchHtml[] = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ZepSearch</title><link rel="icon" href="/favicon.svg" type="image/svg+xml"><link rel="stylesheet" href="/search.css"><script defer src="/search.js"></script></head>
<body><div class="shell"><header><a class="brand" href="/" aria-label="ZepSearch home"><span class="mark">✳</span> ZepSearch</a>
<form id="search-form" role="search"><input id="query" type="search" name="q" aria-label="Search the web" autocomplete="off" spellcheck="false" maxlength="1000"><button type="submit">Search</button></form></header>
<main><div id="status" role="status" aria-live="polite"></div><section id="answers" hidden></section><section id="results" aria-label="Search results"></section><section id="extras" hidden></section><nav id="pages" aria-label="Result pages" hidden><button id="previous" type="button">Previous</button><span id="page-number"></span><button id="next" type="button">Next</button></nav></main>
<footer>Searches are sent to the configured ZepSearch service. Result pages open through normal browser navigation.</footer></div></body></html>)HTML";

inline constexpr char kSearchCss[] =
    R"CSS(:root{color-scheme:light dark;font-family:system-ui,-apple-system,"Segoe UI",sans-serif;--bg:#f4f5f0;--ink:#202923;--muted:#647069;--line:#dce2da;--surface:#fff;--accent:#24694f;--hover:#e7f0e9}
@media(prefers-color-scheme:dark){:root{--bg:#151d19;--ink:#e9eee8;--muted:#a9b8ae;--line:#36453b;--surface:#202b24;--accent:#8bd4a8;--hover:#293b30}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink)}.shell{max-width:1040px;margin:auto;padding:38px 28px 80px}header{display:flex;align-items:center;gap:30px;padding-bottom:34px;border-bottom:1px solid var(--line)}.brand{white-space:nowrap;color:var(--ink);font-size:21px;font-weight:650;letter-spacing:-.04em;text-decoration:none}.mark{color:var(--accent);font-size:29px;vertical-align:-2px}form{display:flex;min-width:0;flex:1;background:var(--surface);border:1px solid var(--line);border-radius:99px;padding:5px;box-shadow:0 4px 16px #00000009}input{flex:1;min-width:0;padding:11px 17px;border:0;background:transparent;color:var(--ink);font:inherit;outline:0}form:focus-within{outline:2px solid var(--accent);outline-offset:2px}button{font:inherit;cursor:pointer}form button{border:0;border-radius:99px;background:var(--accent);color:var(--bg);padding:10px 23px;font-weight:600}main{max-width:750px;margin:37px 0 0 165px}#status{min-height:24px;color:var(--muted);font-size:14px;margin-bottom:20px}.error{padding:20px;border:1px solid var(--line);border-radius:15px;background:var(--surface);color:var(--ink)}.error button{margin-top:15px;padding:8px 16px;border:1px solid var(--line);border-radius:10px;background:var(--hover);color:var(--ink)}.result{padding:19px 0 21px;border-bottom:1px solid var(--line)}.source{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;color:var(--muted);font-size:12px;margin-bottom:7px}.result a{color:var(--accent);font-size:21px;line-height:1.3;text-decoration:none;overflow-wrap:anywhere}.result a:hover{text-decoration:underline}.result p{margin:8px 0 0;font-size:14px;line-height:1.55;overflow-wrap:anywhere}.result small{color:var(--muted)}#answers,#extras{padding:16px 20px;background:var(--surface);border:1px solid var(--line);border-radius:17px;margin-bottom:24px;overflow-wrap:anywhere}#answers h2,#extras h2{margin:0 0 8px;font-size:14px}#answers p,#extras p{margin:6px 0;font-size:14px;line-height:1.55}#pages{display:flex;align-items:center;gap:15px;margin-top:30px}#pages button{border:1px solid var(--line);border-radius:9px;padding:9px 16px;background:var(--surface);color:var(--ink)}#pages button:disabled{opacity:.45;cursor:default}footer{margin:85px 0 0 165px;color:var(--muted);font-size:12px}
@media(max-width:700px){.shell{padding:20px 18px 60px}header{display:block}.brand{display:inline-block;margin-bottom:18px}main,footer{margin-left:0}form button{padding:10px 15px}}
)CSS";

inline constexpr char kSearchFavicon[] =
    R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64"><rect width="64" height="64" rx="18" fill="#24694f"/><path d="M32 9v46M9 32h46M16 16l32 32M48 16 16 48" stroke="#f4f5f0" stroke-width="5" stroke-linecap="round"/></svg>)SVG";

inline constexpr char kSearchJs[] = R"JS('use strict';
const form=document.querySelector('#search-form');
const input=document.querySelector('#query');
const status=document.querySelector('#status');
const results=document.querySelector('#results');
const answers=document.querySelector('#answers');
const extras=document.querySelector('#extras');
const pages=document.querySelector('#pages');
const previous=document.querySelector('#previous');
const next=document.querySelector('#next');
const pageNumber=document.querySelector('#page-number');
const optional=['categories','engines','language','safesearch','time_range'];
let generation=0;
let state={q:'',pageno:1};
function readState(){
  const params=new URLSearchParams(location.search);
  const p=Number(params.get('pageno'));
  const nextState={q:(params.get('q')||'').trim(),pageno:Number.isSafeInteger(p)&&p>0?p:1};
  for(const key of optional){if(params.has(key))nextState[key]=params.get(key)||'';}
  return nextState;
}
function navigate(changes){
  const nextState={...state,...changes};
  const params=new URLSearchParams();
  params.set('q',nextState.q);params.set('pageno',String(nextState.pageno));
  for(const key of optional){if(nextState[key])params.set(key,nextState[key]);}
  history.pushState(null,'','/?'+params.toString());
  run();
}
function setStatus(message){status.textContent=message;status.className='';}
function addTextCard(parent,title,items){
  if(!items.length){parent.hidden=true;return;}
  parent.hidden=false;parent.replaceChildren();
  const heading=document.createElement('h2');heading.textContent=title;parent.append(heading);
  for(const text of items){const p=document.createElement('p');p.textContent=text;parent.append(p);}
}
function safeUrl(raw){
  try{const u=new URL(raw);return u.protocol==='https:'||u.protocol==='http:'?u.href:null;}
  catch{return null;}
}
function render(payload){
  results.replaceChildren();
  const rows=Array.isArray(payload.results)?payload.results:[];
  addTextCard(answers,'Answers',Array.isArray(payload.answers)?payload.answers:[]);
  const extra=[];
  if(Array.isArray(payload.suggestions)&&payload.suggestions.length)
    extra.push('Related: '+payload.suggestions.join(' · '));
  if(Array.isArray(payload.infoboxes))extra.push(...payload.infoboxes);
  addTextCard(extras,'More information',extra);
  for(const row of rows){
    const href=safeUrl(row.url);
    if(!href)continue;
    const article=document.createElement('article');article.className='result';
    const source=document.createElement('span');source.className='source';
    source.textContent=row.engine?row.engine+' · '+href:href;
    const title=document.createElement('a');title.href=href;title.textContent=row.title||href;
    const snippet=document.createElement('p');snippet.textContent=row.content||'';
    article.append(source,title,snippet);results.append(article);
  }
  const failed=Array.isArray(payload.unresponsive_engines)?payload.unresponsive_engines:[];
  const partial=failed.length?' Some engines did not respond: '+failed.join(', ')+'.':'';
  setStatus(results.childElementCount?`${results.childElementCount} results · page ${state.pageno}.${partial}`:
    `No results for “${state.q}” on page ${state.pageno}.${partial}`);
  pages.hidden=!results.childElementCount;
  previous.disabled=state.pageno<=1;
  next.disabled=!results.childElementCount;
  pageNumber.textContent='Page '+state.pageno;
}
function showError(message){
  status.replaceChildren();status.className='error';
  const text=document.createElement('div');text.textContent=message;
  const retry=document.createElement('button');retry.type='button';retry.textContent='Retry';retry.addEventListener('click',run);
  status.append(text,retry);
  pages.hidden=true;
}
function run(){
  state=readState();input.value=state.q;
  const id=++generation;
  results.replaceChildren();answers.hidden=true;extras.hidden=true;pages.hidden=true;
  if(!state.q){setStatus('Search the web with ZepSearch.');return;}
  setStatus('Searching…');
  const opts={};for(const key of optional){if(state[key])opts[key]=state[key];}
  chrome.send('zepSearch',[id,state.q,state.pageno,opts]);
}
window.zepSearchResponse=(id,payload)=>{
  if(id!==generation)return;
  if(!payload||!payload.ok){showError(payload?.error||'ZepSearch is unavailable. Check the connection and try again.');return;}
  render(payload);
};
form.addEventListener('submit',event=>{event.preventDefault();const q=input.value.trim();if(q)navigate({q,pageno:1});});
previous.addEventListener('click',()=>{if(state.pageno>1)navigate({pageno:state.pageno-1});});
next.addEventListener('click',()=>navigate({pageno:state.pageno+1}));
window.addEventListener('popstate',run);
run();
)JS";

}  // namespace zephyrus::search

#endif  // CHROME_BROWSER_UI_WEBUI_ZEPHYRUS_ZEPSEARCH_RESOURCES_H_
