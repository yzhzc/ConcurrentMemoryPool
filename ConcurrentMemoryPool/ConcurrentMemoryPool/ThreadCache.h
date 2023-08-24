#pragma once

#include"Common.h"

class ThreadCache
{
public:
	// �����ڴ����
	void* Allocate(size_t size);
	// �ͷ��ڴ����
	void Deallocate(void* ptr, size_t size);

	// �����Ļ����ȡ����
	void* FetchFromCentralCache(size_t index, size_t size);

	// �ͷŶ���ʱ���������ʱ�������ڴ�ص����Ļ���
	void ListTooLong(size_t index);


private:
	FreeList _freeLists[NFREELIST]; //ÿ���̶߳���208��Ͱ
};

//TLS �̱߳��ش洢, ʹÿ���̶߳����Լ���ThreadCache
static _declspec(thread) ThreadCache* pTLSThreadCache = nullptr;