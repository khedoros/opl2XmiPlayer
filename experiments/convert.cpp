#include<iostream>
#include<fstream>

int main() {
    std::ifstream in("MT32-GM.OUT.txt");
    for(int i=0;i<128;i++) {
    int first,second;
    in>>first>>second;
    std::cout<<"{ "<<first<<", "<<second<<" },\n";
    }
}
