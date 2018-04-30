#include <iostream>

#include "GBAController.h"

int main(int argc, char** argv) {
    GBAController gba(argv[1], argv[2]);
    gba.Run();
}
