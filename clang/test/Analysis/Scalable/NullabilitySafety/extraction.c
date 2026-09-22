// RUN: rm -rf %t.json
// RUN: %clang_cc1 -fsyntax-only %s --ssaf-extract-summaries=NullabilitySafety \
// RUN:   --ssaf-compilation-unit-id=tu-1 --ssaf-tu-summary-file=%t.json
// RUN: FileCheck %s --input-file=%t.json

struct S { int *f; };
void take(int *p);
int *_Nonnull get(int *_Nullable unused) { static int x; return &x; }
void g(struct S *s, int *_Nullable q) {
  take(q);
  take(get(q));
  s->f = 0;
}

// CHECK:      "entity_id": 1,
// CHECK-NEXT: "entity_summary": {
// CHECK-NEXT:   "AllReturnsNonnull": [
// CHECK-NEXT:     [
// CHECK-NEXT:       {
// CHECK-NEXT:         "@": 0
// CHECK:      "NonnullEvidence": [
// CHECK-NEXT:   [
// CHECK-NEXT:     {
// CHECK-NEXT:       "@": 0
// CHECK:      "NullableEvidence": []
// CHECK:      "entity_id": 5,
// CHECK:      "NonnullEvidence": [
// CHECK-NEXT:   [
// CHECK-NEXT:     {
// CHECK-NEXT:       "@": 2
// CHECK:      "NullableEvidence": [
// CHECK-NEXT:   [
// CHECK-NEXT:     {
// CHECK-NEXT:       "@": 2
// CHECK:            "@": 3
// CHECK:            "@": 4
// CHECK:      "summary_name": "NullabilitySafety"

// CHECK:      "id": 5,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "",
// CHECK-NEXT:   "usr": "c:@F@g"
// CHECK:      "id": 1,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "",
// CHECK-NEXT:   "usr": "c:@F@get"
// CHECK:      "id": 0,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "0",
// CHECK-NEXT:   "usr": "c:@F@get"
// CHECK:      "id": 3,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "1",
// CHECK-NEXT:   "usr": "c:@F@get"
// CHECK:      "id": 2,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "1",
// CHECK-NEXT:   "usr": "c:@F@take"
// CHECK:      "id": 4,
// CHECK-NEXT: "name": {
// CHECK-NEXT:   "suffix": "",
// CHECK-NEXT:   "usr": "c:@S@S@FI@f"
