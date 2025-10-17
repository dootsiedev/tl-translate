#pragma once

#include "../global.h"

// basically a copy of doom 3's idLinkList
// (AHH GPL NOOO!) I could use EA's EASTL intrusive_list if I needed
// the reason I don't use EA's list is because it's not as dumb, I like this because it's dumb.
// (it's easy to reimplement idLinkList, but the API is open to optimizations, which is extra work)
template<class T>
struct intrusive_linklist : nocopy // would be nice if I supported move?
{
	T* data = NULL;

	intrusive_linklist* head = NULL;
	intrusive_linklist* next = NULL;
	intrusive_linklist* prev = NULL;

	// the root
	intrusive_linklist()
	: head(this)
	, next(this)
	, prev(this)
	{
	}

	~intrusive_linklist()
	{
		Remove();
	}

	// an entry
	explicit intrusive_linklist(T* data_)
	: data(data_)
	, head(this)
	, next(this)
	, prev(this)
	{
	}

	bool Empty() const {
		return head->next == head;
	}

	void Remove()
	{
		prev->next = next;
		next->prev = prev;

		next = this;
		prev = this;
		head = this;
	}

	void InsertBefore(intrusive_linklist& node)
	{
		Remove();

		next = &node;
		prev = node.prev;
		node.prev = this;
		prev->next = this;
		head = node.head;
	}

	void InsertAfter(intrusive_linklist& node)
	{
		Remove();

		prev = &node;
		next = node.next;
		node.next = this;
		next->prev = this;
		head = node.head;
	}

	void AddToEnd(intrusive_linklist& node)
	{
		InsertBefore(*node.head);
	}

	void AddToFront(intrusive_linklist& node)
	{
		InsertAfter(*node.head);
	}

	intrusive_linklist* ListHead() const
	{
		return head;
	}

	intrusive_linklist* NextNode() const
	{
		if(next == head)
		{
			return NULL;
		}

		return next;
	}

	intrusive_linklist* PrevNode() const
	{
		if(prev == head)
		{
			return NULL;
		}
		return prev;
	}
};