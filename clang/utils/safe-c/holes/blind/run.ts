// usage: bun run.ts file.txt ... ; programs split by "//// name [tsan] [two]"; "//// file2" starts second TU of the current program
const OLD = ["_Exclusive=","_View=","_Unchecked=","_Returns_owned=","_Lifetimebound=","_Unsynchronized=","_Takes(N)="].map(d => "-D" + d); // marks Safe C no longer has
import {$} from "bun"; import {mkdirSync,writeFileSync} from "fs";
const R=import.meta.dir+"/../../../../..", D=process.env.OUT??"/tmp/safe-c-blind", SC=process.env.SC??R+"/build.noindex/bin/clang", STD=R+"/safe-c-std";
const sdk=(await $`xcrun --show-sdk-path`.text()).trim();
const PRE=`#include <locale.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdbool.h>\n#include <stdint.h>\n#include <stddef.h>\n`;
type P={name:string,opts:string[],src:string[]}; const ps:P[]=[];
for(const f of process.argv.slice(2)){ let cur:P|undefined; for(const l of (await Bun.file(f).text()).split("\n")){const m=l.match(/^\/\/\/\/ (\S+)(.*)/); if(m){ if(m[1]=="file2"){cur!.src.push(PRE)} else {cur={name:m[1],opts:m[2].trim().split(/\s+/),src:[PRE]};ps.push(cur)} } else if(cur) cur.src[cur.src.length-1]+=l+"\n"; } }
async function sh(cmd:string[],t=8000){const p=Bun.spawn(cmd,{stdout:"pipe",stderr:"pipe",stdin:"ignore",env:{...process.env,ASAN_OPTIONS:"detect_stack_use_after_return=1:abort_on_error=0:symbolize=0"}});let to=false;const k=setTimeout(()=>{to=true;p.kill(9)},t);const [o,e]=await Promise.all([new Response(p.stdout).text(),new Response(p.stderr).text()]);const c=await p.exited;clearTimeout(k);return {c:to?"TIMEOUT":(p.signalCode??c),o,e}}
async function one(p:P){const d=`${D}/b/${p.name}`;mkdirSync(d,{recursive:true});const fs=p.src.map((s,i)=>{const f=`${d}/x${i||""}.c`;writeFileSync(f,s);return f});
 let r=await sh([SC,"-fsafe-c",...OLD,"-isysroot",sdk,"-O0",...fs,"-o",d+"/o0"],60000);
 if(r.c!==0){const ice=/PLEASE submit|Assertion|Stack dump/.test(r.e)||r.c==="TIMEOUT";const er=r.e.split("\n").filter(l=>/error|Undefined|undefined/.test(l)).slice(0,ice?4:2).map(l=>l.replace(/^.*x(\d?)\.c:(\d+):\d+: /,"L$2 ").slice(0,150));return `${ice?"!!ICE":"rej"} ${p.name}: ${er.join(" | ")}`}
 await sh([SC,"-fsafe-c",...OLD,"-isysroot",sdk,"-O2",...fs,"-o",d+"/o2"],60000);
 const ts=p.opts.includes("tsan");
 const q=await sh(["/usr/bin/clang","-w","-g",ts?"-fsanitize=thread":"-fsanitize=address,undefined","-fno-sanitize-recover=all","-I"+STD,"-include",STD+"/safec.h",...fs,"-o",d+"/ref"],60000);
 const a=await sh([d+"/o0"]),b=await sh([d+"/o2"]);const ref=q.c===0?await sh([d+"/ref"]):{c:"REFCOMPILE",o:"",e:q.e};
 const sm=(ref.e.match(/(ERROR: AddressSanitizer[^\n]{0,60}|runtime error:[^\n]{0,90}|WARNING: ThreadSanitizer[^\n]{0,40}|error:[^\n]*)/)??[""])[0];
 const sig=(x:any)=>typeof x==="string"?x:x;
 const trap=a.c==="SIGTRAP"&&b.c==="SIGTRAP";const bad=!trap&&(sm!==""||[a.c,b.c].some(c=>["SIGSEGV","SIGBUS","SIGABRT"].includes(c as any)));
 return `${bad?"**BUG":"acc"} ${p.name}: O0=${sig(a.c)} O2=${sig(b.c)} ref=${ref.c} ${sm} out0=${JSON.stringify(a.o.slice(0,80))} out2=${JSON.stringify(b.o.slice(0,80))}`}
let i=0;const out:string[]=[];await Promise.all(Array.from({length:4},async()=>{while(i<ps.length){const k=i++;out[k]=await one(ps[k])}}));console.log(out.join("\n"));
