#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

class PageCache
{
public:
	// ����ģʽ(����ֱ�Ӵ���ģʽ)
	static PageCache* GetInstance(){
		return &_sInst;
	}

	// ��ȡȫ����
	std::mutex& GetPageMtx(){
		return _pageMtx;
	}

	// ��ȡһ��kҳ��Span
	Span* NewSpan(size_t k);

	// ӳ���ڴ����ԭ�����ڵ�span
	Span* Map0bjjectToSpan(void* obj);

	// �ͷſ���span�ص�Pagecache�����ϲ����ڵ�span
	void ReleaseSpanToPageCache(Span* span);


private:
	SpanList _spanLists[NPAGES]; 
	ObjectPool<Span> _spanPool;
	std::mutex _pageMtx;
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	TCMalloc_PageMap1<WINNUM - PAGE_SHIFT> _idSpanMap;

private:
	PageCache(){}
	PageCache(const PageCache&) = delete;

	static PageCache _sInst;
};