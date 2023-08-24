#include "CentralCache.h"
#include "PageCache.h"


CentralCache CentralCache::_sInst;	//单例模式对象定义


// 从SpanList获取一个span,没有就向page cache申请切内存
// GetOneSpan(桶位置, 获取内存大小)
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// 在CentralCache中, 查看当前对应的桶中, 是否挂着Span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
			return it;

		it = it->_next;
	}

	// 没有空闲的Span, 只能从PageCache申请Page
	// 先把当前桶内的锁解除,这样如果有其他线程释放内存对象回来不会阻塞
	list.GetSpanMtx().unlock();

	// 向PageCache申请时上Page全局的锁
	PageCache::GetInstance()->GetPageMtx().lock();

	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true; //正在被使用
	span->_objSize = size;

	PageCache::GetInstance()->GetPageMtx().unlock();
	// 解除Page全局的锁
	// 此时不需要加上刚刚解除的桶锁,没有其他线程会访问到这个Span

	// 计算span的大块内存的起始地址和大块内存的大小
	char* start = reinterpret_cast<char*>(span->_pageId << PAGE_SHIFT);	// 当前页号起始地址
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// 把大块内存切成自由链表挂到桶中(尾插)
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;

	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}

	NextObj(tail) = nullptr;	// 收尾置空

	// 在将Span交给CentralCache之后所有线程都可以访问到这个Span
	// 所以在切分好,交给CentralCache之前加上桶锁
	list.GetSpanMtx().lock();
	list.PushFront(span);	//交给CentralCache使用
		
	return span;
}

// 从中心缓存获取一定数量的对象给ThreadCache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 上锁
	// 在单个桶内操作,不影响其他桶,只需要在操作的那个桶里上锁就行了
	_spanLists[index].GetSpanMtx().lock();

	//从对应桶中获取一个非空的span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//从span获取内存对象,如果不够batchNum个,有多少拿多少
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;	//获取内存对象数量

	while ( i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}

	// 将桶内剩余内存对象挂回桶内
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;	//记录当前span交出去多少内存对象

	_spanLists[index].GetSpanMtx().unlock();
	// 解锁

	return actualNum;
}

// 将一定数量的对象释放到span跨度
void CentralCache::ReleaseListToSpans(void* start, size_t index)
{
	_spanLists[index].GetSpanMtx().lock();

	while (start)
	{
		// 通过内存地址映射获取对应Span
		Span* span = PageCache::GetInstance()->Map0bjjectToSpan(start); 
		void* next = NextObj(start);

		// 将内存对象,头插 -> CentralCache -> 对应大小的桶 -> 对应的Span -> _freeList
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;

		// 当这个Span里切出去的内存对象全部回来后, 这个Span就可以还给PageCache
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_prev = nullptr;
			span->_next = nullptr;

			// 这个Span已经从CentralCache中拆卸下来, 别人访问不到
			// 可以解桶锁, 让其他线程在桶内获取/释放
			_spanLists[index].GetSpanMtx().unlock();

			// 将这个Span交给PageCache合并
			PageCache::GetInstance()->GetPageMtx().lock();	//Page上锁
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->GetPageMtx().unlock();	//Page解锁

			_spanLists[index].GetSpanMtx().lock();
		}

		start = next;
	}

	_spanLists[index].GetSpanMtx().unlock();
}