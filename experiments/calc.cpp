#include<iostream>
#include<cmath>

double dBtoX(double dB) {
    return log(-dB) / log(2);
}   
    
double percentageToDB(double percentage) {
    return log10(percentage) * 10.0;
}    
    
double percentageToX(double percentage) {
    return dBtoX(percentageToDB(percentage));
}

int main() {
    for(double per = 0.0; per < 100.0; per+=10.0) {
        std::cout<<per<<"%: "<<percentageToX(per / 100.0 )<<'\n';
    }
}
