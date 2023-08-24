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

// С�ڵ���MAX_BYTES������thread cache����
// ����MAX_BYTES����ֱ����page cache����ϵͳ������
static const size_t MAX_BYTES = 256 * 1024;	

static const size_t NFREELIST = 208;	// thread cache �� central cache���������ϣͰ�ı��С
static const size_t NPAGES = 129;		// page cache ����span list��ϣ���С
static const size_t PAGE_SHIFT = 13;	// ҳ��Сת��ƫ��, ��һҳ����Ϊ2^13,Ҳ����8KB

#ifdef _WIN64	// x64���ܽ�win64Ҳ�ܽ�win32, x86ֻ�ܽ�win32
	typedef unsigned long long PAGE_ID;
	static const size_t WINNUM = 64;
#elif _WIN32
	typedef size_t PAGE_ID;
	static const size_t WINNUM = 32;
#elif 
	// Linux
#endif // _WIN64

/*#####################################################################################*/

// ֱ��ȥ����ȡkpageҳ�ڴ�
inline static void* SystemAlloc(size_t kpage)
{
	// windows����VirtualAlloc
#ifdef _WIN32
	void* ptr = VirtualAlloc(0, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE,
		PAGE_READWRITE);
#else
	// linux��brk mmap��
#endif
	if (ptr == nullptr)
		throw std::bad_alloc();

	return ptr;
}

// ֱ���ͷ��ڴ浽����
inline static void SystemFree(void* ptr)
{
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap��
#endif
}

/*#####################################################################################*/

// ȡ�����ͷ4/8���ֽ�
static void*& NextObj(void* obj)
{
	// 32λ��*(void**)��4���ֽ�
	// 64λ��*(void**)��8���ֽ�
	return *(reinterpret_cast<void**>(obj));
}

/*#####################################################################################*/

// ����Ͱ�ڵ��ڴ��������
class FreeList
{
public:
	// ͷ���ڴ����
	void Push(void* obj)
	{
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}

	// ͷ��һ���ڴ����
	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}

	// ��жһ���ڴ����, ����ɾ������һ��ͷָ��
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

	// ͷɾ�ڴ����
	void* Pop()
	{
		assert(_freeList);
		void* obj = _freeList;
		_freeList = NextObj(_freeList);
		--_size;

		return obj;
	}

	// �������Ƿ�Ϊ��
	bool Empty() {
		return _freeList == nullptr;
	}

	// ��ȡͰ��,�ڴ����������󳤶�
	size_t& MaxSize() {
		return _maxSize;
	}

	size_t Size() {
		return _size;
	}

private:
	void* _freeList = nullptr;	// �ڴ�Ͱ������ͷ�ڵ�
	size_t _maxSize = 1;		// ÿ��Ͱ���λ�ȡ�ڴ�������,�����Ż�ȡ�������Ӷ�����,�������512 + 1
	size_t _size = 0;			// Ͱ���ڴ��������
};

/*#####################################################################################*/

//��������С��ӳ�����
class SizeClass
{
public:
	// �������
	// ������������10%���ҵ�����Ƭ�˷�
	// [1,128] 8byte����       freelist[0,16)
	// [128+1,1024] 16byte����   freelist[16,72)
	// [1024+1,8*1024] 128byte����   freelist[72,128)
	// [8*1024+1,64*1024] 1024byte����     freelist[128,184)
	// [64*1024+1,256*1024] 8*1024byte����   freelist[184,208)
	static inline size_t _RoundUp(size_t bytes, size_t alignNum)
	{
		/*
		// ��ͨд��
		size_t alignSize;
		if (bytes % alignNum != 0)
			alignSize = (bytes / alignNum + 1) * alignNum;
		else
			alignSize = bytes;

		return alignSize;
		*/

		return ((bytes + alignNum - 1) & ~(alignNum - 1));
	}

	// ���������ʵ�ʻ�ȡ�ڴ��С
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

	//alignNum�Ƕ�����������λ-1
	static inline size_t _Index(size_t bytes, size_t alignNum)
	{
		/*
		// ��ͨд��,alignNum�Ƕ�����
		if (bytes % alignNum != 0)
			return bytes / alignNum;
		else
			return bytes / alignNum - 1;
		*/

		return ((bytes + (1 << alignNum) - 1) >> alignNum) - 1;
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// ÿ�������ж��ٸ���
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

	// һ�δӴ��ڴ��ж��ٿ�С�ڴ�
	// (��Ҫ���ڴ��С)
	static size_t NumMoveSize(size_t size)
	{
		assert(size > 0);
		// [2, 512]��һ�������ƶ����ٸ������(������)����ֵ
		// С����һ���������޸�
		// С����һ���������޵�
		int num = MAX_BYTES / size;
		if (num < 2)
			num = 2;

		if (num > 512)
			num = 512;

		return num;
	}

	// ����һ����ϵͳ��ȡ����ҳ
	static size_t NumMovePage(size_t size)
	{
		// �������� 8byte
		// ...
		// �������� 256KB
		size_t num = NumMoveSize(size);	// size��С���ڴ������
		size_t npage = num * size;	// �ܹ������ڴ��С
		npage >>= PAGE_SHIFT;	// ������Ҫ����ҳ�ڴ�
		if (npage == 0)
			npage = 1;

		return npage;
	}
};

/*#####################################################################################*/

// ����������ҳ�Ĵ���ڴ��Ƚṹ
struct Span
{
	PAGE_ID _pageId = 0;		//����ڴ� "��ʼҳ" �� "ҳ��"
	size_t _n = 0;				// ҳ������

	Span* _next = nullptr;		// ˫������ָ��
	Span* _prev = nullptr;

	size_t _objSize = 0;		// �кõ�С�����С
	size_t _useCount = 0;		// �кõ�С���ڴ�,�������ThreadCache�ļ���
	void* _freeList = nullptr;	// ָ���кõ�С���ڴ����������

	bool _isUse = false;		// �Ƿ��ڱ�ʹ��
};

/*#####################################################################################*/

// ��ͷ˫��ѭ������
// Page Cache��Central Cacheÿ��Ͱ�е�span����
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

	// �ж�Span�Ƿ�Ϊ��
	bool Empty() {
		return _head->_next == _head;
	}

	// ͷ��
	void PushFront(Span* span) {
		Inster(Begin(), span);
	}
	
	// ��жͷ��span,���ز�ж��Span
	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);

		return front;
	}

	// �ڽڵ�ǰ�����½ڵ�
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

	// ��ж��ǰ�ڵ�
	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);

		Span* prev = pos->_prev;
		Span* next = pos->_next;

		prev->_next = next;
		next->_prev = prev;

		//����delete
	}

	// ��ȡͰ��
	std::mutex& GetSpanMtx() {
		return _mtx;
	}

private:
	Span* _head;	// ͷ���
	std::mutex _mtx; // Ͱ��
};

/*#####################################################################################*/