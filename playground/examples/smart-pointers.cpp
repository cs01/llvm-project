// Smart pointers get the same flow tracking as raw pointers.
// std::move and reset() leave a unique_ptr null, and the compiler
// follows that through the method just like it follows a null check.
//
// Two bugs below. restart() is the correct version.

#include <memory>
#include <vector>

struct Connection {
    void send(const char* msg);
};

struct Server {
    std::unique_ptr<Connection> active = std::make_unique<Connection>();
    std::vector<std::unique_ptr<Connection>> idle;

    void park() {
        idle.push_back(std::move(active));
        active->send("parked");
    }

    void shutdown() {
        active.reset();
        active->send("bye");
    }

    void restart() {
        active.reset();
        active = std::make_unique<Connection>();
        active->send("hello");
    }
};
