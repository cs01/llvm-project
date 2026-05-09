// Late initialization: fields start null and get set up later.
// Nullsafe warns on every unguarded use — two ways to fix it.

#include <cassert>

struct Config {
    int timeout;
    int max_retries;
};

struct Logger {
    void log(const char* msg) { (void)msg; }
};

// --- Problem: nullable fields warn on every dereference ---

struct Service {
    Config* config_ = nullptr;
    Logger* logger_ = nullptr;

    void init(Config* c, Logger* l) {
        config_ = c;
        logger_ = l;
    }

    void run() {
        int t = config_->timeout;       // warning: nullable dereference
        logger_->log("starting");       // warning: nullable dereference
    }
};

// --- Fix 1: guarded getter returns _Nonnull ---
// Centralizes the null check. Callers get a clean, nonnull pointer.

struct ServiceWithGetter {
    Config* config_ = nullptr;
    Logger* logger_ = nullptr;

    void init(Config* c, Logger* l) {
        config_ = c;
        logger_ = l;
    }

    Config* _Nonnull config() {
        assert(config_);
        return config_;
    }

    Logger* _Nonnull logger() {
        assert(logger_);
        return logger_;
    }

    void run() {
        int t = config()->timeout;       // clean — getter guarantees nonnull
        logger()->log("starting");       // clean
    }
};

// --- Fix 2: require nonnull at construction ---
// Eliminates the two-phase init entirely.

struct ServiceNonnull {
    Config* _Nonnull config_;
    Logger* _Nonnull logger_;

    ServiceNonnull(Config* _Nonnull c, Logger* _Nonnull l)
        : config_(c), logger_(l) {}

    void run() {
        int t = config_->timeout;       // clean — always nonnull
        logger_->log("starting");       // clean
    }
};

// Callers must provide nonnull — nullable is caught at the call site
void create_service(Config* maybe_config, Logger* _Nonnull log) {
    ServiceNonnull s1(maybe_config, log);  // warning: nullable to nonnull

    if (maybe_config) {
        ServiceNonnull s2(maybe_config, log);  // OK — checked
    }
}
