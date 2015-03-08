#ifndef _LIGHTCACHED_H
#define _LIGHTCACHED_H

#include <functional>

namespace lightcache {
template <typename K, typename V>
	class cache {
		public:
			cache(const char * pathname,
				   uint32_t tableSize, 
				   uint32_t totalNode, 
				   std::function<size_t(K)> hash = std::hash<K>());
			~cache();
			cache(const cache &) = delete;
			cache &operator(const cache &) = delete;
			void set(const K &, const V &);
			const V &get(const K &, const V &);//get or else
			bool del(const K &);
			bool exist(const K &);
			uint32_t count();
		private:
			struct LruNode {
				uint32_t pre; //lruAddr is tail
				uint32_t next;//lruAddr is second 
				uint32_t nodeId;
			};
			struct TableHead {
				uint32_t tableLen;
				uint32_t totalNode;
				uint32_t freeNode;
				uint32_t usedCount;
				uint32_t recycledNode;
				char fileName[48];
			};
			struct BucketList {
				uint32_t head;
				uint32_t tail;
			};
			struct Node {
				V value;
				K key;
				struct LruNode *lru;
				uint32_t hashCode;
				uint32_t next;
				uint32_t pre;
			};
			struct TableHead *tableAddr;
			struct BucketList *bucketAddr;
			struct LruNode *lruAddr;
			struct Node *entry;
			std::mutex mtx;
			size_t memSize;
			uintptr_t memAddr;
			std::function<size_t(K)> hashFunc;

			void *attachFile(const char *, size_t);
			void releaseFile(void *addr, size_t length);
			uint32_t getIdByKey(const K &);
			uint32_t getFreeNode();
			void addNewNodeToBucket(uint32_t, uint32_t);
			void addNewNodeToLru(uint32_t);
			void recycleNodeFromBucket(uint32_t, uint32_t);
			void recycleHeadFromLru();
			uint32_t getFirstNodeFromLru();

			static const uint32_t invalidId = 0xffffffff;

	}
}

#endif
