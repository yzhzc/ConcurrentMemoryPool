#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "CentralCache.h"
#include "PageCache.h"

static ObjectPool<ThreadCache> tcPool;

// 为线程分配所需内存
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeClass::RoundUp(size);	// 对齐一下大小
		size_t kpage = alignSize >> PAGE_SHIFT;	//计算页数

		PageCache::GetInstance()->GetPageMtx().lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = alignSize;
		PageCache::GetInstance()->GetPageMtx().unlock();

		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGE_SHIFT);

		return ptr;
	}
	else
	{
		//通过TLS无锁的获取每个线程专属的ThreadCache对象
		if (pTLSThreadCache == nullptr)
			pTLSThreadCache = tcPool.New();

		return pTLSThreadCache->Allocate(size);
	}	
}

// 回收线程中的内存
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->Map0bjjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		// 将这个Span交给PageCache合并
		PageCache::GetInstance()->GetPageMtx().lock();	//Page上锁
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->GetPageMtx().unlock();	//Page解锁
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}

// 每个线程单独的定制回收器，线程退出时，析构回收pTLSThreadCache空间到定长内存池中
class TLSfree
{
public:
	~TLSfree()
	{
		if (pTLSThreadCache != nullptr)
			tcPool.Delete(pTLSThreadCache);
	}
};

static _declspec(thread) TLSfree tf;