#include<iostream>
#include<cmath>

int logsinTable[256];
int expTable[256];

int lookupSin(int val, int wf) {
    bool sign   = val & 512;
    bool mirror = val & 256;
    val &= 255;
    int result = logsinTable[mirror ? val ^ 255 : val];
    switch(wf) {
        case 0: // full sine wave
            break;
        case 1: // half sine wave
            if(sign) {
                //result = 0xfff;
                //result |= 0x8000;
            }
            break;
        // TODO: re-read what the sine wave format is and how I'd represent it here
        case 2: // rectified sine wave (double-bumps)
            break;
        case 3: // rectified sine wave, rises only
            break;
    }
    return result;
}

int lookupExp(int val) {
    bool sign = val & 0x8000;
    int t = (expTable[(val & 255) ^ 255] << 1) | 0x800;
    int result = t >> ((val & 0x7F00) >> 8);
    if (sign) result = ~result;
    return result;
}

void initTables() {
    for (int i = 0; i < 256; ++i) {
        logsinTable[i] = round(-log2(sin((double(i) + 0.5) * M_PI_2 / 256.0)) * 256.0);
        expTable[i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
    }
}

int main() {
    initTables();
    for(int i = 0; i < 65536; i++) {
//        int sinVal = lookupSin(i,0);
//        int expVal = lookupExp(sinVal);
//        std::cout<<i<<": "<<sinVal<<"\t"<<expVal<<"\n";
          int expVal = lookupExp(i);
          std::cout<<i<<": "<<expVal<<"\n";
    }
}
