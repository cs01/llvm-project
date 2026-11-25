#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
/* A1 */ int ref_uaf(void){cJSON *_Owned _Nullable n=cJSON_CreateNumber(1);if(!n)return 0;cJSON *_Owned _Nullable r=cJSON_CreateArrayReference(n);cJSON_Delete(n);int s=cJSON_GetArraySize(r);cJSON_Delete(r);return s;}
/* A2 */ void flag(void){cJSON *_Owned _Nullable o=cJSON_CreateStringReference("lit");if(!o)return;o->type=cJSON_String;cJSON_Delete(o);}
/* A3 */ void take_ref(void){cJSON *_Owned _Nullable n=cJSON_CreateNumber(1);if(!n)return;cJSON *_Owned _Nullable r=cJSON_CreateArrayReference(n);if(!r){cJSON_Delete(n);return;}cJSON_Delete(_Take(r->child));cJSON_Delete(n);}
/* A4 */ void addref(cJSON*a,cJSON*_Owned i){cJSON_AddItemReferenceToArray(a,i);cJSON_Delete(i);}
/* A5 */ char errp(char *_Owned _Nullable __null_terminated t){if(!t)return 0;cJSON_Delete(cJSON_Parse(t));const char *_Nullable __unsafe_indexable e=cJSON_GetErrorPtr();free(t);__unsafe{return e?*e:0;}}
/* A6 */ char endp(char *_Owned _Nullable __null_terminated t){if(!t)return 0;const char *_Unchecked _Nullable __unsafe_indexable e=0;cJSON_Delete(cJSON_ParseWithOpts(t,&e,0));free(t);__unsafe{return e?*e:0;}}
/* A7 */ int added(cJSON*a){cJSON *_Owned _Nullable n=cJSON_CreateNumber(1);if(!n)return 0;cJSON_AddItemToArray(a,n);return n->valueint;}
/* A8 */ int stale(cJSON*a){cJSON*_Nullable c=cJSON_GetArrayItem(a,0);cJSON_DeleteItemFromArray(a,0);return c?c->valueint:0;}
/* A9 */ void dynkey(cJSON*a,const char*k,cJSON*_Owned i){cJSON_AddItemToObjectCS(a,k,i);}
/* A10 */ void self(cJSON*a){cJSON_AddItemToArray(a,a);}
/* A11 */ int str(cJSON*_Owned a){char*_Nullable s=cJSON_GetStringValue(a);cJSON_Delete(a);return s?*s:0;}
/* A12 */ void twice(cJSON*a,cJSON*_Owned i){cJSON_AddItemToArray(a,i);cJSON_AddItemToArray(a,i);}
/* A13 */ void fail(cJSON*a,cJSON*_Owned i){if(!cJSON_AddItemToArray(a,i))cJSON_Delete(i);}
/* A14 */ void freeprev(cJSON*a){cJSON*_Nullable c=a->child;if(c){cJSON_Delete(c->prev);}}
/* A15 */ void cyc(cJSON*a){cJSON*_Owned _Nullable c=_Take(a->child);if(c){c->child=c;}}
/* A16 */ void rep(cJSON*a){cJSON*_Nullable c=a->child;cJSON*_Nullable k=c?c->child:0;cJSON_ReplaceItemViaPointer(a,c,cJSON_CreateNull());if(k)k->type=0;}
