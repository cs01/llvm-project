/* Randomized API test: prints a trace, so two versions of parson can be compared. fuzz SEEDS ITERS [failing] */
#include "parson.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned s; static unsigned rnd(unsigned n){ s^=s<<13; s^=s>>17; s^=s<<5; return s%n; }
static long live; static int failp;
static void *m(size_t n){ if (failp && rand()%failp==0) return NULL; live++; return malloc(n); }
static void f(void *p){ if (p) live--; free(p); }
static const char *K[]={"a","b","c","d","e","aa","k/1","\xc3\xa9","","longer key","x","y","z","w","q1","q2","q3","q4","q5","q6","q7","q8","q9","r1","r2","r3"};
static const char *key(void){ return K[rnd(sizeof K/sizeof*K)]; }
static const char *path(void){ static char b[128]; b[0]=0; int n=1+rnd(4); while(n--){ strcat(b,key()); if(n) strcat(b,"."); } return b; }
static const char *T[]={"null","[1,2,{\"a\":[true,\"\\u00e9\\ud83d\\ude00\"]}]","{\"a\":{\"b\":{\"c\":1}},\"b\":[]}","[","{\"a\":1,\"a\":2}","{\"a\":","\"x\\u0000y\"","[1,2,]","{\"k\\u0000\":1}","-0.5e3","01","{\"a\" 1}","[[[[[]]]],nul]","\"\\ud800\"","\xEF\xBB\xBF{\"bom\":true}"," /*c*/ [1 // x\n ,2]"};
static JSON_Value *gen(int d){ switch(rnd(d>3?6:9)){ case 0:return json_value_init_null(); case 1:return json_value_init_number(rnd(1000)/8.0); case 2:return json_value_init_boolean(rnd(2)); case 3:return json_value_init_string("str\n\"/"); case 4:return json_value_init_string_with_len("a\0b",3); case 5:return json_parse_string(T[rnd(16)]); 
 case 6:return json_parse_string_with_comments(T[rnd(16)]);
 case 7:{JSON_Value*v=json_value_init_array(); int n=rnd(20); while(n--){JSON_Value*c=gen(d+1); 
   int r=json_array_append_value(json_array(v),c);
#ifdef BASE
   if(r!=JSONSuccess) json_value_free(c);
#else
   (void)r;
#endif
 } return v;}
 default:{JSON_Value*v=json_value_init_object(); int n=rnd(20); while(n--){JSON_Value*c=gen(d+1); const char*k=key(); int r=json_object_set_value(json_object(v),k,c);
#ifdef BASE
   if(r!=JSONSuccess) json_value_free(c);
#else
   (void)r;
#endif
 } return v;} } }
static JSON_Value *pick(JSON_Value *v){ for(;;){ if(rnd(4)==0) return v; JSON_Value *n=NULL; if(json_object(v)&&json_object_get_count(json_object(v))) n=json_object_get_value_at(json_object(v),rnd(json_object_get_count(json_object(v)))); else if(json_array(v)&&json_array_get_count(json_array(v))) n=json_array_get_value(json_array(v),rnd(json_array_get_count(json_array(v)))); if(!n) return v; if(json_value_get_parent(n)!=v) abort(); v=n; } }
static unsigned long H=5381; static void out(const char *t){ if(!t)t="(null)"; for(;*t;t++)H=H*33+(unsigned char)*t; }
static int num_hook(double d,char*b){ return snprintf(b?b:(char[64]){0},64,"%.2f",d);} 
#ifdef BASE
#define TAKE(c,call) do{ JSON_Value*c_=(c); int r_=call; if(r_!=JSONSuccess) json_value_free(c_); r=r_; }while(0)
#else
#define TAKE(c,call) do{ JSON_Value*c_=(c); r=call; }while(0)
#endif
int main(int argc,char**argv){ int seeds=atoi(argv[1]), iters=atoi(argv[2]); int fail=argc>3?atoi(argv[3]):0; json_set_allocation_functions(m,f); long ops=0;
 for(int sd=1;sd<=seeds;sd++){ s=sd*2654435761u|1; srand(sd); failp=0; JSON_Value *root=json_value_init_object(); failp=fail; 
  for(int i=0;i<iters;i++){ ops++; int r=0; JSON_Value *v=pick(root); JSON_Object*o=json_object(v); JSON_Array*a=json_array(v); size_t n=json_array_get_count(a); char tmp[200];
   switch(rnd(24)){
   case 0: {const char*k=key(); TAKE(gen(0),json_object_set_value(o,k,c_));} break;
   case 1: {const char*p=path(); TAKE(gen(0),json_object_dotset_value(o,p,c_));} break;
   case 2: TAKE(gen(0),json_array_append_value(a,c_)); break;
   case 3: {unsigned ix=rnd(n+2); TAKE(gen(0),json_array_replace_value(a,ix,c_));} break;
   case 4: r=json_object_remove(o,key()); break;
   case 5: r=json_object_dotremove(o,path()); break;
   case 6: r=json_array_remove(a,rnd(n+2)); break;
   case 7: if(rnd(10)==0){ if(o) r=json_object_clear(o); else r=json_array_clear(a);} break;
   case 8: {JSON_Value*c=json_value_deep_copy(v); if(!fail&&!json_value_equals(c,v)) abort(); if(c&&!fail&&json_validate(c,v)!=JSONSuccess&&json_array(v)==NULL) r=1; json_value_free(c);} break;
   case 9: {char*t=json_serialize_to_string(v); out(t); JSON_Value*c=t?json_parse_string(t):NULL; if(t&&!fail&&!c) abort(); json_value_free(c); json_free_serialized_string(t);} break;
   case 10:{char*t=json_serialize_to_string_pretty(v); out(t); JSON_Value*c=t?json_parse_string(t):NULL; char*u=json_serialize_to_string_pretty(c); if(t&&u&&strcmp(t,u)&&!fail) r=2; json_value_free(c); json_free_serialized_string(u); json_free_serialized_string(t);} break;
   case 11: r=json_object_set_string(o,key(),rnd(5)?"s":NULL); break;
   case 12: r=json_object_set_number(o,key(),rnd(100)); break;
   case 13: r=json_object_dotset_string(o,path(),rnd(9)?"\xff":"ok"); break;
   case 14: r=json_object_dotset_boolean(o,path(),1); break;
   case 15: r=json_array_append_string(a,"q"); break;
   case 16: r=json_array_replace_number(a,rnd(n+2),0.0/0.0+rnd(2)); break;
   case 17: r=json_array_append_null(a); break;
   case 18: json_set_float_serialization_format(rnd(2)?"%.3f":NULL); break;
   case 19: json_set_number_serialization_function(rnd(3)==0?num_hook:NULL); break;
   case 20: json_set_escape_slashes(rnd(2)); break;
   case 21: {const char*k=key(); out(json_object_dotget_string(o,path())); r=json_object_has_value(o,k)+json_object_dothas_value_of_type(o,path(),JSONObject)+(int)json_object_get_number(o,k); const char*q=json_string(v); if(q)out(q); r+=(int)json_string_len(v);} break;
   case 22: {JSON_Value*c=json_value_init_null(); int r1=json_object_set_value(o,"twice",c); if(r1==JSONSuccess){ if(json_object_set_value(o,"t2",c)==JSONSuccess) abort(); }
#ifdef BASE
     else json_value_free(c);
#endif
     r=r1;} break;
   case 23: {char buf[300]; r=json_serialize_to_buffer(v,buf,sizeof buf); if(r==JSONSuccess) out(buf);} break;
   }
   snprintf(tmp,200,"%d %zu;",r,json_object_get_count(json_object(root))); out(tmp);
   if(i%50==49&&json_serialization_size(root)>20000) json_object_clear(json_object(root));
  }
  json_set_float_serialization_format(NULL); json_set_number_serialization_function(NULL); failp=0;
  char*t=json_serialize_to_string(root); out(t); json_free_serialized_string(t); json_value_free(root);
  if(live!=0){ printf("LEAK %ld seed %d\n",live,sd); return 1; }
  if(sd%50==0||sd==seeds) printf("%d %lx\n",sd,H);
 } printf("ok %ld ops\n",ops); return 0; }
