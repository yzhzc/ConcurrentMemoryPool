#include "PageCache.h"


PageCache PageCache::_sInst;	// 单例模式对象定义


// 获取一个k页的Span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	
	// 超过Page最大管理页
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);	// 直接在堆上获取大于128页的内存
		Span* span = _spanPool.New();	// 从定长内存池获取一个Span对象

		// 将内存地址计算成页号，用基数树记录这块内存地址和这个Span关联
		span->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;	
		span->_n = k;
		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);

		return span;
	}

	// 检查第k个桶里是否有span, 有就直接拿走
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();

		// 建立id和span的映射，方便central cache回收小块内存时，查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}
		
	// 检查后面桶是否有span,如果有就将大号span切分
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (_spanLists[i].Empty())
			continue;

		Span* nSpan = _spanLists[i].PopFront();
		Span* kSpan = _spanPool.New();

		// 在nSpan头部头切一个k页
		kSpan->_pageId = nSpan->_pageId;
		kSpan->_n = k;

		nSpan->_pageId += k;
		nSpan->_n -= k;

		// 将切剩下的Span挂到对应大小的位置
		_spanLists[nSpan->_n].PushFront(nSpan);

		//存储nSpan的首页和尾页跟nSpan映射, 方便PageCache回收合并内存时, 查找对应nSpan
		//_idSpanMap[nSpan->_pageId] = nSpan;
		//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
		_idSpanMap.set(nSpan->_pageId, nSpan);
		_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

		// 建立id和Span的映射, 方便CentralCache回收小块内存时,找到对应Span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;	// Span内每个页都映射到这个Span
			_idSpanMap.set(kSpan->_pageId + i, kSpan);

		}

		return kSpan;
	}

	// 没有大页的Span了,只能找堆要一个最大页的Span
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1); 

	// 将内存地址计算成页号,因为VirtualAlloc()设置了起始地址为0，所以(>> PAGE_SHIFT)移动的都是0
	bigSpan->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	// 将Span插入最大页的桶中, 此时列表里已经拥有可以切分的大块页了
	// 只需重新调用一次NewSpan()
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// 映射内存对象原本所在的span
Span* PageCache::Map0bjjectToSpan(void* obj)
{
	PAGE_ID id = reinterpret_cast<PAGE_ID>(obj) >> PAGE_SHIFT;	//将内存地址计算成页号

	/*
	// 读取访问_idSpanMap时保证线程安全, 防止读取时其他线程正在修改
	std::unique_lock<std::mutex> look(PageCache::GetPageMtx());

	// 通过id找对应Span
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
		return ret->second;

	assert(false);
	return nullptr;
	*/
	Span* ret = reinterpret_cast<Span*>(_idSpanMap.get(id));
	assert(ret != nullptr);

	return ret;
}

// 释放空闲span回到Pagecache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 处理大于128页的Span
	if (span->_n > NPAGES - 1)
	{
		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		_spanPool.Delete(span);

		return;
	}

	// 对Span前后的页,尝试进行合并,解决内存碎片问题
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;

		//auto ret = _idSpanMap.find(prevTd);

		//if (ret == _idSpanMap.end())	// 前面没有节点
		//	break;
		// 
		//Span* prevSpan = ret->second;

		auto ret = reinterpret_cast<Span*>(_idSpanMap.get(prevId));
		if (ret == nullptr)	// 前面没有节点
			break;

		Span* prevSpan = ret;

		if (prevSpan->_isUse == true)	// 前面节点正在使用
			break;

		if (prevSpan->_n + span->_n > NPAGES - 1)	//合并完大于最大页,无法管理
			break;

		// 合并后将开头替换成span, 将prevSpan连接属性移除
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;
		_spanLists[prevSpan->_n].Erase(prevSpan);

		_spanPool.Delete(prevSpan);
	}

	while (true)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		//auto ret = _idSpanMap.find(nextId);

		//if (ret == _idSpanMap.end())	// 后面没有节点
		//	break;

		//Span* nextSpan = ret->second;

		auto ret = reinterpret_cast<Span*>(_idSpanMap.get(nextId));
		if (ret == nullptr)	// 前面节点正在使用
			break;

		Span* nextSpan = ret;

		if (nextSpan->_isUse == true)	// 后面节点正在使用
			break;

		if (nextSpan->_n + span->_n > NPAGES - 1)	//合并完大于最大页,无法管理
			break;

		// 将nextSpan接在span后面, 将nextSpan连接属性移除
		span->_n += nextSpan->_n;
		_spanLists[nextSpan->_n].Erase(nextSpan);

		_spanPool.Delete(nextSpan);
	}

	// 将合并后的Span插到对应大小桶中
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}