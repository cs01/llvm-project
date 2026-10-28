// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fnullability-safety-cir -fnullability-safety-cir-facts=%t.json %s -o /dev/null
// RUN: FileCheck --input-file=%t.json %s

struct Node {
  int value;
  Node *next;
};

int length(Node *head) {
  int n = 0;
  while (head) {
    ++n;
    head = head->next;
  }
  return n;
}

int first(Node *head) { return head->value; }

void push(Node **list, Node *node) {
  node->next = *list;
  *list = node;
}

void init(Node *n) { n->next = nullptr; }

Node *find(Node *head, int v) {
  for (Node *n = head; n; n = n->next)
    if (n->value == v)
      return n;
  return nullptr;
}

int wrap(Node *head) { return first(head); }

void caller() { length(nullptr); }

[[noreturn]] void fail();

int checked(Node *p) {
  if (!p)
    fail();
  return p->value;
}

Node *self(Node &n) { return &n; }

int nullableAnnotated(Node *_Nullable p) { return p ? p->value : 0; }

// CHECK-LABEL: "demangled": "length(Node*)",
// CHECK:       "name": "head",
// CHECK:       "null_checked": true,
// CHECK:       "callers_pass_null": true
// CHECK:       "inferred": "nullable"

// CHECK-LABEL: "demangled": "first(Node*)",
// CHECK:       "deref_unchecked": true,
// CHECK:       "inferred": "nonnull"

// CHECK-LABEL: "demangled": "push(Node**, Node*)",
// CHECK:       "name": "list",
// CHECK:       "inferred": "nonnull"
// CHECK-LABEL: "demangled": "push(Node**, Node*)",
// CHECK:       "name": "node",
// CHECK:       "inferred": "nonnull"

// CHECK-LABEL: "kind": "field",
// CHECK-NEXT:  "record": "Node",
// CHECK-NEXT:  "name": "next",
// CHECK:       "assigned_null": true,
// CHECK:       "inferred": "nullable"

// CHECK-LABEL: "demangled": "find(Node*, int)",
// CHECK:       "name": "head",
// CHECK:       "null_checked": true,
// CHECK:       "inferred": "nullable"
// CHECK-LABEL: "kind": "return",
// CHECK-NEXT:  "function": "_Z4findP4Nodei",
// CHECK:       "assigned_null": true,
// CHECK:       "all_paths_nonnull": false
// CHECK:       "inferred": "nullable"

// CHECK-LABEL: "demangled": "wrap(Node*)",
// CHECK:       "forwarded_to_nonnull": true,
// CHECK:       "inferred": "nonnull"

// CHECK-LABEL: "demangled": "checked(Node*)",
// CHECK:       "null_checked": false,
// CHECK-NEXT:  "asserted_nonnull": true,
// CHECK:       "inferred": "nonnull"

// CHECK-LABEL: "demangled": "self(Node&)",
// CHECK:       "declared": "abi_nonnull",
// CHECK:       "inferred": "nonnull"

// CHECK-LABEL: "demangled": "nullableAnnotated(Node*)",
// CHECK:       "declared": "nullable",
// CHECK:       "inferred": "nullable"
