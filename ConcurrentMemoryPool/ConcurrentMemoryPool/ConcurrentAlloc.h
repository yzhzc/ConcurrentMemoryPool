#pragma once

#include "Common.h"
#include "ThreadCache.h"
#include "CentralCache.h"
#include "PageCache.h"

static ObjectPool<ThreadCache> tcPool;

// Ϊ�̷߳��������ڴ�
static void* ConcurrentAlloc(size_t size)
{
	if (size > MAX_BYTES)
	{
		size_t alignSize = SizeClass::RoundUp(size);	// ����һ�´�С
		size_t kpage = alignSize >> PAGE_SHIFT;	//����ҳ��

		PageCache::GetInstance()->GetPageMtx().lock();
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		span->_objSize = alignSize;
		PageCache::GetInstance()->GetPageMtx().unlock();

		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGE_SHIFT);

		return ptr;
	}
	else
	{
		//ͨ��TLS�����Ļ�ȡÿ���߳�ר����ThreadCache����
		if (pTLSThreadCache == nullptr)
			pTLSThreadCache = tcPool.New();

		return pTLSThreadCache->Allocate(size);
	}	
}

// �����߳��е��ڴ�
static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->Map0bjjectToSpan(ptr);
	size_t size = span->_objSize;

	if (size > MAX_BYTES)
	{
		// �����Span����PageCache�ϲ�
		PageCache::GetInstance()->GetPageMtx().lock();	//Page����
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->GetPageMtx().unlock();	//Page����
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, size);
	}
}

// ÿ���̵߳����Ķ��ƻ��������߳��˳�ʱ����������pTLSThreadCache�ռ䵽�����ڴ����
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