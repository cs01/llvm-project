// _Nullable and _Nonnull work on smart pointers too. Put them on the
// declaration and the compiler enforces them at every use: a _Nonnull
// member never needs a check, a _Nullable factory result always does.
//
// Two bugs below.

#include <memory>

struct Shader { void use(); };
struct Device { void present(); };

std::unique_ptr<Shader> _Nullable compile_shader(const char* src);
std::unique_ptr<Shader> _Nonnull default_shader();

struct Renderer {
    std::unique_ptr<Device> _Nonnull device;
    std::unique_ptr<Shader> shader;

    void frame() {
        device->present();
    }

    void load(const char* src) {
        shader = compile_shader(src);
        shader->use();
    }

    void load_or_default(const char* src) {
        shader = compile_shader(src);
        if (!shader)
            shader = default_shader();
        shader->use();
    }

    void reload(const char* src) {
        auto next = compile_shader(src);
        next->use();
        shader = std::move(next);
    }
};
