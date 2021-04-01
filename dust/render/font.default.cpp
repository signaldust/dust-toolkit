

#include <cstdint>

/**** How to generate these:

  xxd -i default.ttf \
    | sed -e 's/unsigned/static const unsigned/' > font_default.ttf.h
  xxd -i default.mono.ttf \
    | sed -e 's/unsigned/static const unsigned/' > font_default.mono.ttf.h

The included fonts are Deja Vu Sans and Deja Vu Sans Mono.

*****/
#include "font_default.ttf.h"
#include "font_default.mono.ttf.h"

namespace dust
{
    const uint8_t * __getDefaultFontData(bool monospace)
    {
        return monospace ? default_mono_ttf : default_ttf;
    }
};
