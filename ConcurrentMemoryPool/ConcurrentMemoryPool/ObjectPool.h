#pragma once

#include"Common.h"


template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;

		// ���Ȱѻ��յĿռ��ظ�����
		if (_freeList)
		{
			// objָ���ͷ��һ��ռ�
			obj = reinterpret_cast<T*>(_freeList);

			// _freeListָ����һ��ռ��ַ
			_freeList = NextObj(_freeList);
		}
		else
		{
			// ʣ���ڴ治��Ҫ��������Сʱ,���¿����ռ�
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 16 * 8 * 1024; //128kb
				_memory = reinterpret_cast<char*>(SystemAlloc(_remainBytes >> 13));
				if (_memory == nullptr)
					throw std::bad_alloc();
			}

			obj = reinterpret_cast<T*>(_memory);

			// ���ٸ�һ��ָ���С�Ŀռ�,������չ�����
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// ��λnew, ��ʾ����T���캯����ʼ��
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		// ��ʾ������������
		obj->~T();
		NextObj(obj) = _freeList;

		//ͷ��
		_freeList = obj;
	}
private:
	char* _memory = nullptr;	// ָ�����ڴ��ָ��
	size_t _remainBytes = 0;	// ����ڴ�ʣ���ֽ���

	void* _freeList = nullptr;	// ���յ������ڴ�����ͷָ��
};