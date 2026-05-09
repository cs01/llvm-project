// Smart pointers are tracked for nullability just like raw pointers.
// unique_ptr, shared_ptr, new, and move semantics are all understood.

#include <memory>

struct Sensor {
    int id;
    double read() { return id * 1.5; }
};

// --- Factories and new-expressions produce nonnull pointers ---

void factories() {
    auto u = std::make_unique<Sensor>();
    u->read();                // OK — make_unique never returns null

    auto s = std::make_shared<Sensor>();
    s->read();                // OK — make_shared never returns null

    Sensor* raw = new Sensor;
    raw->read();              // OK — throwing new never returns null
}

// --- new(nothrow) CAN return null ---

void nothrow_new() {
    Sensor* p = new (std::nothrow) Sensor;
    p->read();                // warning: nothrow new may return null

    if (p) {
        p->read();            // OK — checked
    }
}

// --- Default-constructed smart pointers are nullable ---

void default_constructed() {
    std::unique_ptr<Sensor> u;
    u->read();                // warning: default-constructed, null

    std::shared_ptr<Sensor> s;
    s->read();                // warning: default-constructed, null
}

// --- std::move transfers ownership, source becomes nullable ---

void move_semantics() {
    auto owner = std::make_unique<Sensor>();
    owner->read();            // OK — just created

    auto new_owner = std::move(owner);
    new_owner->read();        // OK — received ownership
    owner->read();            // warning: moved-from, might be null
}

// --- reset() changes nullability ---

void reset_patterns() {
    auto p = std::make_unique<Sensor>();
    p->read();                // OK

    p.reset();                // now null
    p->read();                // warning: reset to null

    p.reset(new Sensor);      // now nonnull again
    p->read();                // OK — reset with non-null value
}

// --- Reassignment from factory re-narrows ---

void reassignment() {
    std::unique_ptr<Sensor> p;
    p->read();                // warning: default-constructed

    p = std::make_unique<Sensor>();
    p->read();                // OK — reassigned from make_unique

    p = std::unique_ptr<Sensor>(new Sensor());
    p->read();                // OK — new-expression is nonnull
}

// --- Flow narrowing works the same as raw pointers ---

void check_before_use(const std::unique_ptr<Sensor>& sensor) {
    sensor->read();           // warning: might be null

    if (sensor) {
        sensor->read();       // OK — checked
    }
}

void early_return(const std::shared_ptr<Sensor>& s) {
    if (!s) return;
    s->read();                // OK — null path returned
}
