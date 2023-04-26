#include "klib.hh"
{
    using namespace klib;
void put(T d){
    buff[tail]=d;
    tail=next(tail);
}
void pop(){
    head=next(head);
}
T get(){
    return buff[head];
}
bool empty(){
    return head==tail;
}
bool full(){
return next(tail)==head;
}
}