#include "ThreadCache.h"
#include "CentralCache.h"


// 从CentralCache获取一定数量内存对像挂到对应大小的内存桶中
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// 给多少内存的慢开始调节算法
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));

	//最开始不会向CentralCache要太多,随着索要次数平繁,逐渐从1增至512
	if (_freeLists[index].MaxSize() == batchNum)
		_freeLists[index].MaxSize() += 1;

	void* start = nullptr;
	void* end = nullptr;

	//不一定能能申请batchNum个,Span内不够batchNum个,剩多少就返回多少
	size_t actualNum =  CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);
	assert(start);

	//获取多个内存块时, 除了使用的那一个, 将多余的挂到对应桶中
	if (actualNum > 1)
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);

	return start;
}

// 申请内存对象
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);	// 获取对齐后的内存大小
	size_t index = SizeClass::Index(size);	// 获取所在桶的位置

	if (!_freeLists[index].Empty())
	{
		// 当前桶内有回收的空余内存
		return _freeLists[index].Pop();
	}
	else
	{
		// 从中心缓存获取对象
		return FetchFromCentralCache(index, alignSize);
	}
}

// 释放内存对象
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);

	// 找到对应映射的自由链表桶, 将对象插入
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// 当链表长度大于一次批量申请的内存时,拆卸一段list还给CentralCache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
		ListTooLong(index);
}

// 释放对象时，链表过长时，回收一串内存回到CentralCache
void ThreadCache::ListTooLong(size_t index)
{
	// 将要回收的一串内存对象从桶中拆下
	void* start = _freeLists[index].PopRange(_freeLists[index].MaxSize());

	// 通知CentralCache回收这串内存对象
	CentralCache::GetInstance()->ReleaseListToSpans(start, index);
}