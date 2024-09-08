#include<cmath>
#include<iostream>
#include<array>

std::array<int,256> expTable;
std::array<int,256> expTable2;
std::array<int,256> expTable3;

int lookupExp(int val) {
    bool sign = val & 0x8000;
    int t = (expTable[(val & 255) ^ 255] | 1024) << 1;
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
}

void initTables() {
    for (int i = 0; i < 256; ++i) {
        expTable[i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
    }
}

int lookupExp2(int val) {
    bool sign = val & 0x8000;
    int t = (expTable2[(val & 255)] | 1024) << 1;
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
}

void initTables2() {
    for (int i = 0; i < 256; ++i) {
        expTable2[255-i] = round(exp2(double(i) / 256.0) * 1024.0) - 1024.0;
    }
}

int lookupExp3(int val) {
    bool sign = val & 0x8000;
    int t = expTable3[(val & 255)];
    int result = (t >> ((val & 0x7F00) >> 8)) >> 2;
    if (sign) result = ~result;
    return result;
}

void initTables3() {
    for (int i = 0; i < 256; ++i) {
        expTable3[255-i] = int(round(exp2(double(i) / 256.0) * 1024.0)) << 1;
    }
}


int main() {
    initTables();
    initTables2();
    initTables3();
    for(int i=0;i<65536;i++) {
        if(lookupExp(i) != lookupExp2(i)) {
            std::cout<<"Mismatch(1,2) at i == "<<i<<'\n';
        }
        if(lookupExp(i) != lookupExp3(i)) {
            std::cout<<"Mismatch(1,3) at i == "<<i<<'\n';
        }
    }
}
