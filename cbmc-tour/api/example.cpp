// Driving CBMC from C++ via libcprover-cpp.
//
// This is the embedding API: instead of shelling out to the `cbmc` binary and
// parsing --json-ui, you own the goto model and the verification loop.
//
// NOT COMPILED BY run.sh: the Debian/Ubuntu `cbmc` package ships binaries only.
// You need a source build of CBMC to get api.h and libcprover-cpp:
//
//   git clone https://github.com/diffblue/cbmc && cd cbmc
//   cmake -S . -B build && cmake --build build -j
//
// then build this file with the CMakeLists.txt next to it, or by hand:
//
//   c++ -std=c++17 example.cpp \
//       -I <cbmc>/src -I <cbmc>/build/src \
//       -L <cbmc>/build/lib -lcprover-api-cpp -lgoto-checker -lgoto-programs \
//       -lansi-c -lutil ... -o example
//
// (the exact link line is long and version-dependent; prefer the CMake route,
//  which picks it up from CBMC's own exported targets.)

#include <cprover/api.h>

#include <iostream>
#include <string>
#include <vector>

namespace
{
// The API pushes progress and diagnostics through a plain C function pointer
// plus an opaque context, so it never has to know about your logging.
struct log_contextt
{
  bool verbose;
  unsigned error_count;
};

void on_message(const api_messaget &message, api_call_back_contextt context)
{
  auto *ctx = static_cast<log_contextt *>(context);

  if(api_message_is_error(message))
  {
    ++ctx->error_count;
    std::cerr << "error: " << api_message_get_string(message) << '\n';
  }
  else if(ctx->verbose)
  {
    std::cout << "  | " << api_message_get_string(message) << '\n';
  }
  // NOTE: the api_messaget only lives for the duration of this call. Copy
  // anything you need to keep.
}

const char *to_string(prop_statust status)
{
  switch(status)
  {
  case prop_statust::NOT_CHECKED:   return "NOT_CHECKED";
  case prop_statust::UNKNOWN:       return "UNKNOWN";
  case prop_statust::NOT_REACHABLE: return "NOT_REACHABLE";
  case prop_statust::PASS:          return "PASS";
  case prop_statust::FAIL:          return "FAIL";
  case prop_statust::ERROR:         return "ERROR";
  }
  return "?";
}
} // namespace

int main(int argc, char *argv[])
{
  if(argc < 2)
  {
    std::cerr << "usage: " << argv[0] << " <file.c|file.goto> ...\n";
    return 2;
  }

  // 1. Options: a small fluent builder that lowers to CBMC's internal optionst.
  const api_optionst options = api_optionst::create()
                                 .simplify(true)
                                 .drop_unused_functions(true)
                                 .validate_goto_model(true);

  // 2. The session owns the goto model, the message handler and the options.
  api_sessiont session{options};

  log_contextt log_context{/*verbose=*/true, /*error_count=*/0};
  session.set_message_callback(&on_message, &log_context);

  std::cout << "CBMC API version " << *session.get_api_version() << "\n\n";

  // 3. Load a model. Source files go through the front end; goto binaries are
  //    read directly. is_goto_binary() lets you accept either.
  std::vector<std::string> files{argv + 1, argv + argc};

  std::string first = files.front();
  if(files.size() == 1 && session.is_goto_binary(first))
  {
    std::cout << "reading goto binary " << first << "\n";
    session.read_goto_binary(first);
  }
  else
  {
    std::cout << "compiling " << files.size() << " source file(s)\n";
    session.load_model_from_files(files);
  }

  // 4. Optional transformations on the loaded model.
  session.validate_goto_model();
  session.drop_unused_functions();

  // 5. verify_model() = preprocess_model() + run_verifier():
  //      remove asm -> link CPROVER libc models -> lower function pointers,
  //      vtables and returns -> label properties -> symex -> solve.
  std::cout << "\nverifying...\n";
  const std::unique_ptr<verification_resultt> result = session.verify_model();

  if(!result)
  {
    std::cerr << "verification produced no result (front-end error?)\n";
    return 6;
  }

  // 6. Results are per-property, not a wall of text.
  std::cout << "\nproperties:\n";
  for(const std::string &id : result->get_property_ids())
  {
    std::cout << "  [" << id << "] "
              << result->get_property_description(id) << ": "
              << to_string(result->get_property_status(id)) << '\n';
  }

  const verifier_resultt final = result->final_result();
  std::cout << "\nverdict: "
            << (final == verifier_resultt::PASS    ? "PASS"
                : final == verifier_resultt::FAIL  ? "FAIL"
                : final == verifier_resultt::ERROR ? "ERROR"
                                                   : "UNKNOWN")
            << '\n';

  // Same exit codes as the cbmc binary: 0 pass, 10 fail, 6 error.
  return verifier_result_to_exit_code(final);
}
