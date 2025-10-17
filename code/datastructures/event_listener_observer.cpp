// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "event_listener_observer.h"
#include "../core/stacktrace.h"

#ifndef HAS_DEBUG_FUNCTION_INFO
#include <sstream>
#endif

bool event_observer::destroy()
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
#ifdef HAS_DEBUG_FUNCTION_INFO
		debug_stacktrace_string_printer handler(function_detail);
		if(!debug_write_function_info(handler, reinterpret_cast<void*>(event.cb)))
		{
			success = false;
		}
#else
		// you can technically use addr2line or llvm_symbolizer to decode if you had the symbols
		std::ostringstream oss;
		oss << "0x" << event.cb << '\n';
		function_detail = oss.str();
#endif
		serrf("event_observer::%s: leaked callback\n%s", __func__, function_detail.c_str());

		// this will make remove() assert
		// this is also a hack to stop the destructor from printing the error twice...
		event.head = nullptr;
	}

	// I am tempted to clear the array,
	// but I shouldn't because it would be use-after-free when a leaked listener tries to remove(),
	// which asan would detect, but the listener should assert because head == null.

	return success;
}

// this was older code this WAS based on, but I changed it.
// This is here because I think it would be fun to compare the performance (if it mattered...).

// this Swap and Pop code is annoying because it cant unlink itself without the observer,
// The freelist version (being used above) uses 8 for the node, 32 for the observer.
// Swap and Pop (below) uses 16 for the node and 8 for the observer
//
// I think using a function pointer pop and swap would be better
// so the listener node would hold a pointer to the observer entry (replaces the index),
// and the observer entry would hold a **pointer to the listener's pointer.
// then the listeners destructor can just set the observer's pointer to NULL to flag for deletion.
// And the trigger function should be able to pop and swap the value for you.
// I could also make the listener movable with a move overload.
//
// I am not using it because ATM I use this event for an error condition that almost never happens.
// (opengl context loss, I only use it to switch to the debug context... / hacky stuff)
// And I frequently added and removed listeners to the observer.
// so the memory is essentially "leaking" in that situation (trigger does garbage collection)
// (but if the observer was global, pop&swap in destructor wouldn't be an issue,
// 	and the opengl context loss IS global, so I should be using it...)
//
// I could also try to use delegates:
// https://www.codeproject.com/Articles/1170503/The-Impossibly-Fast-Cplusplus-Delegates-Fixed
// I don't think these delegates would be faster or slower than my function pointer code,
// but one of my issues with using function pointers is that I can easily cast to the wrong type.
// EX: static void c_func(void* ud) { static_cast<WRONG_copy_pasted*>(ud)->func();}
// (EDIT: I switched to decltype(this), so it's pretty safe, I want to use a macro like:
// #define SET_CALLBACK(func) [](void* user_data) {static_cast<decltype(this)>(user_data)->func();}
// But I don't because once parameters are added, it's not simple anymore)
// I ASSUME that CppDelegates includes type checking, if it doesn't, it's useless.
// but in my opinion the API is "uglier" (the static cast is not pretty, but it's simple)
// Also if I had OCD about stacktraces, function pointers look prettier.
// (but I use lamdas instead of static functions which adds 2 cryptic frames in msvc...)
#if 0
#define INIT_EVENT_LISTENER(listener_name, function_name, type) \
	class listener_name                                         \
	{                                                           \
	public:                                                     \
		typedef type MACRO_LISTENER_PARAM_TYPE;                 \
		int listener_name##_MACRO_LISTENER_INDEX = -1;          \
		virtual void function_name(type) = 0;                   \
		virtual ~listener_name() = default;                     \
	}

// observer name, listener event name, and function name
// note that the order of listerners triggered is random.
// you cannot remove a listener while in the same callback.
// functions:
//-trigger(param) calls all listeners with the parameter passed in
//-add_listener(pointer to listener)
//-remove_listener(pointer to listener)
//-clear() removes all listeners.
#define INIT_EVENT_OBSERVER(observer_name, listener_name, function_name)               \
	class observer_name                                                                \
	{                                                                                  \
		std::vector<listener_name*> calls;                                             \
                                                                                       \
	public:                                                                            \
		void trigger(listener_name::MACRO_LISTENER_PARAM_TYPE param) const             \
		{                                                                              \
			for(listener_name* event : calls)                                          \
				event->function_name(param);                                           \
		}                                                                              \
		void add_listener(listener_name* listener)                                     \
		{                                                                              \
			ASSERT(                                                                    \
				listener->listener_name##_MACRO_LISTENER_INDEX == -1 && #observer_name \
				"::add_listener listener must be inactive");                           \
			listener->listener_name##_MACRO_LISTENER_INDEX = calls.size();             \
			calls.push_back(listener);                                                 \
		}                                                                              \
		void remove_listener(listener_name* listener)                                  \
		{                                                                              \
			ASSERT(                                                                    \
				listener->listener_name##_MACRO_LISTENER_INDEX != -1 && #observer_name \
				"::remove_listener listener must be active");                          \
			calls.back()->listener_name##_MACRO_LISTENER_INDEX =                       \
				listener->listener_name##_MACRO_LISTENER_INDEX;                        \
			calls.at(listener->listener_name##_MACRO_LISTENER_INDEX) = calls.back();   \
			listener->listener_name##_MACRO_LISTENER_INDEX = -1;                       \
			calls.pop_back();                                                          \
		}                                                                              \
		void clear()                                                                   \
		{                                                                              \
			calls.clear();                                                             \
		}                                                                              \
	}

#endif