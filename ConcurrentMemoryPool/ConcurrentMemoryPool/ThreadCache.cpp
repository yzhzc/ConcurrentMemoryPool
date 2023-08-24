#include "ThreadCache.h"
#include "CentralCache.h"


// ��CentralCache��ȡһ�������ڴ����ҵ���Ӧ��С���ڴ�Ͱ��
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
	// �������ڴ������ʼ�����㷨
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(size));

	//�ʼ������CentralCacheҪ̫��,������Ҫ����ƽ��,�𽥴�1����512
	if (_freeLists[index].MaxSize() == batchNum)
		_freeLists[index].MaxSize() += 1;

	void* start = nullptr;
	void* end = nullptr;

	//��һ����������batchNum��,Span�ڲ���batchNum��,ʣ���پͷ��ض���
	size_t actualNum =  CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
	assert(actualNum > 0);
	assert(start);

	//��ȡ����ڴ��ʱ, ����ʹ�õ���һ��, ������Ĺҵ���ӦͰ��
	if (actualNum > 1)
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);

	return start;
}

// �����ڴ����
void* ThreadCache::Allocate(size_t size)
{
	assert(size <= MAX_BYTES);
	size_t alignSize = SizeClass::RoundUp(size);	// ��ȡ�������ڴ��С
	size_t index = SizeClass::Index(size);	// ��ȡ����Ͱ��λ��

	if (!_freeLists[index].Empty())
	{
		// ��ǰͰ���л��յĿ����ڴ�
		return _freeLists[index].Pop();
	}
	else
	{
		// �����Ļ����ȡ����
		return FetchFromCentralCache(index, alignSize);
	}
}

// �ͷ��ڴ����
void ThreadCache::Deallocate(void* ptr, size_t size)
{
	assert(size <= MAX_BYTES);
	assert(ptr);

	// �ҵ���Ӧӳ�����������Ͱ, ���������
	size_t index = SizeClass::Index(size);
	_freeLists[index].Push(ptr);

	// �������ȴ���һ������������ڴ�ʱ,��жһ��list����CentralCache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
		ListTooLong(index);
}

// �ͷŶ���ʱ���������ʱ������һ���ڴ�ص�CentralCache
void ThreadCache::ListTooLong(size_t index)
{
	// ��Ҫ���յ�һ���ڴ�����Ͱ�в���
	void* start = _freeLists[index].PopRange(_freeLists[index].MaxSize());

	// ֪ͨCentralCache�����⴮�ڴ����
	CentralCache::GetInstance()->ReleaseListToSpans(start, index);
}