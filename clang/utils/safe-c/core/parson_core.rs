// parson's value/object/array/parse core, function by function, as a referee for
// safe-c-notes/core-subset.md: rustc checks the borrow rules. Subset limits:
// no reference in any struct, no named lifetime, no Rc/RefCell/unsafe, no clone
// to get around a borrow. Vec<Option<Box<T>>> stands for a counted array of owners.
// "CHANGED n:" marks code that differs in shape from the C.
#![forbid(unsafe_code)]
const NONE: usize = usize::MAX;
pub enum Value { Null, Bool(bool), Num(f64), Str(Box<str>), Obj(Box<Object>), Arr(Box<Array>) }
#[derive(Default)]
pub struct Object { cells: Vec<usize>, hashes: Vec<u64>, names: Vec<Option<Box<str>>>, values: Vec<Option<Box<Value>>>, cell_ixs: Vec<usize>, count: usize, item_capacity: usize }
#[derive(Default)]
pub struct Array { items: Vec<Option<Box<Value>>>, count: usize }
// The caller keeps what a failed call did not take (CHANGED 1: conditional ownership).
type Kept<T> = Result<(), T>;

fn hash(s: &str) -> u64 { s.bytes().fold(5381u64, |h, c| (h << 5).wrapping_add(h).wrapping_add(c as u64)) }
impl Value {
    fn get_object(&self) -> Option<&Object> { if let Value::Obj(o) = self { Some(o) } else { None } }
    fn get_array(&self) -> Option<&Array> { if let Value::Arr(a) = self { Some(a) } else { None } }
    // CHANGED 2: second version of each getter whose C result is modified.
    fn get_object_mut(&mut self) -> Option<&mut Object> { if let Value::Obj(o) = self { Some(o) } else { None } }
    fn get_array_mut(&mut self) -> Option<&mut Array> { if let Value::Arr(a) = self { Some(a) } else { None } }
    fn get_number(&self) -> f64 { if let Value::Num(n) = self { *n } else { 0.0 } }
}
impl Object {
    fn init(cap: usize) -> Object {
        let items = cap * 7 / 10;
        let mut o = Object { cells: vec![NONE; cap], hashes: vec![0; items], names: vec![], values: vec![], cell_ixs: vec![0; items], count: 0, item_capacity: items };
        o.names.resize_with(items, || None); o.values.resize_with(items, || None); o
    }
    fn cell_ix(&self, key: &str, hash: u64, found: &mut bool) -> usize {
        *found = false;
        let cap = self.cells.len(); if cap == 0 { return NONE; }
        let start = hash as usize & (cap - 1);
        for i in 0..cap {
            let ix = (start + i) & (cap - 1);
            let cell = self.cells[ix];
            if cell == NONE { return ix; }
            if hash != self.hashes[cell] { continue; }
            if self.names[cell].as_deref() == Some(key) { *found = true; return ix; }
        }
        NONE
    }
    // CHANGED 3: owners move out of the old arrays (C copies the pointers and rolls back).
    fn grow_and_rehash(&mut self) -> bool {
        let mut n = Object::init((self.cells.len() * 2).max(16));
        for i in 0..self.count {
            let (Some(k), Some(v)) = (self.names[i].take(), self.values[i].take()) else { return false };
            if n.add(k, v).is_err() { return false; }
        }
        *self = n; true
    }
    fn add(&mut self, name: Box<str>, value: Box<Value>) -> Kept<(Box<str>, Box<Value>)> {
        let h = hash(&name); let mut found = false;
        let mut cell = self.cell_ix(&name, h, &mut found);
        if found { return Err((name, value)); }
        if self.count >= self.item_capacity {
            if !self.grow_and_rehash() { return Err((name, value)); }
            cell = self.cell_ix(&name, h, &mut found);
        }
        let c = self.count;
        self.names[c] = Some(name); self.cells[cell] = c; self.values[c] = Some(value);
        self.cell_ixs[c] = cell; self.hashes[c] = h; self.count += 1;
        Ok(())
    }
    fn get(&self, name: &str) -> Option<&Value> {
        let mut found = false; let cell = self.cell_ix(name, hash(name), &mut found);
        if !found { return None; }
        self.values[self.cells[cell]].as_deref()
    }
    fn get_mut(&mut self, name: &str) -> Option<&mut Value> {
        let mut found = false; let cell = self.cell_ix(name, hash(name), &mut found);
        if !found { return None; }
        self.values[self.cells[cell]].as_deref_mut()
    }
    fn dotget(&self, name: &str) -> Option<&Value> {
        let Some(dot) = name.find('.') else { return self.get(name) };
        let object = self.get(&name[..dot])?.get_object()?;
        object.dotget(&name[dot + 1..])
    }
    fn remove(&mut self, name: &str) -> Option<Box<Value>> { // CHANGED 4: returns the owner (C: free_value flag)
        let mut found = false; let cell = self.cell_ix(name, hash(name), &mut found);
        if !found { return None; }
        let item = self.cells[cell];
        let val = self.values[item].take();
        self.names[item] = None;
        let last = self.count - 1;
        if item < last {
            let n = self.names[last].take(); self.names[item] = n;
            let v = self.values[last].take(); self.values[item] = v;
            self.cell_ixs[item] = self.cell_ixs[last]; self.hashes[item] = self.hashes[last];
            self.cells[self.cell_ixs[item]] = item;
        }
        self.count -= 1;
        let cap = self.cells.len(); let (mut i, mut j) = (cell, cell);
        for _ in 0..cap - 1 {
            j = (j + 1) & (cap - 1);
            if self.cells[j] == NONE { break; }
            let k = self.hashes[self.cells[j]] as usize & (cap - 1);
            if (j > i && (k <= i || k > j)) || (j < i && (k <= i && k > j)) {
                self.cell_ixs[self.cells[j]] = i; self.cells[i] = self.cells[j]; i = j;
            }
        }
        self.cells[i] = NONE;
        val
    }
    fn dotremove(&mut self, name: &str) -> Option<Box<Value>> {
        let Some(dot) = name.find('.') else { return self.remove(name) };
        self.get_mut(&name[..dot])?.get_object_mut()?.dotremove(&name[dot + 1..])
    }
    fn set_value(&mut self, name: &str, value: Box<Value>) -> Kept<Box<Value>> {
        let h = hash(name); let mut found = false;
        let mut cell = self.cell_ix(name, h, &mut found);
        if found { let item = self.cells[cell]; self.values[item] = Some(value); return Ok(()); }
        if self.count >= self.item_capacity {
            if !self.grow_and_rehash() { return Err(value); }
            cell = self.cell_ix(name, h, &mut found);
        }
        let c = self.count;
        self.names[c] = Some(name.into()); self.cells[cell] = c; self.values[c] = Some(value);
        self.cell_ixs[c] = cell; self.hashes[c] = h; self.count += 1;
        Ok(())
    }
    fn dotset_value(&mut self, name: &str, value: Box<Value>) -> Kept<Box<Value>> {
        let Some(dot) = name.find('.') else { return self.set_value(name, value) };
        let (head, tail) = (&name[..dot], &name[dot + 1..]);
        if let Some(t) = self.get_mut(head) {
            return match t.get_object_mut() { Some(o) => o.dotset_value(tail, value), None => Err(value) };
        }
        let mut nv = Box::new(Value::Obj(Box::default()));
        let Some(no) = nv.get_object_mut() else { return Err(value) };
        no.dotset_value(tail, value)?;
        // CHANGED 5: get the moved value back before undoing (C uses new_object, a borrow of it).
        if let Err((_, mut back)) = self.add(head.into(), nv) {
            return match back.get_object_mut().and_then(|o| o.dotremove(tail)) { Some(v) => Err(v), None => Ok(()) };
        }
        Ok(())
    }
}
impl Array {
    fn add(&mut self, v: Box<Value>) -> Kept<Box<Value>> {
        if self.count >= self.items.len() { let n = (self.items.len() * 2).max(16); self.items.resize_with(n, || None); }
        self.items[self.count] = Some(v); self.count += 1; Ok(())
    }
    fn get(&self, i: usize) -> Option<&Value> { self.items.get(i)?.as_deref() }
    fn get_mut(&mut self, i: usize) -> Option<&mut Value> { self.items.get_mut(i)?.as_deref_mut() }
    fn replace(&mut self, i: usize, v: Box<Value>) -> Kept<Box<Value>> { if i >= self.count { return Err(v); } self.items[i] = Some(v); Ok(()) }
    fn remove(&mut self, ix: usize) -> bool { // CHANGED 6: moves one by one (C: memmove of owners)
        if ix >= self.count { return false; }
        self.items[ix] = None;
        for i in ix..self.count - 1 { let t = self.items[i + 1].take(); self.items[i] = t; }
        self.count -= 1; true
    }
}
fn deep_copy(v: &Value) -> Option<Box<Value>> {
    Some(Box::new(match v {
        Value::Arr(a) => { let mut out = Box::<Array>::default(); for i in 0..a.count { out.add(deep_copy(a.get(i)?)?).ok()?; } Value::Arr(out) }
        Value::Obj(o) => { let mut out = Box::<Object>::default(); for i in 0..o.count { let k = o.names[i].as_deref()?; out.add(k.into(), deep_copy(o.get(k)?)?).ok()?; } Value::Obj(out) }
        Value::Str(s) => Value::Str(s.as_ref().into()),
        Value::Num(n) => Value::Num(*n), Value::Bool(b) => Value::Bool(*b), Value::Null => Value::Null,
    }))
}
// CHANGED 7: the cursor is an index (C: const char **string, a borrow behind a pointer).
fn at(s: &str, p: usize) -> u8 { s.as_bytes().get(p).copied().unwrap_or(0) }
fn ws(s: &str, p: &mut usize) { while at(s, *p).is_ascii_whitespace() { *p += 1; } }
fn parse_value(s: &str, p: &mut usize, depth: usize) -> Option<Box<Value>> {
    if depth > 2048 { return None; }
    ws(s, p);
    match at(s, *p) {
        b'{' => {
            let mut out = Box::new(Value::Obj(Box::default()));
            let obj = out.get_object_mut()?;
            *p += 1; ws(s, p);
            if at(s, *p) == b'}' { *p += 1; return Some(out); }
            loop {
                let key = quoted(s, p)?; ws(s, p);
                if at(s, *p) != b':' { return None; }
                *p += 1;
                let v = parse_value(s, p, depth + 1)?;
                obj.add(key, v).ok()?; ws(s, p);
                if at(s, *p) != b',' { break; }
                *p += 1; ws(s, p);
            }
            if at(s, *p) != b'}' { return None; }
            *p += 1; Some(out)
        }
        b'[' => {
            let mut out = Box::new(Value::Arr(Box::default()));
            let arr = out.get_array_mut()?;
            *p += 1; ws(s, p);
            if at(s, *p) == b']' { *p += 1; return Some(out); }
            loop { let v = parse_value(s, p, depth + 1)?; arr.add(v).ok()?; ws(s, p); if at(s, *p) != b',' { break; } *p += 1; }
            if at(s, *p) != b']' { return None; }
            *p += 1; Some(out)
        }
        b'"' => Some(Box::new(Value::Str(quoted(s, p)?))),
        b't' => { *p += 4; Some(Box::new(Value::Bool(true))) }
        b'n' => { *p += 4; Some(Box::new(Value::Null)) }
        _ => { let a = *p; while matches!(at(s, *p), b'0'..=b'9' | b'-' | b'.') { *p += 1; } Some(Box::new(Value::Num(s[a..*p].parse().ok()?))) }
    }
}
fn quoted(s: &str, p: &mut usize) -> Option<Box<str>> {
    if at(s, *p) != b'"' { return None; }
    *p += 1; let a = *p;
    while at(s, *p) != b'"' { if at(s, *p) == 0 { return None; } *p += 1; }
    let r = s[a..*p].into(); *p += 1; Some(r)
}
fn num(n: f64) -> Box<Value> { Box::new(Value::Num(n)) }
fn main() {
    let mut p = 0;
    let mut v = parse_value(r#"{"a":{"b":[1,2,{"c":3}]},"s":"x"}"#, &mut p, 0).unwrap();
    let copy = deep_copy(&v).unwrap();
    let o = v.get_object_mut().unwrap();
    for i in 0..200 { assert!(o.set_value(&format!("k{i}"), num(i as f64)).is_ok()); }
    for i in (0..200).step_by(2) { assert!(o.remove(&format!("k{i}")).is_some()); }
    for i in 0..200 { assert_eq!(o.get(&format!("k{i}")).map(|x| x.get_number()), if i % 2 == 1 { Some(i as f64) } else { None }); }
    assert!(o.dotset_value("x.y.z", num(7.0)).is_ok());
    assert_eq!(o.dotget("x.y.z").unwrap().get_number(), 7.0);
    assert!(o.dotset_value("s.q", num(1.0)).is_err()); // caller keeps the value
    assert!(o.dotremove("x.y.z").is_some());
    let arr = o.get_mut("a").unwrap().get_object_mut().unwrap().get_mut("b").unwrap().get_array_mut().unwrap();
    assert!(arr.remove(0)); assert!(arr.replace(0, num(9.0)).is_ok()); assert!(arr.replace(9, num(9.0)).is_err());
    assert_eq!(arr.get_mut(0).unwrap().get_number(), 9.0);
    assert_eq!(copy.get_object().unwrap().dotget("a.b").unwrap().get_array().unwrap().get(0).unwrap().get_number(), 1.0);
    println!("ok");
}
