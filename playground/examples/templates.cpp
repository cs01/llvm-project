// Nullability is part of the type, so it flows through templates.
// A member template returning T* _Nullable is checked at every call,
// and a class template carries whatever nullability its argument has.
//
// Two bugs below.

struct Health { int hp; };
struct Position { float x, y; };
struct Texture { void bind(); };

struct Entity {
    template <class T>
    T* _Nullable get();
};

void take_hit(Entity& e, int damage) {
    e.get<Health>()->hp -= damage;
}

void nudge(Entity& e, float dx) {
    if (auto* pos = e.get<Position>())
        pos->x += dx;
}

template <class V>
struct Registry {
    V find(const char* name);
};

Registry<Texture* _Nonnull> builtin_textures;
Registry<Texture* _Nullable> user_textures;

void draw_skin() {
    builtin_textures.find("white")->bind();
    user_textures.find("skin")->bind();
}
