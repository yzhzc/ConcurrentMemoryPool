#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <time.h>
#include <assert.h>


#ifdef _WIN32
	#include <windows.h>
#elif 
	// Linux
#endif


using std::cout;
using std::endl;

// 小于等于MAX_BYTES，就找thread cache申请
// 大于MAX_BYTES，就直接找page cache或者系统堆申请
static const size_t MAX_BYTES = 256 * 1024;	

static const size_t NFREELIST = 208;	// thread cache 和 central cache自由链表哈希桶的表大小
static const size_t NPAGES = 129;		// page cache 管理span list哈希表大小
static const size_t PAGE_SHIFT = 13;	// 页大小转换偏移, 即一页定义为2^13,也就是8KB

#ifdef _WIN64	// x64既能进win64也能进win32, x86只能进win32
	typedef unsigned long long PAGE_ID;
	static const size_t WINNUM = 64;
#elif _WIN32
	typedef size_t PAGE_ID;
	static const size_t WINNUM = 32;
#elif 
	// Linux
#endif // _WIN64

/*#####################################################################################*/

// 直接去堆上取kpage页内存
inline static void* SystemAlloc(size_t kpage)
{
	// windows下用VirtualAlloc
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE);
#else
	// linux下brk mmap等
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

// 直接释放内存到堆上
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
#endif
}

/*#####################################################################################*/

// 取对象的头4/8个字节
static void*& NextObj(void* obj)
{
	// 32位下*(void**)是4个字节
	// 64位下*(void**)是8个字节
	return *(reinterpret_cast<void**>(obj));
}

/*#####################################################################################*/

// 管理桶内的内存对象链表
class FreeList
{
public:
	// 头插内存对象
	void Push(void* obj)
	{
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}

	// 头插一串内存对象
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// 拆卸一串内存对象, 返回删除的那一串头指针
	void* PopRange(size_t n)
	{
		assert(n <= _size);
		void* start = _freeList;
		void* end = start;
		for (size_t i = 0; i < n - 1; ++i)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;

		return start;
	}

	// 头删内存对象
	void* Pop()
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		--_size;

		return obj;
	}

	// 缓存区是否为空
	bool Empty() {
		return _freeList == nullptr;
	}

	// 获取桶内,内存对象链表最大长度
	size_t& MaxSize() {
		return _maxSize;
	}

	size_t Size() {
		return _size;
	}

private:
	void* _freeList = nullptr;	// 内存桶的链表头节点
	size_t _maxSize = 1;		// 每个桶单次获取内存块的数量,会随着获取次数增加而增加,最大增至512 + 1
	size_t _size = 0;			// 桶内内存对象数量
};

/*#####################################################################################*/

//计算对象大小的映射规则
class SizeClass
{
public:
	// 计算对齐
	// 整体控制在最多10%左右的内碎片浪费
	// [1,128] 8byte对齐       freelist[0,16)
	// [128+1,1024] 16byte对齐   freelist[16,72)
	// [1024+1,8*1024] 128byte对齐   freelist[72,128)
	// [8*1024+1,64*1024] 1024byte对齐     freelist[128,184)
	// [64*1024+1,256*1024] 8*1024byte对齐   freelist[184,208)
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		/*
		// 普通写法
		size_t alignSize;
		if (bytes % alignNum != 0)
			alignSize = (bytes / alignNum + 1) * alignNum;
		else
			alignSize = bytes;

		return alignSize;
		*/

		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	// 计算对齐后的实际获取内存大小
	static inline size_t RoundUp(size_t size)
	{
		if (size <= 128) {
			return _RoundUp(size, 8);
		}
		else if (size <= 1024) {
			return _RoundUp(size, 16);
		}
		else if (size <= 8 * 1024) {
			return _RoundUp(size, 128);
		}
		else if (size <= 64 * 1024) {
			return _RoundUp(size, 1024);
		}
		else if (size <= 256 * 1024) {
			return _RoundUp(size, 8 * 1024);
		}
		else {
			return _RoundUp(size, 1 << PAGE_SHIFT);
		}
	}

	//alignNum是对齐数二进制位-1
	static inline size_t _Index(size_t bytes, size_t alignNum)
	{
		/*
		// 普通写法,alignNum是对齐数
		if (bytes % alignNum != 0)
			return bytes / alignNum;
		else
			return bytes / alignNum - 1;
		*/

		return ((bytes + (1 << alignNum) - 1) >> alignNum) - 1;
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1]
				+ group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] +
				group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
		}
	}

	// 一次从大内存切多少块小内存
	// (需要的内存大小)
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 小对象一次批量上限低
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// 计算一次向系统获取几个页
	static size_t NumMovePage(size_t size)
	{
		// 单个对象 8byte
		// ...
		// 单个对象 256KB
		size_t num = NumMoveSize(size);	// size大小的内存块数量
		size_t npage = num * size;	// 总共所需内存大小
		npage >>= PAGE_SHIFT;	// 计算需要多少页内存
		if (npage == 0)
			npage = 1;

		return npage;
	}
};

/*#####################################################################################*/

// 管理多个连续页的大块内存跨度结构
struct Span
{
	PAGE_ID _pageId = 0;		//大块内存 "起始页" 的 "页号"
	size_t _n = 0;				// 页的数量

	Span* _next = nullptr;		// 双向链表指针
	Span* _prev = nullptr;

	size_t _objSize = 0;		// 切好的小对象大小
	size_t _useCount = 0;		// 切好的小块内存,被分配给ThreadCache的计数
	void* _freeList = nullptr;	// 指向切好的小块内存的自由链表

	bool _isUse = false;		// 是否在被使用
};

/*#####################################################################################*/

// 带头双向循环链表
// Page Cache和Central Cache每个桶中的span链表
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	Span* Begin() {
		return _head->_next;
	}

	Span* End() {
		return _head;
	}

	// 判断Span是否为空
	bool Empty() {
		return _head->_next == _head;
	}

	// 头插
	void PushFront(Span* span) {
		Inster(Begin(), span);
	}
	
	// 拆卸头部span,返回拆卸的Span
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);

		return front;
	}

	// 在节点前插入新节点
	void Inster(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		prev->_next = newSpan;

		newSpan->_prev = prev;
		newSpan->_next = pos;

		pos->_prev = newSpan;
	}

	// 拆卸当前节点
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;

		//无需delete
	}

	// 获取桶锁
	std::mutex& GetSpanMtx() {
		return _mtx;
	}

private:
	Span* _head;	// 头结点
	std::mutex _mtx; // 桶锁
};

/*#####################################################################################*/