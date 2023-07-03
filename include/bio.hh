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
    /// @todo move to klib
    template<int SIZE=512>
    struct alignas(SIZE) AlignedBytes{uint8_t bytes[SIZE];};
    struct BlockKey{
        dev_t dev;
        uint32_t secno;
        bool operator==(const BlockKey other) const {return dev==other.dev&&secno==other.secno;}
    };
    struct BlockBuf{
        const BlockKey key;
        bool dirty;
        uint8_t *d;
        BlockBuf(const BlockKey& key_):key(key_),d(reinterpret_cast<uint8_t*>(new AlignedBytes)){
            /// @todo fill buffer from dev:secno
        }
        ~BlockBuf(){
            /// @todo writeback
            delete reinterpret_cast<AlignedBytes<>*>(d);
        }
        template<typename T=uint32_t>
        inline const T& operator[](off_t off) const {return reinterpret_cast<T*>(d)[off];}
        template<typename T=uint32_t>
        inline T& operator[](off_t off){ dirty=true; return reinterpret_cast<T*>(d)[off]; }
        template<typename T=uint32_t>
        inline const T& at(off_t off) const {return *reinterpret_cast<T*>(d+off);}
    };
    typedef eastl::weak_ptr<BlockBuf> BufWeakRef;
    typedef eastl::shared_ptr<BlockBuf> BufRef;
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
            return lru.getOrSet(key,[key](){return new BlockBuf(key);});
        }
    };
}
#endif