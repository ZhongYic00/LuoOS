#include "resmgr.hh"
#include <bits/stdc++.h>

class A{
    long k[20];
};
int main(){
    auto a=make_shared<int>(10);
    std::cout<<a.refcount()<<std::endl;
    {
        auto b=a;
        std::cout<<a.refcount()<<std::endl
        <<((*a)==(*b))<<std::endl;
        ;
    }
    std::cout<<a.refcount()<<std::endl;
}