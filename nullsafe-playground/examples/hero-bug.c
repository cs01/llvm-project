// Standard clang compiles this with ZERO warnings.
// But there's a null pointer crash hiding here.
// Nullsafe C catches it at compile time.

typedef struct Config {
    int timeout;
    int retries;
} Config;

Config* _Nullable load_config(const char* path);

void apply_config(const char* path) {
    Config* cfg = load_config(path);
    if (!cfg) {
        // Programmer thought they handled it...
        // but forgot to return!
    }
    cfg->timeout = 30;   // BUG: cfg can be NULL here
    cfg->retries = 3;    // BUG: still nullable
}
