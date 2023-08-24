#pragma once

#include"Common.h"


template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;

		// 优先把回收的空间重复利用
		if (_freeList)
		{
			// obj指向表头的一块空间
			obj = reinterpret_cast<T*>(_freeList);

			// _freeList指向下一块空间地址
			_freeList = NextObj(_freeList);
		}
		else
		{
			// 剩余内存不够要申请对象大小时,重新开大块空间
			if (_remainBytes < sizeof(T))
			{
				_remainBytes = 16 * 8 * 1024; //128kb
				_memory = reinterpret_cast<char*>(SystemAlloc(_remainBytes >> 13));
				if (_memory == nullptr)
					throw std::bad_alloc();
			}

			obj = reinterpret_cast<T*>(_memory);

			// 至少给一个指针大小的空间,方便回收挂链表
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			_memory += objSize;
			_remainBytes -= objSize;
		}

		// 定位new, 显示调用T构造函数初始化
		new(obj)T;

		return obj;
	}

	void Delete(T* obj)
	{
		// 显示调用析构函数
		obj->~T();
		NextObj(obj) = _freeList;

		//头插
		_freeList = obj;
	}
private:
	char* _memory = nullptr;	// 指向大块内存的指针
	size_t _remainBytes = 0;	// 大块内存剩余字节数

	void* _freeList = nullptr;	// 回收的自由内存链表头指针
};