#pragma once

#include"Common.h"


class CentralCache
{
public:
	// ����ģʽ(����ֱ�Ӵ���ģʽ)
	static CentralCache* GetInstance() {
		return &_sInst;
	}

	// ��SpanList����page cache��ȡһ��span
	// GetOneSpan(Ͱλ��, ��ȡ�ڴ��С)
	Span* GetOneSpan(SpanList& list, size_t size);

	// �����Ļ����ȡһ�������Ķ����ThreadCache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// ��һ�������Ķ����ͷŵ�span���
	void ReleaseListToSpans(void* start, size_t index);


private:
	SpanList _spanLists[NFREELIST];

private:
	CentralCache(){}
	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;	//����
};