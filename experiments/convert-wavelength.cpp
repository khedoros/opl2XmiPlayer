#include<iostream>
#include<array>
#include<cstdint>
#include<cassert>

const std::array<uint8_t,16> multVal {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

#define NATIVE_OPL_SAMPLE_RATE          49716.0
#define OPL_SAMPLE_RATE 49716.0
#define NATIVE_SAMPLE_RATE  49716

int convertWavelength(int wavelength) {
    return (static_cast<int64_t>(wavelength) * static_cast<int64_t>(NATIVE_SAMPLE_RATE)) / static_cast<int64_t>(OPL_SAMPLE_RATE);
}

int alg1(int fNum, int multIndex, int octave) {
    unsigned a = convertWavelength(((fNum * multVal[multIndex]) << octave));
             return a;
}


int alg2(int fNum, int multIndex, int octave) {
    unsigned a = convertWavelength(((fNum << octave) * multVal[multIndex]) >> 1);
             return a;
}

int main() {
    for(int i=0;i<multVal.size();i++) {
        for(unsigned fNum=1; fNum < (1<<10); fNum++) {
            for(int octave = 0; octave < 8; octave++) {
                std::cout<<fNum<<", "<<int(multVal[i])<<", "<<octave<<": "<<alg1(fNum,i,octave)<<'\t'<<alg2(fNum,i,octave)<<'\n';
                assert(alg1(fNum,i,octave) == 2 * alg2(fNum,i,octave));
            }
        }
    }
}
