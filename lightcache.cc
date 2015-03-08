#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unistd.h>
#include <cstdlib>
#include <sys/mann.h>

#include "err.h"
#include "lightcache.h"

namespace lightcache {
	template <typename K, typename V>
	cache<K, V>::cache(const char * pathname,
				uint32_t tableSize,
				uint32_t totalNode,
				std::function<size_t(K)> hash = std::hash<K>()) {
		memSize = sizeof(tableHead) + 
				  sizeof(struct LruNode) + 
				  tableSize * sizeof(struct BucketHead) +
				  totalNode * sizeof(struct Node);
		memAddr = static_cast<uintptr_t> 
					attachFile(pathname, memSize);
		if (memAddr = NULL) 
			errexit("cache() attachFile");

		tableAddr = static_cast<struct TableHead *> memAddr;
		bucketAddr = static_cast<struct LruNode *>
				(tableAddr + sizeof(struct TableHead));
		lruAddr = static_cast<struct LruNode *>
				(bucketAddr +tableSize * sizeof(struct BucketList));
		entry = static_cast<struct Node *>
				(lruAddr + sizeof(struct LruNode));

		tableAddr->tableLen = tableSize;
		tableAddr->totalNode = totalNode;
		strncpy(tableAddr->fileName, pathname,	sizeof(tableAddr->fileName));

		if (tableAddr->usedCount == 0) {
			//this is a new cache
			for (ptrdiff_t i = 0; i < tableSize; i++) {
				(bucketAddr + i)->head = invalidId;
				(bucketAddr + i)->tail = invalidId;
			}
			tableAddr->freeNode = 0;
			tableAddr->recycledNode = invalidId;

			lruAddr->pre = lruAddr->next = lruAddr->nodeId = invalidId;
		}	
		std::function<size_t(K)> hashFunc = hash; 
	}
	
	template <typename K, typename V>
	cache<K, V>::~cache() {
		releaseFile(static_cast<void *>memAddr, memSize);
	}

	template <typename K, typename V>
	void cache<K, V>::set(const K &key, const V &value) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId != invalidId) { //modify old node
			(entry + nodeId)->value = value;
		} else { //add new node
			nodeId = getFreeNode();
			size_t hashCode = hashFunc(key);
			struct Node *newNode = entry + nodeId;
			newNode->key = key;
			newNode->hashCode = hashCode;
			newNode->value = value;
			uint32_t index = nodeId % tableAddr->tableLen;
			addNewNodeToBucket(nodeId, index);
			addNewNodeToLru(nodeId);
		}
	}

	template <typename K, typename V>
	const V &cache<K, V>::get(const K &key, const V &Else) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId)
			return Else;
		return (entry + nodeId)->value;
	}

	template <typename K, typename V>
	bool del(const K &key) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId) 
			return false;

		uint32_t index = nodeId % tableAddr->tableLen;
		recycleNodeFromBucket(nodeId, index);
		addNewNodeToLru(nodeId);
	}

	template <typename K, typename V>
	bool cache<K, V>::exist(K &key) {
		std::unique_lock<std::mutex> lock(mtx);

		uint32_t nodeId = getIdByKey(key);
		if (nodeId == invalidId)
			return false;
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
		if (access(pathname, F_OK) == 0) {
			//file exist
			if (access(pathname, R_OK|W_OK) == -1)
				return NULL;
			fd = open(name, O_RDWR);
			if (fd == -1)
				return NULL;
		} else {
			fd = open(pathname, O_CREAT|O_EXCL|O_RDWR);
			if (fd == -1) 
				return NULL;
		}
		if (ftruncate(fd, length) == -1)
			return NULL;
		void *addr = mmap(NULL, length, PROT_WRITE|PROT_READ, MAP_SHARED, fd, 0);
		close(fd);
		if (addr = MAP_FAILED)
			return NULL;
		return addr;
	}

	template <typename K, typename V> 
	void cache<K, V>::releaseFile(void *addr, size_t length) {
		if (addr != NULL && size_t != 0) {
			if (munmap(addr, length) == -1) 
				errexit("releaseFile");
		}
	}

	template <typename K, typename V>
	uint32_t cache<K, V>::getIdByKey(const K &key) {
		size_t hashCode = hashFunc(key);
		uint32_t index = hashCode % tableAddr->tableLen;
		struct BucketList *tmpList = bucketAddr + index;
		uint32_t nodeId = tmpList->head;
		while (nodeId != invalidId) {
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
			nodeId = tableAddr->recycleNode;
			tableAddr->recycleNode = (entry + nodeId)->next;
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
			struct Node *tmpLru = 
				static_cast<struct LruNode *>(malloc(sizeof(struct LruNode)));
			tmpLru->pre = lruAddr->pre;
			lruAddr->pre = nodeId;
			if (lruAddr->next == invalidId)
				lruAddr->next = nodeId;
			tmpLru->next = invalidId;
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
		lruAddr->nodeId = lruAddr->next;
		if (lruAddr->nodeId != invalidId) {
			lruAddr->next = (entry + lruAddr->nodeId)->lru->next;
			free((entry + lruAddr->nodeId)->lru);
			(entry + lruAddr->nodeId)->lru = lruAddr;
		}
		else {
			lruAddr->pre = invalidId;
		}
	}

	template <typename K, typename V>
	uint32_t getFirstNodeFromLru() {
		return lruAddr->nodeId;
	}
}
