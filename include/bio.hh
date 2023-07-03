#ifndef BIO_HH__
#define BIO_HH__
#include "common.h"
#include <EASTL/shared_ptr.h>
#include <EASTL/weak_ptr.h>
#include <EASTL/bonus/lru_cache.h>

namespace eastl{
    namespace v2{
        template<typename K,typename V>
        class shared_lru{
            unordered_map<K,weak_ptr<V>> active;
            lru_cache<K,shared_ptr<V>> inactive;
        public:
            shared_lru(size_t size):inactive(size){}
            template<typename Lambda>
            shared_ptr<V> getOrSet(const K &key,Lambda refill){
                if(active.find(key)!=active.end()) return active[key].lock();
                V* valptr=nullptr;
                if(inactive.contains(key)){
                    auto ptr=inactive[key];
                    valptr=ptr.get();
                    inactive.erase(key);
                } else valptr=refill();
                auto rt=shared_ptr<V>(valptr,[this,key](V* ptr)->void{
                    active.erase(key);
                    inactive[key]=shared_ptr<V>(ptr);
                });
                active.insert(pair{key,weak_ptr<V>(rt)});
                return rt;
            }
            inline bool contains(const K &key){
                return active.find(key)!=active.end()||inactive.contains(key);
            }
        };
    }
#ifdef EASTL_LRU_MODIFIED
    namespace v1{
        template<typename K,typename V>
        class shared_lru:public lru_cache<K,shared_ptr<V>>{
            void erase_oldest() override{
                ///@bug temporal complexity incorrect
                auto iter = --(this->m_list.end());
                for(;this->m_map[*iter].first.use_count()>1;iter--);
                auto key=*iter;
                this->m_list.erase(iter);

                // Delete the actual entry
                this->map_erase(this->m_map.find(key));
            }
        public:
          shared_lru(size_t size):lru_cache<K,shared_ptr<V>>(size){}
        };
    }
#endif
    using namespace v2;
}


namespace bio{
    using ::eastl::shared_ptr;
    struct BlockKey{
        dev_t dev;
        uint32_t secno;
        bool operator==(const BlockKey other) const {return dev==other.dev&&secno==other.secno;}
    };
    struct alignas(512) BlockBuf{
        uint8_t d[512];
    };
    typedef eastl::weak_ptr<BlockBuf> BufWeakRef;
    struct BufRef{
        BlockKey key;
        eastl::shared_ptr<BlockBuf> buf;
        BufRef(BlockBuf *buf_):buf(buf_){}
        BufRef(const shared_ptr<BlockBuf> &ref,const BlockKey &key_):key(key_),buf(ref){}
        template<typename T=uint32_t>
        inline T& operator[](off_t off){return reinterpret_cast<T*>(buf->d)[off];}
        template<typename T=uint32_t>
        inline T& at(off_t off){return *reinterpret_cast<T*>(buf->d+off);}
    };
}

namespace eastl{
    template <> struct hash<bio::BlockKey>
    { size_t operator()(bio::BlockKey val) const { return val.dev<<32|val.secno; } };
}

namespace bio{
    class BCacheMgr{
    eastl::shared_lru<BlockKey,BlockBuf> lru;
    public:
        constexpr static size_t defaultSize=0x100;
        BCacheMgr():lru(defaultSize){}
        BufRef operator[](const BlockKey &key){
            return BufRef{lru.getOrSet(key,[key](){return new BlockBuf();}),key};
        }
    };
}
#endif