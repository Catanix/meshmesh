#!/usr/bin/env node
// Languages of the device page and the Android app (i18n/README.md). The Russian text of web/index.html,
// android/.../assets/host.js and the app's Kotlin code is the key of i18n/web/<lang>.json:
//   L('текст'), L`текст ${x}` (the key has {0}, {1}… for the values) and the markup of web/index.html;
//   plural(n,'одна','две','пять') — the key "одна|две|пять", its value the forms of the language with {n};
//   I18n.t("текст {0}", x) in Kotlin; "_pieces" — the letters K Q R B N of the chess notation.
// Needs acorn: npm ci --prefix tools.
//   node tools/web_i18n.cjs sync          keys of the code into every dictionary (new ones empty, old ones go)
//   node tools/web_i18n.cjs todo LANG     what LANG still lacks
//   node tools/web_i18n.cjs check         complete and consistent dictionaries, for the code as it is
//   node tools/web_i18n.cjs build DIR     DIR/<lang>.js: the dictionaries as the page loads them
'use strict';
const fs=require('fs'),path=require('path');
const ROOT=path.resolve(__dirname,'..'),DIR=path.join(ROOT,'i18n/web');
const LANGS=['en','uk','es','pt','fr','de','it','pl','tr','zh','ja','ko','ar','id'];
const CYR=/[А-Яа-яЁё]/;

function keys(){
 const acorn=require('acorn'),out=new Map(),add=(k,kind,where)=>{if(!out.has(k))out.set(k,{kind,where})};
 const scan=(src,file,offset=0)=>{
  const ast=acorn.parse(src,{ecmaVersion:'latest',locations:true});
  const walk=n=>{
   if(!n||typeof n.type!=='string')return;
   const line=n.loc&&`${file}:${n.loc.start.line+offset}`;
   if(n.type==='CallExpression'&&n.callee.type==='Identifier'&&n.callee.name==='L'){
    const a=n.arguments[0];
    if(n.arguments.length!==1||a.type!=='Literal'||typeof a.value!=='string')throw Error(`${line}: L() takes one string`);
    add(a.value,'text',line)}
   if(n.type==='TaggedTemplateExpression'&&n.tag.type==='Identifier'&&n.tag.name==='L')
    add(n.quasi.quasis.map((q,i)=>(i?`{${i-1}}`:'')+q.value.cooked).join(''),'text',line);
   if(n.type==='CallExpression'&&n.callee.type==='Identifier'&&n.callee.name==='plural'&&n.arguments.length===4){
    const f=n.arguments.slice(1);
    if(f.every(a=>a.type==='Literal'&&typeof a.value==='string'))add(f.map(a=>a.value).join('|'),'plural',line);
    else if(!f.every(a=>a.type==='Identifier'))throw Error(`${line}: plural() takes the forms as strings`)}
   for(const k in n){const c=n[k];if(Array.isArray(c))c.forEach(walk);else if(c&&typeof c.type==='string')walk(c)}};
  walk(ast)};
 // web/index.html: the scripts and the markup.
 const html=fs.readFileSync(path.join(ROOT,'web/index.html'),'utf8');
 let markup=html;
 for(const m of html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)){scan(m[1],'web/index.html',html.slice(0,m.index+m[0].indexOf('>')+1).split('\n').length-1);markup=markup.replace(m[0],'')}
 markup=markup.replace(/<style[\s\S]*?<\/style>/g,'').replace(/(<textarea[^>]*>)[\s\S]*?(<\/textarea>)/g,'$1$2');
 const decode=s=>s.replace(/&nbsp;/g,'\u00a0').replace(/&lt;/g,'<').replace(/&gt;/g,'>').replace(/&quot;/g,'"').replace(/&#39;/g,"'").replace(/&#(\d+);/g,(m,n)=>String.fromCodePoint(+n)).replace(/&amp;/g,'&');
 for(const part of markup.split(/<[^>]*>/)){const t=decode(part).trim();if(CYR.test(t))add(t,'markup','web/index.html')}
 for(const m of markup.matchAll(/\s(?:title|placeholder|aria-label|value)="([^"]*)"/g)){const t=decode(m[1]).trim();if(CYR.test(t))add(t,'markup','web/index.html')}
 scan(fs.readFileSync(path.join(ROOT,'android/app/src/main/assets/host.js'),'utf8'),'host.js');
 // Kotlin: I18n.t("…") with the Russian text as it is written.
 const kt=d=>fs.readdirSync(d,{withFileTypes:true}).flatMap(e=>e.isDirectory()?kt(path.join(d,e.name)):e.name.endsWith('.kt')?[path.join(d,e.name)]:[]);
 for(const f of kt(path.join(ROOT,'android/app/src/main/java'))){
  const src=fs.readFileSync(f,'utf8');
  for(const m of src.matchAll(/I18n\.t\("((?:\\.|[^"\\])*)"/g))add(m[1].replace(/\\(.)/g,(x,c)=>({n:'\n',t:'\t'}[c]??c)),'text',path.relative(ROOT,f));
  const left=src.split('\n').map((l,i)=>[l,i+1]).filter(([l])=>CYR.test(l.replace(/\/\/.*$/,'').replace(/^\s*\*.*$/,'').replace(/I18n\.t\("(?:\\.|[^"\\])*"/g,'')));
  if(!f.endsWith('/I18n.kt'))for(const [l,i] of left)if(/"[^"]*[А-Яа-яЁё]/.test(l))throw Error(`${path.relative(ROOT,f)}:${i}: Russian text outside I18n.t: ${l.trim()}`)}
 return out}

const file=l=>path.join(DIR,l+'.json');
const read=l=>fs.existsSync(file(l))?JSON.parse(fs.readFileSync(file(l),'utf8')):{};
const write=(l,d)=>fs.writeFileSync(file(l),JSON.stringify(d,null,1)+'\n');
const holes=s=>[...String(s).matchAll(/\{(\d+|n)\}/g)].map(m=>m[0]).sort().join();
const tags=s=>[...String(s).matchAll(/<\/?([a-z0-9]+)/gi)].map(m=>m[0].toLowerCase()).sort().join();
const filled=v=>typeof v==='string'?v!=='':v&&typeof v==='object'&&Object.values(v).some(x=>x);

function sync(){
 const k=keys();fs.mkdirSync(DIR,{recursive:true});
 for(const l of LANGS){
  const old=read(l),next={_pieces:old._pieces||''};
  for(const [key,{kind}] of k)next[key]=old[key]!==undefined?old[key]:kind==='plural'?{}:'';
  write(l,next)}
 console.log(`${k.size} keys in ${LANGS.length} dictionaries`)}

function problems(l,k){
 const d=read(l),out=[],cats=new Intl.PluralRules(l).resolvedOptions().pluralCategories;
 if(String(d._pieces||'').split(' ').length!==5)out.push('_pieces: five letters K Q R B N');
 for(const [key,{kind,where}] of k){
  const v=d[key];
  if(!filled(v)){out.push(`missing (${where}): ${key}`);continue}
  if(kind==='plural'){
   if(typeof v!=='object'){out.push(`plural forms expected: ${key}`);continue}
   for(const c of cats)if(!v[c]&&!(c!=='other'&&v.other))out.push(`plural "${c}" missing: ${key}`);
   continue}
  if(typeof v!=='string'){out.push(`text expected: ${key}`);continue}
  if(holes(v)!==holes(key))out.push(`placeholders differ: ${key} -> ${v}`);
  if(tags(v)!==tags(key))out.push(`markup differs: ${key} -> ${v}`);
  if(/^\s|\s$/.test(key)&&(key.match(/^\s*/)[0]!==v.match(/^\s*/)[0]||key.match(/\s*$/)[0]!==v.match(/\s*$/)[0]))out.push(`edge spaces differ: "${key}" -> "${v}"`)}
 for(const key of Object.keys(d))if(key!=='_pieces'&&!k.has(key))out.push(`not in the code: ${key}`);
 return out}

function build(outDir){
 fs.mkdirSync(outDir,{recursive:true});
 for(const l of LANGS){const d=read(l);for(const k of Object.keys(d))if(!filled(d[k]))delete d[k];
  fs.writeFileSync(path.join(outDir,l+'.js'),'globalThis.MeshMeshWebDict='+JSON.stringify(d).replace(/</g,'\\u003c')+';\n')}}

const [cmd,arg]=process.argv.slice(2);
if(cmd==='sync')sync();
else if(cmd==='todo'){const k=keys();for(const p of problems(arg,k))console.log(p)}
else if(cmd==='check'){
 const k=keys();let bad=0;
 for(const l of LANGS){const p=problems(l,k);if(p.length){bad+=p.length;console.error(`${l}: ${p.length} problems\n  `+p.slice(0,20).join('\n  '))}}
 if(bad)process.exit(1);console.log(`PASS page and app languages: ${k.size} keys, ${LANGS.length} dictionaries`)}
else if(cmd==='build'&&arg)build(path.resolve(arg));
else{console.error('usage: web_i18n.cjs sync | todo LANG | check | build DIR');process.exit(2)}
