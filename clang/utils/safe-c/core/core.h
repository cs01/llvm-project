// Marks for the core subset, written like _Owned after the '*' of a parameter. The function takes the owner only when it returns true / zero.
#include <stdlib.h>
#include <safec.h>
#define _Taken_if_true [[clang::annotate_type("safec_taken_if_true")]]
#define _Taken_if_zero [[clang::annotate_type("safec_taken_if_zero")]]
// Escape hatch: the body is not checked, the signature is.
#define _Trusted __attribute__((annotate("safec_trusted")))
