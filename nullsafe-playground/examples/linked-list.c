// Nullsafe C tracks struct member nullability through
// control flow — not just local variables.

typedef struct Node {
    int value;
    struct Node* next;  // nullable by default
} Node;

// Classic linked list traversal — no warnings
void traverse(Node* head) {
    Node* p = head;
    while (p) {
        p->value = 0;   // OK — p checked by while
        p = p->next;    // OK — p is non-null, safe to access ->next
    }
}

// Forgot to check: warning
void buggy_second(Node* head) {
    if (head) {
        head->next->value = 1;  // warning: head->next might be NULL
    }
}

// Fixed: check both levels
void safe_second(Node* head) {
    if (head && head->next) {
        head->next->value = 1;  // OK — both checked
    }
}
