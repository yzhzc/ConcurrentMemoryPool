#include "PageCache.h"


PageCache PageCache::_sInst;	// ����ģʽ������


// ��ȡһ��kҳ��Span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	
	// ����Page������ҳ
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);	// ֱ���ڶ��ϻ�ȡ����128ҳ���ڴ�
		Span* span = _spanPool.New();	// �Ӷ����ڴ�ػ�ȡһ��Span����

		// ���ڴ��ַ�����ҳ�ţ��û�������¼����ڴ��ַ�����Span����
		span->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;	
		span->_n = k;
		//_idSpanMap[span->_pageId] = span;
		_idSpanMap.set(span->_pageId, span);

		return span;
	}

	// ����k��Ͱ���Ƿ���span, �о�ֱ������
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();

		// ����id��span��ӳ�䣬����central cache����С���ڴ�ʱ�����Ҷ�Ӧ��span
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, kSpan);
		}

		return kSpan;
	}
		
	// ������Ͱ�Ƿ���span,����оͽ����span�з�
	for (size_t i = k + 1; i < NPAGES; ++i)
	{
		if (_spanLists[i].Empty())
			continue;

		Span* nSpan = _spanLists[i].PopFront();
		Span* kSpan = _spanPool.New();

		// ��nSpanͷ��ͷ��һ��kҳ
		kSpan->_pageId = nSpan->_pageId;
		kSpan->_n = k;

		nSpan->_pageId += k;
		nSpan->_n -= k;

		// ����ʣ�µ�Span�ҵ���Ӧ��С��λ��
		_spanLists[nSpan->_n].PushFront(nSpan);

		//�洢nSpan����ҳ��βҳ��nSpanӳ��, ����PageCache���պϲ��ڴ�ʱ, ���Ҷ�ӦnSpan
		//_idSpanMap[nSpan->_pageId] = nSpan;
		//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
		_idSpanMap.set(nSpan->_pageId, nSpan);
		_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, nSpan);

		// ����id��Span��ӳ��, ����CentralCache����С���ڴ�ʱ,�ҵ���ӦSpan
		for (PAGE_ID i = 0; i < kSpan->_n; ++i)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;	// Span��ÿ��ҳ��ӳ�䵽���Span
			_idSpanMap.set(kSpan->_pageId + i, kSpan);

		}

		return kSpan;
	}

	// û�д�ҳ��Span��,ֻ���Ҷ�Ҫһ�����ҳ��Span
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1); 

	// ���ڴ��ַ�����ҳ��,��ΪVirtualAlloc()��������ʼ��ַΪ0������(>> PAGE_SHIFT)�ƶ��Ķ���0
	bigSpan->_pageId = reinterpret_cast<PAGE_ID>(ptr) >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;

	// ��Span�������ҳ��Ͱ��, ��ʱ�б����Ѿ�ӵ�п����зֵĴ��ҳ��
	// ֻ�����µ���һ��NewSpan()
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}

// ӳ���ڴ����ԭ�����ڵ�span
Span* PageCache::Map0bjjectToSpan(void* obj)
{
	PAGE_ID id = reinterpret_cast<PAGE_ID>(obj) >> PAGE_SHIFT;	//���ڴ��ַ�����ҳ��

	/*
	// ��ȡ����_idSpanMapʱ��֤�̰߳�ȫ, ��ֹ��ȡʱ�����߳������޸�
	std::unique_lock<std::mutex> look(PageCache::GetPageMtx());

	// ͨ��id�Ҷ�ӦSpan
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

// �ͷſ���span�ص�Pagecache�����ϲ����ڵ�span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// �������128ҳ��Span
	if (span->_n > NPAGES - 1)
	{
		void* ptr = reinterpret_cast<void*>(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		_spanPool.Delete(span);

		return;
	}

	// ��Spanǰ���ҳ,���Խ��кϲ�,����ڴ���Ƭ����
	while (true)
	{
		PAGE_ID prevId = span->_pageId - 1;

		//auto ret = _idSpanMap.find(prevTd);

		//if (ret == _idSpanMap.end())	// ǰ��û�нڵ�
		//	break;
		// 
		//Span* prevSpan = ret->second;

		auto ret = reinterpret_cast<Span*>(_idSpanMap.get(prevId));
		if (ret == nullptr)	// ǰ��û�нڵ�
			break;

		Span* prevSpan = ret;

		if (prevSpan->_isUse == true)	// ǰ��ڵ�����ʹ��
			break;

		if (prevSpan->_n + span->_n > NPAGES - 1)	//�ϲ���������ҳ,�޷�����
			break;

		// �ϲ��󽫿�ͷ�滻��span, ��prevSpan���������Ƴ�
		span->_pageId = prevSpan->_pageId;
		span->_n += prevSpan->_n;
		_spanLists[prevSpan->_n].Erase(prevSpan);

		_spanPool.Delete(prevSpan);
	}

	while (true)
	{
		PAGE_ID nextId = span->_pageId + span->_n;

		//auto ret = _idSpanMap.find(nextId);

		//if (ret == _idSpanMap.end())	// ����û�нڵ�
		//	break;

		//Span* nextSpan = ret->second;

		auto ret = reinterpret_cast<Span*>(_idSpanMap.get(nextId));
		if (ret == nullptr)	// ǰ��ڵ�����ʹ��
			break;

		Span* nextSpan = ret;

		if (nextSpan->_isUse == true)	// ����ڵ�����ʹ��
			break;

		if (nextSpan->_n + span->_n > NPAGES - 1)	//�ϲ���������ҳ,�޷�����
			break;

		// ��nextSpan����span����, ��nextSpan���������Ƴ�
		span->_n += nextSpan->_n;
		_spanLists[nextSpan->_n].Erase(nextSpan);

		_spanPool.Delete(nextSpan);
	}

	// ���ϲ����Span�嵽��Ӧ��СͰ��
	_spanLists[span->_n].PushFront(span);
	span->_isUse = false;
	//_idSpanMap[span->_pageId] = span;
	//_idSpanMap[span->_pageId + span->_n - 1] = span;
	_idSpanMap.set(span->_pageId, span);
	_idSpanMap.set(span->_pageId + span->_n - 1, span);
}