#include "inflate.h"

#include <vector>
#include <algorithm>
#include <numeric>
#include <iterator>
#include <tuple>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "teestream.h"
#include "ringbuffer.h"
#include "ifbstream.h"


namespace inflate { 
    namespace flag {
        const unsigned char text = 0x01;
        const unsigned char hcrc = 0x02;
        const unsigned char extra = 0x04;
        const unsigned char fname = 0x08;
        const unsigned char comment = 0x10;
    }

    const std::vector<inflate::Range> fixedranges = {
        (inflate::Range){143, 8},
        (inflate::Range){255, 9},
        (inflate::Range){279, 7},
        (inflate::Range){287, 8}
    };

    const int max_buffer_size=32768;
}



std::vector<int> inflate::_UTIL::count_by_bitlength(
        const std::vector<inflate::Range>& ranges) {
/* Calculates the length of each canonical huffman code range */
    std::vector<int> lengths;
    int max_bit_length;
    std::vector<inflate::Range> diff;

    // max = max(ranges, lambda x, y: x.bit_length < y.bit_length)
    max_bit_length = std::max_element(ranges.begin(), ranges.end(),
            [](const inflate::Range& x, const inflate::Range& y) {
                return x.bit_length < y.bit_length; }
        )->bit_length;

    // count the lengths of each range
    // take the difference between previous and current range ends
    // diff = map(zip(ranges, [0] + ranges[:-1]), lambda x, y: x.end - y.end)
    std::adjacent_difference(ranges.begin(), ranges.end(),
            back_inserter(diff), 
            [](const inflate::Range& p, const inflate::Range& c) {
                return (inflate::Range){p.end - c.end, p.bit_length};}
        );
    diff[0].end += 1; // 0 is a symbol
    // collect range lengths into bins by code bit_length
    std::fill_n(back_inserter(lengths), max_bit_length + 1, 0);
    for(auto& drange : diff) {
        lengths[drange.bit_length] += drange.end;
    }
    return lengths;
}


inflate::Node* inflate::build_tree(
        const std::vector<inflate::Range>& ranges) {
/* Build a huffman tree from the canonical huffman code ranges */
    if (ranges.empty()) return nullptr;

    inflate::Node* root;
    std::vector<int> numrows;
    std::vector<inflate::Code>nextcodes;
    std::vector<inflate::_UTIL::Coderow> codebook;

    numrows = inflate::_UTIL::count_by_bitlength(ranges);

    // determine starting codes for each bit length
    nextcodes.push_back(0); // start at 0
    // acc = 0
    // for num in numrows[1:]:
    //      acc = (acc + num) << 1
    //      nextcodes.append(acc if num else 0)
    int acc = 0;
    for(auto i=numrows.begin()+1, e=numrows.end(); i<e; i++) {
        nextcodes.push_back(*i ? acc : 0);
        acc = (acc + *i) << 1;
    }

    // populate the codebook
    // codebook = map(zip(diff, ranges), 
    //                lambda d, r: zip(d*[r.code], range(r.end-d, r)
    //                                             if r.code != 0 else 0))
    codebook.reserve(ranges.back().end);
    auto appendtobook = back_inserter(codebook);
    for(const auto& range : ranges) {
        unsigned int len = range.bit_length;
        std::generate_n(appendtobook, range.end + 1 - codebook.size(),
                [&nextcodes, &len]() { 
                return inflate::_UTIL::Coderow(
                        {len, (len ? nextcodes[len]++ : 0)} ); 
                }
            );
    }

#ifdef DEBUG_DUMP_CODES
    std::cout << "Ranges: ";
    for(auto& range : ranges) {
        std::cout << range.end << ':' << range.bit_length << ", ";
    }
    std::cout << std::endl;
    for(int i=0; i < codebook.size(); i++) {
        std::cout << i << ": ";
        for(int pos=codebook[i].bit_length-1; pos>=0; pos--)
            std::cout << ((codebook[i].code >> pos ) & 1);
        std::cout << std::endl;
    }
#endif
    
    // build tree from codebook
    root = new inflate::Node({-1, nullptr, nullptr});
    inflate::Symbol symbol = 0;
    for (auto& row : codebook) {
        if (row.bit_length < 1) {  // skip empty lines
            symbol++;
            continue;
        }
        inflate::Node* curr = root;
        // read code bit by bit
        for( int i = row.bit_length - 1; i >= 0; i--){
            if ((row.code >> i) & 0x01) {  // 1
                if (curr->one == nullptr) {
                    curr->one = new inflate::Node({-1, nullptr, nullptr});
                }
                curr = curr->one;
            }
            else {  // 0
                if (curr->zero == nullptr) {
                    curr->zero = new inflate::Node({-1, nullptr, nullptr});
                }
                curr = curr->zero;
            }
        }
        curr->symbol = symbol++;
    }
    return root;
}


inflate::Symbol inflate::read_out(
        inflate::Node* huffman_tree, ifbstream& in) {
/*Decode a single symbol from stream in, using the huffman_tree*/
    inflate::Node* curr = huffman_tree;
    inflate::Symbol symbol = curr->symbol;
#ifdef DEBUG_DUMP_CODES
    std::cout << std::endl;
    std::ostringstream buf;
#endif
    
    while(symbol < 0) {
        if (in.next()) {  // next bit is 1
#ifdef DEBUG_DUMP_CODES
            buf << 1;
#endif
            curr = curr->one;
        }
        else {           // next bit is 0
#ifdef DEBUG_DUMP_CODES
            buf << 0;
#endif
            curr = curr->zero;
        }

        if (curr == nullptr){  // this leaf has code -1 probably
#ifdef DEBUG_DUMP_CODES
            std::cout << buf.str();
#endif
            throw std::invalid_argument("Malformed tree, unindexed code");
        }
        symbol = curr->symbol;
    }
#ifdef DEBUG_DUMP_CODES
    std::cout << std::setw(15) << std::left << buf.str();
    std::cout << " :" << std::setw(3) << symbol << " = ";
    std::cout << std::setw(0);
#endif

    return symbol;
}


template <class InputIterator>
std::vector<inflate::Range> inflate::_UTIL::group_into_ranges(
        InputIterator first, InputIterator last) {
/* Groups vectors of code lengths into canonical huffman ranges */
    std::vector<inflate::Range> ranges;

    // collapse into ranges (possibly unnecessary?)
    // may use Eric Niebler's range lib group_by in future STL
    InputIterator it = first;
    for(int i = 0; it < last; i++, it++) {
        if (*it == *(it+1) && it < last-1) {
            continue;  // only push when code bit length changes or at end
        }
        ranges.emplace_back(
                (inflate::Range){i, *it});
    }
    return ranges;
}


std::vector<inflate::Range> inflate::_UTIL::read_preheader(ifbstream& in){
    int hclen;
    hclen = in.read(4);
    std::vector<unsigned int> preheader_lengths(19, 0);  // input
    std::vector<inflate::Range> preheader_ranges;  // output
    static const int preheader_offsets[] = {  // according to spec
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

#ifdef DEBUG_INFGEN_OUTPUT_D
    std::cout << hclen + 4 << std::endl;
#endif

#ifdef DEBUG_DUMP_CODES
    std::cout << "--------------------------Preheader Codes--------------------------------------" << std::endl;
#endif

    // read in offsets
    for(int i=0; i<(hclen + 4); i++){
        preheader_lengths[ preheader_offsets[i] ] = in.read(3);
    }

    preheader_ranges = inflate::_UTIL::group_into_ranges(
            preheader_lengths.begin(), preheader_lengths.end());
#ifdef DEBUG_INFGEN_OUTPUT_D
    int i = 0;
    for(auto& range : preheader_ranges){
        if(!range.bit_length) {
            i = range.end + 1;
            continue;
        }
        for(i; i <= range.end; i++) {
            std::cout << "code ";
            std::cout << i << ' ' << range.bit_length << std::endl;
        }
    }
#endif
    return preheader_ranges;
}


std::pair<inflate::Node*, inflate::Node*>
inflate::read_deflate_header(ifbstream& in) {
    std::vector<inflate::Range> preheader_ranges;
    inflate::Node* preheader_tree;
    inflate::Node* literals_tree, *distance_tree;

    unsigned int bit_length;
    std::vector<unsigned int> lengths;
    auto into_lengths = back_inserter(lengths);

    int hlit, hdist;
    hlit = in.read(5);
    hdist = in.read(5);

#ifdef DEBUG_INFGEN_OUTPUT_D
    std::cout << "count " << hlit + 257 << ' ' << hdist + 1 << ' ';
    bool lens = false;
    #ifdef DEBUG_DUMP_CODES
    std::cout << std::endl;
    #endif
#endif

    
    lengths.reserve(hlit + hdist + 258);

    preheader_ranges = inflate::_UTIL::read_preheader(in);
    preheader_tree = build_tree(preheader_ranges);


    for(inflate::Symbol symbol=0; symbol < (hlit + hdist + 258); symbol++){
        try {
            bit_length = inflate::read_out(preheader_tree, in);
        }
        catch (std::invalid_argument& e) {
            throw std::invalid_argument(
                    "Preheader Tree Invalid: negative leaf symbol");
        }
        if (bit_length > 15) {  // repeat symbol
#ifdef DEBUG_INFGEN_OUTPUT_D
            if (lens) { lens = false; std::cout << std::endl; }
#endif
            int repeat;
            switch(bit_length) {
            case 16:
                repeat = in.read(2) + 3;
                std::fill_n(into_lengths, repeat, lengths.back());
#ifdef DEBUG_INFGEN_OUTPUT_D
                std::cout << "repeat " << repeat << std::endl;
#endif
                break;
            case 17:
                repeat = in.read(3) + 3;
                std::fill_n(into_lengths, repeat, 0);
#ifdef DEBUG_INFGEN_OUTPUT_D
                std::cout << "zeros " << repeat << std::endl;
#endif
                break;
            case 18:
                repeat = in.read(7) + 11;
                std::fill_n(into_lengths, repeat, 0);
#ifdef DEBUG_INFGEN_OUTPUT_D
                std::cout << "zeros " << repeat << std::endl;
#endif
                break;
            default:
                throw std::invalid_argument(
                        "Preheader Tree Invalid: symbol too large");
            }
            symbol += repeat - 1;
        }
        else { // read_out output guaranteed > 0
            into_lengths = bit_length;
#ifdef DEBUG_INFGEN_OUTPUT_D
            if (!lens) { lens = true; std::cout << "lens"; }
            std::cout << " " << bit_length;
#endif
        }
    }
#if defined(DEBUG_DUMP_CODES) || defined(DEBUG_INFGEN_OUTPUT_D)
    std::cout << std::endl;
#endif
    auto literals_ranges = inflate::_UTIL::group_into_ranges(
            lengths.begin(), lengths.begin() + (hlit + 257));
    
#ifdef DEBUG_DUMP_CODES
    std::cout << "----------------------------Literal Codes--------------------------------------" << std::endl;
#endif

#ifdef DEBUG_INFGEN_OUTPUT
    int i = 0;
    for(auto& range : literals_ranges){
        if(!range.bit_length) {
            i = range.end + 1;
            continue;
        }
        for(i; i <= range.end; i++) {
    #ifdef DEBUG_INFGEN_OUTPUT_D
            std::cout << "! ";
    #endif
            std::cout << "litlen ";
            std::cout << i << ' ' << range.bit_length << std::endl;
        }
    }
#endif

    literals_tree = inflate::build_tree(literals_ranges);

    auto distance_ranges = inflate::_UTIL::group_into_ranges(
            lengths.begin() + (hlit + 257), lengths.end());

#ifdef DEBUG_DUMP_CODES
    std::cout << "---------------------------Distance Codes--------------------------------------" << std::endl;
#endif

#ifdef DEBUG_INFGEN_OUTPUT
    i = 0;
    for(auto& range : distance_ranges){
        if(!range.bit_length) {
            i = range.end + 1;
            continue;
        }
        for(i; i <= range.end; i++) {
    #ifdef DEBUG_INFGEN_OUTPUT_D
            std::cout << "! ";
    #endif
            std::cout << "dist ";
            std::cout << i << ' ' << range.bit_length << std::endl;
        }
    }
#endif

    distance_tree = inflate::build_tree(distance_ranges);
    
    return std::make_pair(literals_tree, distance_tree);
}

#ifdef DEBUG_INFGEN_OUTPUT
void inflate::_UTIL::teeprint::operator()(teestream& out,
        ringbuffer& buf, char symbol) {
    if (symbol < 32) {  // unprintable
        if (!wasliteral) {  // new literal
            std::cout << "literal ";
            wasliteral = true;
            quoted = false;
            std::cout << (int)(unsigned char)symbol;
            buf << symbol;

        }
        else {  // more literals
            if (quoted) std::cout << std::endl << "literal";
            std::cout << ' ';
            quoted = false;
            std::cout << (int)(unsigned char)symbol;
            buf << symbol;

        }
    }
    else {  // printable
        if (!wasliteral) {  // new literal
            wasliteral = true;
            quoted = true;
            std::cout << "literal '";
            out << symbol;
        }
        else {  // more literals
            if (!quoted) {
                std::cout << " '";
                quoted = true;
            }
            out << symbol;
        }
    }
}
#endif

ringbuffer& inflate::inflate_block(ifbstream& in,
        std::ostream& output/*=std::cout*/, bool fixedtree/*=false*/) {
    ringbuffer buf(inflate::max_buffer_size);
    return inflate::inflate_block(in, buf, output, fixedtree);
}

ringbuffer& inflate::inflate_block(ifbstream& in, 
        ringbuffer& buf,
        std::ostream& output/*=std::cout*/, 
        bool fixedtree/*=false*/) {
    teestream teeout(output, buf);

    static const int extra_length_addend[] = {
        11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 
        51, 59, 67, 83, 99, 115, 131, 163, 195, 227
    };
    static const int extra_dist_addend[] = {
        4, 6, 8, 12, 16, 24, 32, 48, 64, 96, 128,
        192, 256, 384, 512, 768, 1024, 1536, 2048,
        3072, 4096, 6144, 8192, 12288, 16384, 24576
    };
    inflate::Node* literals_tree, *distance_tree;

    if (!fixedtree) {
        std::tie(literals_tree, distance_tree) = read_deflate_header(in);
    }
    else {
        literals_tree = build_tree(inflate::fixedranges);
        distance_tree = nullptr;
    }
#ifdef DEBUG_INFGEN_OUTPUT
    inflate::_UTIL::teeprint infgen_print;    
#endif

    while(true) {
        inflate::Symbol symbol = read_out(literals_tree, in);
        if(symbol < 256) {
#ifdef DEBUG_INFGEN_OUTPUT
            infgen_print(teeout, buf, symbol);
#else
            teeout << (char)symbol;
#endif
        }
        else if (symbol == 256) { // stop symbol is 256
            break;
        }
        else if (symbol < 286) {  // backpointer (3.2.5):
            int length, distance;
            if (symbol < 265) {             // length of match
                length = symbol - 254;      // 257-264 -> 3-10
            }
            else if (symbol < 285) {
                int extra_bits = in.read((symbol - 261) / 4);
                length = extra_bits         // 265-284 -> 11-257
                           + extra_length_addend[symbol - 265];
            }
            else {                          // symbol == 285
                length = 258;
            }

            // read the distance code
            if (distance_tree == nullptr) {
                distance = in.read(5);
            }
            else {
                distance = read_out(distance_tree, in);
            }

            if (distance < 30) {
                if (distance > 3) {             // 4-29 -> 4-32767
                    int extra_dist = in.read((distance - 2) / 2 );
                    distance = extra_dist
                        + extra_dist_addend[distance - 4];
                }
                ++distance; // 0-32767 -> 1-32768

#ifndef DEBUG_INFGEN_OUTPUT
                std::cout << buf.readfrom(length, distance);
#else
    #ifdef DEBUG_DUMP_CODES
                 std::cout << "match " << length << ' ' << distance;
    #else
                if (infgen_print.wasliteral) std::cout << std::endl;
                infgen_print.wasliteral = false;
                std::cout << "match "
                    << length << ' ' << distance << std::endl;
    #endif
#endif
            }
            else {
                throw std::invalid_argument(
                    "Error decoding block: Invalid distance symbol");
            }
        }
        else {
            throw std::invalid_argument(
                    "Error decoding block: Invalid literal symbol");
        }
    }

    return buf;
}


void inflate::gunzip(std::string fn, std::ostream& output/*=std::cout*/) {
    inflate::gzip_file file;
    std::ifstream in;
#ifdef DEBUG_INFGEN_OUTPUT
    std::cout << "! infgen 2.2 output" << std::endl << '!' << std::endl;
#endif

    // may throw the following
    in.exceptions(std::ios::badbit|std::ios::failbit);

    in.open(fn, std::ios::in|std::ios::binary);
    in.read((char*)&file.header, sizeof(gzip_header));
    // 1f8b signifies a gzip file
    if (file.header.id[0] != 0x1f || file.header.id[1] != 0x8b) {
        throw std::invalid_argument("Not in gzip format");
    }
#ifdef DEBUG_INFGEN_OUTPUT
        std::cout << "gzip" << std::endl << '!' << std::endl;
#endif
    if (file.header.compression_method != 8) {
        throw std::invalid_argument("Compression Method not 8");
    }
    if (file.header.flags & flag::extra) {
        // TODO spec seems to indicate that this is little-endian;
        // htons for big-endian machines?
        in.read(reinterpret_cast<char*>(file.xlen), 2);
        in.read((char*)file.extra, file.xlen);
    }
    if (file.header.flags & flag::fname) {
        std::getline(in, file.fname, '\0');
    }
    if (file.header.flags & flag::comment) {
        std::getline(in, file.fcomment, '\0');
    }
    if (file.header.flags & flag::hcrc) {
        in.read(reinterpret_cast<char*>(file.crc16), 2);
    }

    // no longer throws exceptions
    in.exceptions(std::ios::goodbit);

    int last_block;
    unsigned char block_format;

    ifbstream bin(in);
    ringbuffer buf(inflate::max_buffer_size);
    do {
        last_block = bin.next();
#ifdef DEBUG_INFGEN_OUTPUT
        if(last_block) std::cout << "last" << std::endl;
#endif
        block_format = bin.read(2);
        switch(block_format) {
            case 0x00:
                // TODO
                std::cerr << "Uncompressed block type not supported"
                    << std::endl;
                throw std::logic_error(
                        "Uncompressed block type not implemented");
                break;
            case 0x01:
                inflate_block(bin, buf, output, true); // fixedtree = true
                break;
            case 0x02:
#ifdef DEBUG_INFGEN_OUTPUT
                std::cout << "dynamic" << std::endl;
#endif
                inflate_block(bin, buf, output);
                break;
            default:
                std::cerr << "Unsupported block type "
                    << int(block_format) << std::endl;
                throw std::invalid_argument("Invalid block type");
        }
#ifdef DEBUG_INFGEN_OUTPUT
        std::cout << std::endl << "end" << std::endl << '!' << std::endl;
#endif

#ifdef DEBUG_DUMP_CODES
        std::cout << "offset=" << bin.tellg() << std::endl;
#endif
    } while (!last_block);

    return;
}

