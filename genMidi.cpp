#include "genMidi.h"

int main() {
    std::string fileName{"genmidi.lmp"};
    genMidi_t doomInst(fileName);
    for(auto name: doomInst.names) {
        std::cout<<name<<'\n';
    }
}
