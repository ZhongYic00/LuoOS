#include "resmgr.hh"
#include <bits/stdc++.h>

class A{
    long k[20];
};
int main(){
    SharedPtr<int> a=new int(10);
    std::cout<<a.refCount()<<std::endl;
    {
        auto b=a;
        std::cout<<a.refCount()<<std::endl
        <<((*a)==(*b))<<std::endl;
        ;
    }
    std::cout<<a.refcount()<<std::endl;
}