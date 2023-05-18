#include "resmgr.hh"
#include <bits/stdc++.h>

class A{
    long k[20];
};
int main(){
    auto a=make_shared<A>();
    std::cout<<a.refcount()<<std::endl;
    {
        auto b=a;
        std::cout<<a.refcount()<<std::endl;
    }
    std::cout<<a.refcount()<<std::endl;
}