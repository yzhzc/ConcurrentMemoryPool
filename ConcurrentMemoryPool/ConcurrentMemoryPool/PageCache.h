#pragma once

#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

class PageCache
{
public:
	// 单例模式(启动直接创建模式)
	static PageCache* GetInstance(){
		return &_sInst;
	}

	// 获取全局锁
	std::mutex& GetPageMtx(){
		return _pageMtx;
	}

	// 获取一个k页的Span
	Span* NewSpan(size_t k);

	// 映射内存对象原本所在的span
	Span* Map0bjjectToSpan(void* obj);

	// 释放空闲span回到Pagecache，并合并相邻的span
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