#include "adlibBnk.h"
#include <cassert>

int main() {
    std::cout<<"Header size\n"; assert(sizeof(adlibBnk_t::header_t) == 28);
    std::cout<<"oplregs size\n"; assert(sizeof(adlibBnk_t::oplRegs_t) == 13);
    std::cout<<"inst_t size\n"; assert(sizeof(adlibBnk_t::inst_t) == 2*sizeof(adlibBnk_t::oplRegs_t) + 4);
    std::cout<<"instName_t size\n"; assert(sizeof(adlibBnk_t::instName_t) == 12);
    std::string fileName{"ADLIB.BNK"};

    adlibBnk_t bank;
    try {
        adlibBnk_t oplInst(fileName);
        bank = oplInst;
    }
    catch(const std::string& e) {
        std::cout<<e<<std::endl;
    }

    for(auto ind: bank.index) {
        std::cout<<ind.name<<" at offset "<<ind.dataIndex<<'\n';
    }
    std::cout<<"Version: "<<bank.header.version<<"\tUsed instrument entries: "<<bank.header.numUsed<<"\tTotal instrument entries: "<<bank.header.numInstruments<<"\tOffset to name index: "<<bank.header.offsetInstNames<<"\tOffset to inst data: "<<bank.header.offsetInstData<<'\n';
    std::cout<<sizeof(adlibBnk_t::header)<<"\t"<<sizeof(adlibBnk_t::instName_t)<<" ("<<bank.header.numInstruments<<" of them)\t"<<sizeof(adlibBnk_t::inst_t)<<" ("<<bank.header.numInstruments<<" of them)\n";

}
