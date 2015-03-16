#ifndef _LIGHTCACHED_H
#define _LIGHTCACHED_H

#include <functional>
#include <mutex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unistd.h>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <cstring>
#include <sys/mman.h>
#include <sys/stat.h>
#include <iostream>

#include "err.h"

namespace lightcache {
template <typename K, typename V>
	class cache {
		public:
			//Don't use this constructor now because the problem of mmap;
			cache(char const* pathname,
				   uint32_t tableSize, 
				   uint32_t totalNode, 
				   std::function<size_t(K)> hash);

			cache(uint32_t tableSize, uint32_t totalNode, std::function<size_t(K)> hash);			
			~cache();
			cache(const cache &) = delete;
			cache &operator=(const cache &) = delete;
			void set(const K &, const V &);
			const V &getOrElse(const K &, const V &);//get or else
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
			void *memAddr;
			std::function<size_t(K)> hashFunc;

			void *attachFile(const char *, size_t);
			//Fix me! 
			void releaseFile(void *addr, size_t length);
			uint32_t getIdByKey(const K &);
			uint32_t getFreeNode();
			void addNewNodeToBucket(uint32_t, uint32_t);
			void addNewNodeToLru(uint32_t);
			void recycleNodeFromBucket(uint32_t, uint32_t);
			void recycleHeadFromLru();
			void recycleNodeFromLru(uint32_t);
			uint32_t getFirstNodeFromLru();
			void MoveToTailLru(uint32_t);
			void *mallocLru();
			void freeLru();

			const uint32_t invalidId = std::numeric_limits<unsigned int>::max();

	};

	template <typename K, typename V>
	cache<K, V>::cache(uint32_t tableSize, uint32_t totalNode, std::function<size_t(K)> hash) {
		memSize = sizeof(struct TableHead) + 
				  sizeof(struct LruNode) + 
				  tableSize * sizeof(struct BucketList) +
				  totalNode * sizeof(struct Node);
		std::cout << "memSize: " << memSize << std::endl;
		memAddr = malloc(memSize);		
		//memAddr = attachFile(pathname, memSize);
		if (memAddr == NULL) 
			errexit("cache() attachFile");

		tableAddr = reinterpret_cast<struct TableHead *>(memAddr);
		bucketAddr = reinterpret_cast<struct BucketList *>
				(tableAddr + sizeof(struct TableHead));
		lruAddr = reinterpret_cast<struct LruNode *>
				(bucketAddr + tableSize * sizeof(struct BucketList));
		entry = reinterpret_cast<struct Node *>
				(lruAddr + sizeof(struct LruNode));

		tableAddr->tableLen = tableSize;
		tableAddr->totalNode = totalNode;
		tableAddr->fileName[0] = '\0';
		
		for (ptrdiff_t i = 0; i < tableSize; i++) {
			std::cout << "bucket " << i << bucketAddr + i << std::endl;
			(bucketAddr + i)->head = invalidId;
			(bucketAddr + i)->tail = invalidId;
		}
		tableAddr->freeNode = 0;
		tableAddr->recycledNode = invalidId;
		lruAddr->pre = lruAddr->next = lruAddr->nodeId = invalidId;
			
		hashFunc = hash; 
	}

	template <typename K, typename V>
	cache<K, V>::cache(char const * pathname,
				uint32_t tableSize,
				uint32_t totalNode,
				std::function<size_t(K)> hash) {
		memSize = sizeof(struct TableHead) + 
				  sizeof(struct LruNode) + 
				  tableSize * sizeof(struct BucketList) +
				  totalNode * sizeof(struct Node);
		memAddr = attachFile(pathname, memSize);
		if (memAddr == NULL) 
			errexit("cache() attachFile");

		tableAddr = reinterpret_cast<struct TableHead *>(memAddr);
		bucketAddr = reinterpret_cast<struct BucketList *>
				(tableAddr + sizeof(struct TableHead));
		lruAddr = reinterpret_cast<struct LruNode *>
				(bucketAddr + tableSize * sizeof(struct BucketList));
		entry = reinterpret_cast<struct Node *>
				(lruAddr + sizeof(struct LruNode));

		tableAddr->tableLen = tableSize;
		tableAddr->totalNode = totalNode;
		strncpy(tableAddr->fileName, pathname,	sizeof(tableAddr->fileName));

		std::cout << "usedCount: " << tableAddr->usedCount << std::endl;
		if (tableAddr->usedCount == 0) {
			//this is a new cache
			for (ptrdiff_t i = 0; i < tableSize; i++) {
				std::cout << "bucket " << i << bucketAddr + i << std::endl;
				(bucketAddr + i)->head = invalidId;
				(bucketAddr + i)->tail = invalidId;
			}
			tableAddr->freeNode = 0;
			tableAddr->recycledNode = invalidId;

			lruAddr->pre = lruAddr->next = lruAddr->nodeId = invalidId;
		}	
		hashFunc = hash; 
	}
	
	template <typename K, typename V>
	cache<K, V>::~cache() {
		//releaseFile(memAddr, memSize);
	}

	template <typename K, typename V>
	void cache<K, V>::set(const K &key, const V &value) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId != invalidId) { //modify old node
			memcpy(&((entry + nodeId)->value), &value, sizeof(value));
			MoveToTailLru(nodeId);
		} else { //add new node
			nodeId = getFreeNode();
			size_t hashCode = hashFunc(key);
			struct Node *newNode = entry + nodeId;
			newNode->key = key;
			newNode->hashCode = hashCode;
			memcpy(&(newNode->value), &value, sizeof(value));
			uint32_t index;
			index = hashCode % tableAddr->tableLen;
			addNewNodeToBucket(nodeId, index);
			addNewNodeToLru(nodeId);
			std::cout << "add done" << std::endl;
		}
	}

	template <typename K, typename V>
	const V &cache<K, V>::getOrElse(const K &key, const V &Else) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId)
			return Else;
		MoveToTailLru(nodeId);
		return (entry + nodeId)->value;
	}

	template <typename K, typename V>
	bool cache<K, V>::del(const K &key) {
		std::unique_lock<std::mutex> lock(mtx);

		std::cout << "del start" << std::endl;
		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId) 
			return false;

		uint32_t index = hashFunc(key) % tableAddr->tableLen;
		recycleNodeFromBucket(nodeId, index);
		recycleNodeFromLru(nodeId);
		return true;
	}

	template <typename K, typename V>
	bool cache<K, V>::exist(const K &key) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId)
			return false;
		MoveToTailLru(nodeId);
		return true;
	}

	template <typename K, typename V>
	uint32_t cache<K, V>::count() {
		std::unique_lock<std::mutex> lock(mtx);
		return tableAddr->usedCount;
	}

	template <typename K, typename V>
	void *cache<K, V>::attachFile(const char *pathname, size_t length) {
		int fd;
		struct stat statbuff;
		if (stat(pathname, &statbuff) == -1) {
			fd = open(pathname, O_RDWR|O_CREAT|O_EXCL, 0644);
			if (fd == -1)
				errexit("open O_CREAT");
			if (lseek(fd, length-1, SEEK_SET) == -1) {
				close(fd);
				errexit("lseek");
			}
			if (write(fd, " ", 1) == -1) {
				close(fd);
				errexit("write fd " "");
			}
			fd = open(pathname, O_RDWR);
			if (fd == -1)
				errexit("open O_RDWR");
		}
		void *addr = mmap(NULL, length, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if (addr == MAP_FAILED)
			errexit("mmap");
		//	return NULL;
		return addr;
	}

	template <typename K, typename V> 
	void cache<K, V>::releaseFile(void *addr, size_t length) {
		if (addr != NULL && length != 0) {
			if (msync(addr, length, MS_SYNC) == -1) 
				errexit("msync");
			if (munmap(addr, length) == -1) 
				errexit("munmap");
		}
	}

	template <typename K, typename V>
	uint32_t cache<K, V>::getIdByKey(const K &key) {
		size_t hashCode = hashFunc(key);
		uint32_t index = hashCode % tableAddr->tableLen;
		struct BucketList *tmpList = bucketAddr + index;
		std::cout << "tmpList: " << tmpList << std::endl;
		uint32_t nodeId = tmpList->head;
		std::cout << "nodeId: " << nodeId << std::endl;
		while (nodeId != invalidId) {
			std::cout << "nodeId: " << nodeId << std::endl;
			struct Node *tmpNode = entry + nodeId;
			if (tmpNode->hashCode == hashCode && tmpNode->key == key) 
				break;
			nodeId = tmpNode->next;
		}
		return nodeId;
	}

	template <typename K, typename V>
	uint32_t cache<K, V>::getFreeNode() {
		uint32_t nodeId = invalidId;
		//get from recycle list
		if (tableAddr->recycledNode != invalidId) { 
			nodeId = tableAddr->recycledNode;
			tableAddr->recycledNode = (entry + nodeId)->next;
			(tableAddr->usedCount)++;
		} 
		//new node
		else if (tableAddr->freeNode < tableAddr->totalNode) {
			nodeId = tableAddr->freeNode;
			(tableAddr->freeNode)++;
			(tableAddr->usedCount)++;
		}
		//get from lru
		else if (lruAddr->nodeId != invalidId) {
			nodeId = getFirstNodeFromLru();
			recycleHeadFromLru();
			uint32_t index = hashFunc((entry + nodeId)->key) % tableAddr->tableLen;
			recycleNodeFromBucket(nodeId, index);
		}
		//error
		else 
			return invalidId;
		struct Node *tmpNode = entry + nodeId;
		memset(tmpNode, 0, sizeof(struct Node));
		tmpNode->next = invalidId;
		tmpNode->pre = invalidId;
		return nodeId;
	}

	template <typename K, typename V>
	void cache<K, V>::addNewNodeToBucket(uint32_t nodeId, uint32_t index) {
		std::cout << "index: " << index << std::endl;
		std::cout << "nodeId: " << nodeId << std::endl;
		if (index >= tableAddr->tableLen || nodeId >= tableAddr->totalNode) 
			return;
		struct BucketList *tmpBucket = bucketAddr + index;
		struct Node *tmpNode = entry + nodeId;
		if (tmpBucket->head == invalidId) {
			tmpBucket->head = nodeId;
			tmpBucket->tail = nodeId;
		}
		else {
			tmpNode->next = tmpBucket->head;
			(entry + tmpBucket->head)->pre = nodeId;
			tmpBucket->head = nodeId;
		}
	}

	template <typename K, typename V>
	void cache<K, V>::addNewNodeToLru(uint32_t nodeId) {
		if (lruAddr->nodeId == invalidId) { //lrulist is empty
			lruAddr->nodeId = nodeId;
			lruAddr->next = invalidId;
			lruAddr->pre = nodeId;
			(entry + nodeId)->lru = lruAddr;
		}
		else { // insert at tail
			struct LruNode *tmpLru = 
				reinterpret_cast<struct LruNode *>(malloc(sizeof(struct LruNode)));
			tmpLru->pre = lruAddr->pre;
			lruAddr->pre = nodeId;
			if (lruAddr->next == invalidId) {
				lruAddr->next = nodeId;
				tmpLru->next = invalidId;
			}
			else {
				(entry + lruAddr->next)->lru->next = nodeId;
			}
			(entry + nodeId)->lru = tmpLru;		
		}
	}

	template <typename K, typename V>
	void cache<K, V>::recycleNodeFromBucket(uint32_t nodeId, uint32_t index) {

		if (index >= tableAddr->tableLen || nodeId >= tableAddr->totalNode) 
			return;
		struct BucketList *tmpBucket = bucketAddr + index;
		struct Node *tmpNode = entry + nodeId;

		if (tmpNode->next != invalidId) {
			(entry + tmpNode->next)->pre = tmpNode->pre;
		}
		else {
			tmpBucket->tail = tmpNode->pre;
		}

		if (tmpNode->pre != invalidId) {
			(entry + tmpNode->pre)->next = tmpNode->next;
		}
		else {
			tmpBucket->head = tmpNode->next;
		}

		tmpNode->next = tableAddr->recycledNode;
		tableAddr->recycledNode = nodeId;
		(tableAddr->usedCount)--;
	}

	template <typename K, typename V>
	void cache<K, V>::recycleHeadFromLru() {
		if (lruAddr->nodeId == invalidId) {
			return;
		}
		lruAddr->nodeId = lruAddr->next;
		if (lruAddr->next != invalidId) {
			lruAddr->next = (entry + lruAddr->nodeId)->lru->next;
			free((entry + lruAddr->nodeId)->lru);
			(entry + lruAddr->nodeId)->lru = lruAddr;
		}
		else {
			lruAddr->pre = invalidId;
		}
	}

	template <typename K, typename V>
	uint32_t cache<K, V>::getFirstNodeFromLru() {
		return lruAddr->nodeId;
	}

	template <typename K, typename V>
	void cache<K, V>::recycleNodeFromLru(uint32_t nodeId) {
		if (nodeId == lruAddr->nodeId) {
			recycleHeadFromLru();
			return;
		}
		struct LruNode *delLru = (entry + nodeId)->lru;
		if (nodeId == lruAddr->pre) {
			lruAddr->pre = delLru->pre;	
		}
		if (nodeId == lruAddr->next) {
			lruAddr->next = delLru->next;
			if (delLru->next != invalidId)
				(entry + delLru->next)->lru->pre = lruAddr->nodeId;
		}
		else {
			(entry + delLru->pre)->lru->next = delLru->next;
			if (lruAddr->next != invalidId)
				(entry + delLru->next)->lru->pre = delLru->pre;
		}
		free(delLru);
	}
	
	template <typename K, typename V>
	void cache<K, V>::MoveToTailLru(uint32_t nodeId) {
		if (nodeId == lruAddr->pre)
			return;
		recycleNodeFromLru(nodeId);
		addNewNodeToLru(nodeId);
	}
}

#endif
