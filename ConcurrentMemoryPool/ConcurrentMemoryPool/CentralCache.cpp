#include "CentralCache.h"
#include "PageCache.h"


CentralCache CentralCache::_sInst;	//����ģʽ������


// ��SpanList��ȡһ��span,û�о���page cache�������ڴ�
// GetOneSpan(Ͱλ��, ��ȡ�ڴ��С)
Span* CentralCache::GetOneSpan(SpanList& list, size_t size)
{
	// ��CentralCache��, �鿴��ǰ��Ӧ��Ͱ��, �Ƿ����Span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
			return it;

		it = it->_next;
	}

	// û�п��е�Span, ֻ�ܴ�PageCache����Page
	// �Ȱѵ�ǰͰ�ڵ������,��������������߳��ͷ��ڴ���������������
	list.GetSpanMtx().unlock();

	// ��PageCache����ʱ��Pageȫ�ֵ���
	PageCache::GetInstance()->GetPageMtx().lock();

	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));
	span->_isUse = true; //���ڱ�ʹ��
	span->_objSize = size;

	PageCache::GetInstance()->GetPageMtx().unlock();
	// ���Pageȫ�ֵ���
	// ��ʱ����Ҫ���ϸոս����Ͱ��,û�������̻߳���ʵ����Span

	// ����span�Ĵ���ڴ����ʼ��ַ�ʹ���ڴ�Ĵ�С
	char* start = reinterpret_cast<char*>(span->_pageId << PAGE_SHIFT);	// ��ǰҳ����ʼ��ַ
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;

	// �Ѵ���ڴ��г���������ҵ�Ͱ��(β��)
	span->_freeList = start;
	start += size;
	void* tail = span->_freeList;

	while (start < end)
	{
		NextObj(tail) = start;
		tail = start;
		start += size;
	}

	NextObj(tail) = nullptr;	// ��β�ÿ�

	// �ڽ�Span����CentralCache֮�������̶߳����Է��ʵ����Span
	// �������зֺ�,����CentralCache֮ǰ����Ͱ��
	list.GetSpanMtx().lock();
	list.PushFront(span);	//����CentralCacheʹ��
		
	return span;
}

// �����Ļ����ȡһ�������Ķ����ThreadCache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	// ����
	// �ڵ���Ͱ�ڲ���,��Ӱ������Ͱ,ֻ��Ҫ�ڲ������Ǹ�Ͱ������������
	_spanLists[index].GetSpanMtx().lock();

	//�Ӷ�ӦͰ�л�ȡһ���ǿյ�span
	Span* span = GetOneSpan(_spanLists[index], size);
	assert(span);
	assert(span->_freeList);

	//��span��ȡ�ڴ����,�������batchNum��,�ж����ö���
	start = span->_freeList;
	end = start;
	size_t i = 0;
	size_t actualNum = 1;	//��ȡ�ڴ��������

	while ( i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}

	// ��Ͱ��ʣ���ڴ����һ�Ͱ��
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	span->_useCount += actualNum;	//��¼��ǰspan����ȥ�����ڴ����

	_spanLists[index].GetSpanMtx().unlock();
	// ����

	return actualNum;
}

// ��һ�������Ķ����ͷŵ�span���
void CentralCache::ReleaseListToSpans(void* start, size_t index)
{
	_spanLists[index].GetSpanMtx().lock();

	while (start)
	{
		// ͨ���ڴ��ַӳ���ȡ��ӦSpan
		Span* span = PageCache::GetInstance()->Map0bjjectToSpan(start); 
		void* next = NextObj(start);

		// ���ڴ����,ͷ�� -> CentralCache -> ��Ӧ��С��Ͱ -> ��Ӧ��Span -> _freeList
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		span->_useCount--;

		// �����Span���г�ȥ���ڴ����ȫ��������, ���Span�Ϳ��Ի���PageCache
		if (span->_useCount == 0)
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_prev = nullptr;
			span->_next = nullptr;

			// ���Span�Ѿ���CentralCache�в�ж����, ���˷��ʲ���
			// ���Խ�Ͱ��, �������߳���Ͱ�ڻ�ȡ/�ͷ�
			_spanLists[index].GetSpanMtx().unlock();

			// �����Span����PageCache�ϲ�
			PageCache::GetInstance()->GetPageMtx().lock();	//Page����
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->GetPageMtx().unlock();	//Page����

			_spanLists[index].GetSpanMtx().lock();
		}

		start = next;
	}

	_spanLists[index].GetSpanMtx().unlock();
}