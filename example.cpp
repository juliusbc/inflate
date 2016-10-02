#include <iostream>
#include "inflate.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Please supply a file to gunzip" << std::endl;
        return 1;
    }
    for (int i=1; i<argc; i++)
        inflate::gunzip(argv[i]);
    return 0;
}
