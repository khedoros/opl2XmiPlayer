#include<array>
#include<cstdint>
#include<cmath>
#include<iostream>

std::array<int,4*1024> logsinTable;
std::array<int,256> expTable;

void initTables() {
    for (int i = 0; i < 256; ++i) {
        logsinTable[i] = round(-log2(sin((double(i) + 0.5) * M_PI_2 / 256.0)) * 256.0);
        logsinTable[511 - i] = logsinTable[i];
        logsinTable[512 + i] = 0x8000 | logsinTable[i];
        logsinTable[1023 - i] = logsinTable[512+i];
        // expTable[i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
        expTable[255-i] = int(round(exp2(double(i) / 256.0) * 1024.0)) << 1;
    }
    for(int i = 0; i < 1024; ++i) {
        bool sign = i & 512;
        bool mirror = i & 256;
        for(int wf=0;wf<4;wf++) {
            switch(wf) {
                case 0: break; // full sine wave; already covered.
                case 1: // half sine wave (positive half, set negative half to 0)
                    if(!sign) logsinTable[wf*1024+i] = logsinTable[i];
                    else      logsinTable[wf*1024+i] = 0x8fff; // constant for -0 value
                    break;
                case 2: // rectified sine wave (double-bumps)
                    logsinTable[wf*1024+i] = logsinTable[i & 511];
                    break;
                case 3: // pseudo-saw (only the 1st+3rd quarters of the wave is defined, and are both positive)
                    if(!mirror) logsinTable[wf*1024+i] = logsinTable[i&255];
                    else        logsinTable[wf*1024+i] = 0x8fff;
            }
        }

    }
}

int lookupSin(int val, int wf) {
    val &= 1023;
    return logsinTable[1024 * wf + val];
}

int lookupExp(int val) {
    bool sign = val & 0x8000;
    int t = expTable[(val & 255)];
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
}

int main() {
    initTables();
    for(int wf=0;wf<4;wf++) {
        std::cout<<"Waveform# "<<wf+1<<"\n-----------------\n";
        for(int s=0;s<1024;s++) {
        std::cout<<logsinTable[s+1024*wf]<<'\t'<<lookupExp(logsinTable[s+1024*wf])<<'\n';
        }
    }
}
