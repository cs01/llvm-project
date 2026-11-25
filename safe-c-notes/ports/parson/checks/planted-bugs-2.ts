import {readFileSync,writeFileSync} from "fs"; import {spawnSync} from "child_process";
const src=readFileSync(process.argv[2]+"/parson/parson.c","utf8"), S=process.argv[3];
const anchor="/* JSON Object API */";
const P:[string,string][]=[
["goto double free","@@R                default:\n                    goto error;@@                default:\n                    parson_free(output);\n                    goto error;"],
["reader outlives text","static char t1(void){ char *_Owned _Nullable b=new_bytes(4); if(!b) return 0; char *_Owned __null_terminated s=end_string(b,3); JSON_Reader r={s}; release_string(s); return *r.p; }"],
["reader moved by callee, text freed","static char t2(void){ char *_Owned _Nullable b=new_bytes(4); if(!b) return 0; char *_Owned __null_terminated s=end_string(b,0); JSON_Reader r={s}; skip_chars(&r,0); release_string(s); return *r.p; }"],
["out param holds other text","static void t3(JSON_Reader*r,const char*q){ r->p=q; }"],
["slot copied","static void t4(JSON_Slot*a,JSON_Slot*b){ *a=*b; }"],
["slot value freed, not taken","static void t5(JSON_Slot*a){ json_value_free(a->value); }"],
["array element borrow across resize","static int t6(JSON_Array*a){ if(!a->items||!a->count) return 0; JSON_Slot*s=&a->items[0]; json_array_resize(a,64); return s->value!=NULL; }"],
["array element borrow across sibling free","static int t7(JSON_Object*o){ if(!o->names||!o->count) return 0; const char*_Nullable __null_terminated n=o->names[0].chars; release_string(_Take(o->names[0].chars)); return n?*n:0; }"],
["chars view across free","static int t8(JSON_Value*_Owned v){ JSON_Chars c=json_value_get_chars(v); json_value_free(v); return c.p?c.p[0]:0; }"],
["string across free","static int t9(JSON_Value*_Owned v){ const char*_Nullable c=json_value_get_string(v); json_value_free(v); return c?*c:0; }"],
["count changed past owner (release of raw object leaves children owned)","static int t10(JSON_Object*_Owned o){ JSON_Value*_Nullable v=json_object_get_value_at(o,0); release_object(o); return json_value_get_type(v); }"],
["writer buf freed under it","static void t11(void){ char*_Owned _Nullable b=new_bytes(8); JSON_Writer w={b,8,0}; parson_free(b); json_writer_append(&w,\"x\",1); }"],
["store local address in settings","static void t12(void){ char b[4]=\"abc\"; settings.float_format=(char*__null_terminated)0; (void)b; json_set_float_serialization_format(\"x\"); json_free_serialized_string(settings.float_format);}"],
["two moves in a loop","static void t13(JSON_Array*a,JSON_Value*_Owned v){ for(int i=0;i<2;i++) json_array_add(a,v); }"],
["two moves through switch","static void t14(JSON_Array*a,JSON_Value*_Owned v,int k){ switch(k){case 0: json_array_add(a,v); case 1: json_array_add(a,v);} }"],
["parsed keeps and frees","static int t15(JSON_Slot*o,JSON_Value*_Owned v){ int r=parsed(o,v); if(r!=JSONSuccess) json_value_free(v); return r; }"],
["child view of key after add","static int t16(JSON_Object*o,char*_Owned __null_terminated k,JSON_Value*_Owned v){ const char*__null_terminated c=k; if(json_object_add(o,k,v)!=JSONSuccess) return 0; return *c; }"],
];
for(const [n,b] of P){ let out:string; if(b.startsWith("@@R")){const [x,y]=b.slice(3).split("@@"); if(src.split(x).length<2) {console.log("NOMATCH",n);} out=src.replace(x,()=>y);} else out=src.replace(anchor,()=>b+"\n"+anchor);
 if(out===src){console.log("NOMATCH",n);continue}
 writeFileSync(S+"/pl/bug.c",out); const r=spawnSync(process.env.HOME+"/git/llvm-safe-c/build.snap17/bin/clang",["-fsafe-c","-w","-ferror-limit=0","-fsyntax-only",S+"/pl/bug.c","-I"+S+"/pl"],{encoding:"utf8"});
 const e=r.stderr.split("\n").filter(l=>/error:/.test(l)).map(l=>l.replace(/.*bug.c/,"")); console.log(e.length?"REJECTED":"ACCEPTED",n,"|",e[0]??"");}
