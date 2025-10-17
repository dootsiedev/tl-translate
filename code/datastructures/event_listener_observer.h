#pragma once

#include "../core/global.h"
#include <deque>

// a simple oneshot event.
// this isn't very flexible, and this should be renamed.
// since this event is only good for events that are almost never called (errors)

typedef void (*event_listener_callback)(void* ud);

struct INTERNAL_event_listener_node
{
	INTERNAL_event_listener_node** head = nullptr;
	INTERNAL_event_listener_node* next = nullptr;
	event_listener_callback cb = nullptr;
	void* ud = nullptr;

	// emplace back ugliness
	INTERNAL_event_listener_node(INTERNAL_event_listener_node** head_, event_listener_callback cb_, void* ud_)
	: head(head_)
	, cb(cb_)
	, ud(ud_)
	{
	}
};

struct event_listener_node : nocopy
{
	INTERNAL_event_listener_node* node = nullptr;
	bool is_active() const
	{
		if(node != nullptr) {
			ASSERT(node->next == nullptr);
			ASSERT(node->head != nullptr && "observer is deleted");
		}
		return (node != nullptr);
	}
	// this removes the callback safely if you are inside the trigger callback.
	void remove()
	{
		if(node != nullptr)
		{
			ASSERT(node->next == nullptr);
			ASSERT(node->head != nullptr && "listener deleted after observer");
			if(node->head == nullptr)
			{
				node = NULL;
				return;
			}
			node->next = *node->head;
			*node->head = node;
			node->head = NULL;
			node = nullptr;
		}
	}
	~event_listener_node()
	{
		remove();
	}
};

// this has a list of callbacks that are unordered.
struct event_observer
{
	std::deque<INTERNAL_event_listener_node> calls;

	INTERNAL_event_listener_node* head = nullptr;

	void trigger()
	{
		for(auto it = calls.begin(); it != calls.end(); ++it)
		{
			if(it->head == nullptr)
			{
				continue;
			}
			ASSERT(it->head == &head && "mismatching head");
			it->cb(it->ud);
		}
	}

	void add_listener(event_listener_node& node, event_listener_callback cb, void* ud)
	{
		// it's already in a list.
		if(node.is_active())
		{
			if(node.node->head == &head)
			{
				return;
			}
			node.remove();
		}
		// use the freelist
		if(head != nullptr)
		{
			ASSERT(head->head == nullptr);
			// pop list
			node.node = head;
			head = head->next;
			// set values.
			node.node->head = &head;
			node.node->next = nullptr;
			node.node->cb = cb;
			node.node->ud = ud;
			return;
		}
		node.node = &calls.emplace_back(&head, cb, ud);
	}


	// checks if the entries are deleted.
	NDSERR bool destroy();

	~event_observer()
	{
		bool ret = destroy();
		ASSERT(ret);
	}
};

#if 0
#if __cpp_rtti
__has_feature(cxx_rtti) // clang
__GXX_RTTI // GCC
#include <typeinfo>
#endif

// instead of a function pointer, use a virtual class.
// useful if you needed a vtable.
template<class T>
struct virt_event_observer
{
	struct INTERNAL_virt_event_listener_node
	{
		INTERNAL_virt_event_listener_node** head = nullptr;
		INTERNAL_virt_event_listener_node* next = nullptr;
		T* object = nullptr;

		// emplace back ugliness
		INTERNAL_virt_event_listener_node(INTERNAL_virt_event_listener_node** head_, T* object_)
		: head(head_)
		, object(object_)
		{
		}
	};

	struct event_listener_node : nocopy
	{
		INTERNAL_virt_event_listener_node* node = nullptr;
		bool is_active() const
		{
			if(node != nullptr) {
				ASSERT(node->next == nullptr);
				ASSERT(node->head != nullptr && "observer is deleted");
			}
			return (node != nullptr);
		}
		// this removes the callback safely if you are inside the trigger callback.
		void remove()
		{
			if(node != nullptr)
			{
				ASSERT(node->next == nullptr);
				ASSERT(node->head != nullptr && "listener deleted after observer");
				if(node->head == nullptr)
				{
					node = NULL;
					return;
				}
				node->next = *node->head;
				*node->head = node;
				node->head = NULL;
				node = nullptr;
			}
		}
		~event_listener_node()
		{
			remove();
		}
	};
	std::deque<INTERNAL_virt_event_listener_node> calls;

	INTERNAL_virt_event_listener_node* head = nullptr;

	template<class FN>
	void trigger(FN cb)
	{
		for(auto it = calls.begin(); it != calls.end(); it++)
		{
			if(it->head == nullptr)
			{
				continue;
			}
			ASSERT(it->head == &head && "mismatching head");
			cb(it->object);
		}
	}

	void add_listener(event_listener_node& node, T* object)
	{
		// it's already in a list.
		if(node.is_active())
		{
			if(node.node->head == &head)
			{
				return;
			}
			node.remove();
		}
		// use the freelist
		if(head != nullptr)
		{
			ASSERT(head->head == nullptr);
			// pop list
			node.node = head;
			head = head->next;
			// set values.
			node.node->head = &head;
			node.node->next = nullptr;
			node.node->object = object;
			return;
		}
		node.node = &calls.emplace_back(&head, object);
	}


	// checks if the entries are deleted.
	NDSERR bool destroy()
	{
		bool success = true;

		for(auto& event : calls)
		{
			if(event.head == nullptr)
			{
				continue;
			}
			success = false;
			std::string function_detail;

			#if __cpp_rtti
			function_detail = typeid(event.object).name();
			#else
			// or str_asprintf
			function_detail = std::to_string(event.object);
			#endif
			serrf("virt_event_observer::%s: leaked callback\n%s\n", __func__, function_detail.c_str());

			// this will make remove() assert
			// this is also a hack to stop the destructor from printing the error twice...
			event.head = nullptr;
		}

		// I am tempted to clear the array,
		// but I shouldn't because it would be use-after-free when a leaked listener tries to remove(),
		// which asan would detect, but the listener should assert because head == null.

		return success;
	}

	~virt_event_observer()
	{
		bool ret = destroy();
		ASSERT(ret);
	}
};
#endif