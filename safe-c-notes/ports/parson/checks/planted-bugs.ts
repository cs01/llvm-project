import {readFileSync,writeFileSync} from "fs"; import {spawnSync} from "child_process";
const src=readFileSync(process.argv[2]+"/parson/parson.c","utf8"), S=process.argv[3];
const B:[string,string,string][]=[
["put: free value then store it (has_parent path)","        json_value_reject(value);\n        return JSONFailure;\n    }\n    json_value_free(_Take(array->items[ix].value));","        json_value_reject(value);\n    }\n    json_value_free(_Take(array->items[ix].value));"],
["remove: free old value without taking it out","json_value_free(_Take(object->values[item_ix].value));\n    release_string","json_value_free(object->values[item_ix].value);\n    release_string"],
["add: use value after it was stored in a failing add (key freed twice)","    if (json_object_add(output_object, new_key, new_value) != JSONSuccess) {\n","    if (json_object_add(output_object, new_key, new_value) != JSONSuccess) {\n            release_string(new_key);\n"],
["grow: copy name pointer instead of moving","names[i].chars = _Take(object->names[i].chars);","names[i].chars = object->names[i].chars;"],
["parse: return value after freeing on missing }","    if (*reader->p != '}') {\n        json_value_free(output_value);\n        return JSONFailure;","    if (*reader->p != '}') {\n        json_value_free(output_value);\n        out->value = output_value;\n        return JSONFailure;"],
["parse_with_comments: release before parse","    result = parse_terminated(terminated);\n    release_string(terminated);","    release_string(terminated);\n    result = parse_terminated(terminated);"],
["object_put: old value used after free","json_value_free(_Take(object->values[item_ix].value));\n        value->parent","JSON_Value *_Nullable old = object->values[item_ix].value;\n        json_value_free(_Take(object->values[item_ix].value));\n        if (old) { value->type = old->type; }\n        value->parent"],
["dotset: borrow of object inside new_value used after new_value is moved","    return json_object_add(object, name_copy, new_value);\n}\n\nJSON_Status json_object_dotset_string","    JSON_Status st = json_object_add(object, name_copy, new_value);\n    if (st == JSONSuccess) { json_object_clear(new_object); }\n    return st;\n}\n\nJSON_Status json_object_dotset_string"],
["grow: cells freed, then object->cells read through stale borrow","    parson_free(_Take(object->cells));\n    parson_free(_Take(object->hashes));","    const size_t *stale = object->cells ? object->cells : cells;\n    parson_free(_Take(object->cells));\n    if (stale) { cell_ix = *stale; }\n    parson_free(_Take(object->hashes));"],
["follow parent in a checked function","static parson_bool_t has_parent(const JSON_Value *value) {\n    return value->parent != NULL;","static parson_bool_t has_parent(const JSON_Value *value) {\n    return value->parent != NULL && value->parent->type;"],
];
for(const [n,a,b] of B){ if(src.split(a).length!==2){console.log("NOMATCH",src.split(a).length-1,n);continue}
 writeFileSync(S+"/pl/bug.c",src.replace(a,()=>b)); const r=spawnSync(process.env.HOME+"/git/llvm-safe-c/build.snap17/bin/clang",["-fsafe-c","-w","-ferror-limit=0","-fsyntax-only",S+"/pl/bug.c","-I"+S+"/pl"],{encoding:"utf8"});
 const e=r.stderr.split("\n").filter(l=>/error:/.test(l)).map(l=>l.replace(/.*bug.c/,"")); console.log(e.length?"REJECTED":"ACCEPTED",n,"|",e[0]??"");}
