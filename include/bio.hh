#ifndef BIO_HH__
#define BIO_HH__
#include "common.h"
#include "klib.hh"
#include <EASTL/bonus/lru_cache.h>

namespace eastl{
    namespace v3{
        template<typename K,typename V>
        class shared_lru{
            using list_type=list< pair<K,shared_ptr<V>> >;
            using list_iterator=typename list_type::iterator;
            using map_type=unordered_map<K,pair<weak_ptr<V>,list_iterator>>;
            map_type active;
            list_type lru;
            size_t capacity;
            void reserve(){
                if(lru.size()>capacity){
                    active.find(lru.back().first)->second.second=lru.end();
                    lru.pop_back();
                }
            }
        public:
            shared_lru(size_t size):capacity(size){}
            template<typename Lambda>
            shared_ptr<V> getOrSet(const K &key,Lambda refill){
                if(active.find(key)!=active.end()){
                    Log(trace,"bcache hit(%d)",key);
                    auto mit=active.find(key);
                    auto rt=mit->second.first.lock();
                    lru.erase(mit->second.second);
                    lru.push_front(pair{key,rt});
                    mit->second.second=lru.begin();
                    return rt;
                }
                Log(trace,"bcache miss(%d)",key);
                reserve();
                auto rt=shared_ptr<V>(refill(),[this,key](V* val)->void{
                    active.erase(key);
                    delete val;
                });
                lru.push_front(pair{key,rt});
                active[key]=pair{weak_ptr<V>(rt),lru.begin()};
                return rt;
            }
            inline bool contains(const K &key){
                return active.find(key)!=active.end();
            }
        };
    }
    using namespace v3;
}


namespace bio{
    /// @todo move to klib
    template<int SIZE=512>
    struct alignas(SIZE) AlignedBytes{uint8_t bytes[SIZE];};
    struct BlockKey{
        uint64 dev;
        uint32_t secno;
        bool operator==(const BlockKey other) const {return dev==other.dev&&secno==other.secno;}
    };
    struct BlockBuf{
        /// @todo add lock for concurrency
        constexpr static int blockSize=512;
        const BlockKey key;
        bool dirty;
        uint8_t *d;
        BlockBuf(const BlockKey& key_);
        BlockBuf(const BlockBuf &other)=delete;
        ~BlockBuf();
        inline const uint32_t& operator[](off_t off) const {return at<uint32_t>(off);}
        inline uint32_t& operator[](off_t off){return at<uint32_t>(off);}
        template<typename T>
        inline const T& at(off_t off) const {return *reinterpret_cast<T*>(d+off);}
        template<typename T>
        inline T& at(off_t off){ dirty=true; return *reinterpret_cast<T*>(d+off); }
        template<typename T=uint32_t>
        inline const T& as(off_t off) const {return reinterpret_cast<T*>(d)[off];}
        template<typename T=uint32_t>
        inline T& as(off_t off) {return reinterpret_cast<T*>(d)[off];}
        inline void clear(){dirty=true; memset(d,0,blockSize);}
        /// @todo writeback
        void flush();
        /// @todo load from dev:secno
        void reload();
    private:
        inline ByteArray asArray(){return ByteArray(d,blockSize);}
    };
    typedef weak_ptr<BlockBuf> BufWeakRef;
    typedef shared_ptr<BlockBuf> BufRef;
}

namespace eastl{
    template <> struct hash<bio::BlockKey>
    { size_t operator()(bio::BlockKey val) const { return val.dev<<32|val.secno; } };
}

namespace bio{
    class BCacheMgr{
    eastl::shared_lru<BlockKey,BlockBuf> lru;
    public:
        constexpr static size_t defaultSize=0x80;
        BCacheMgr():lru(defaultSize){}
        BufRef operator[](const BlockKey &key){
            Log(debug,"bcache[{%d:%x}]",key.dev,key.secno);
            return lru.getOrSet(key,[key]()->BlockBuf*{return new BlockBuf(key);});
        }
    };
    void init();
}
extern bio::BCacheMgr bcache;
#endif