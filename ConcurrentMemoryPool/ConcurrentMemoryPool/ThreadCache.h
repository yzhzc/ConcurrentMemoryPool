#pragma once

#include"Common.h"

class ThreadCache
{
public:
	// 申请内存对象
	void* Allocate(size_t size);
	// 释放内存对象
	void Deallocate(void* ptr, size_t size);

	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t size);

	// 释放对象时，链表过长时，回收内存回到中心缓存
	void ListTooLong(size_t index);


private:
	FreeList _freeLists[NFREELIST]; //每个线程都有208个桶
};

//TLS 线程本地存储, 使每个线程都有自己的ThreadCache
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;