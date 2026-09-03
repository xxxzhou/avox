#include <math.h>
#include <emscripten/emscripten.h>

extern "C" {

EMSCRIPTEN_KEEPALIVE int int_sqrt(int x) {
  return sqrt(x);
}

}

// emcc hello_function.cpp -o function.html -s EXPORTED_FUNCTIONS=_int_sqrt -s EXPORTED_RUNTIME_METHODS=ccall,cwrap
