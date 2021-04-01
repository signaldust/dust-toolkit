
#pragma once

#include <cstdint>

// Copyright (c) 2008-2009 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.
//
// NOTE:
// This is a modified version with some renaming and namespacing.
// We store the data table in a separate source to link it just once.
namespace dust
{
    namespace utf8
    {
        extern const uint8_t data[];

        // use static constants instead of defines
        static const uint32_t ACCEPT = 0;
        static const uint32_t REJECT = 1;

        // decoder function
        static inline uint32_t decode
        (uint32_t* state, uint32_t* codep, uint8_t byte) {
            uint32_t type = data[byte];

            *codep = (*state != ACCEPT) ?
            (byte & 0x3fu) | (*codep << 6) :
            (0xff >> type) & (byte);

            *state = data[256 + *state*16 + type];
            return *state;
        }

        // this is just the unicode invalid character
        static const uint32_t invalid = 0xfffd;

        // convenience wrapper for more convenient C++ interface
        struct Decoder
        {
            unsigned state;
            unsigned ch;

            Decoder() : state(0), ch(0) {}

            // true when ch contains a character (valid or invalid)
            // internally handle REJECT by mapping to invalid
            bool next(uint8_t byte)
            {
                uint32_t status = decode(&state, &ch, byte);
                // check rejects and reset if necessary
                // output "invalid unicode" for client
                if(status == REJECT)
                {
                    state = 0; ch = invalid; return true;
                }
                return status == ACCEPT;
            }


        };

    };
};
