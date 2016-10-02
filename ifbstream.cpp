#include "ifbstream.h"
#include <iostream>
#include <fstream>
#include <unistd.h>

#ifdef DEBUG_DUMP_CODES
#include <bitset>
#endif

unsigned int ifbstream::next() {
    if (pos >= 8) {
        pos = 0;
        if(!in.get(buf)) {
            throw std::ios_base::failure("Error reading compressed block");
        }
    }
    unsigned int bit = (buf >> pos) & 0x01;
    pos++;
    return bit;
}

int ifbstream::read(int count) {
    int bits = 0;
    int bit;

    for(int i = 0; i < count; i++) {
        bit = next();
        bits |= (bit << i);
    }
#ifdef DEBUG_DUMP_CODES
    std::bitset<16> bs(bits);
    std::cout << std::left << std::setw(16)
        << bs.to_string().substr(16-count, 16) << ":" << bits << std::endl;
#endif
    return bits;
}

