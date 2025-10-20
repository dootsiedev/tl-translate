#pragma once

#include "global.h"
#include "RWops.h"

#include <cstring> //for strcmp

#include <map>
#include <string>

enum class CVAR_T
{
	// with the cvar init callback, it's unknown, should never be accessed.
	UNKNOWN,
	// modifying this cvar requires the app to be fully restarted
	STARTUP,
	// this value might be modified without fully restarting,
	// but things that were created using the value will not change.
	DEFERRED,
	// you can modify this during runtime and changes should take effect
	RUNTIME,
	// warn when this setting is attempted to be set
	DISABLED,
	// the variable was never meant to be written
	READONLY
	// TODO: I should add a value called GPU_CONTEXT (or something?),
	//  because many cvars for graphics are cached in GL state (shaders, buffers),
	//  but can be fixed using a context reset which I have implemented (maybe cvar triggers it?),
	//  But recreating the context is an overkill solution for modifying ONE buffer / shader...
	//  And I probably need a WINDOW_CREATION to modify the window state (SRGB/multisampling/DPI?)
};

enum CVAR_CACHE_FLAGS
{
	// NONE means that it will never commit, you must close and restart.
	CVAR_CACHE_NONE = 0,
	CVAR_CACHE_WINDOW = (1 << 0),
	CVAR_CACHE_GL = (1 << 1) | CVAR_CACHE_WINDOW,
	CVAR_CACHE_FONT = (1 << 3) | CVAR_CACHE_GL,
	CVAR_CACHE_UI = (1 << 4) | CVAR_CACHE_FONT,
	CVAR_CACHE_DEMO = (1 << 5),
	CVAR_CACHE_AUDIO = (1 << 6)
};

class V_cvar;

// single linked list of cvars that are going to be saved in the cvar file.
// use cvar.cvar_save_changes()
extern V_cvar* g_cvar_modified_list_head;

class V_cvar
{
public:
	const char* cvar_key = "unknown";
	const char* cvar_comment = "unknown";
	CVAR_T internal_cvar_type = CVAR_T::UNKNOWN; // use cvar_type()

	// I should remove this since I don't use it to print any errors,
	// it's not hard to find where the cvar is...
	const char* cvar_debug_file;
	int cvar_debug_line;

	// single linked list node to save into the file.
	V_cvar* modified_next = NULL;

	// for cvar_string
	bool cvar_save_quotes = false;

	// don't add the cvar to the modified list if it's already modified
	// NOTE: I should remove the linked list and manually find modified cvars...
	bool cvar_commit_changes = false;

	// this is just a sanity check to make sure you
	// aren't accessing data() in a global initializer
	bool cvar_init_once = false;

	// this will not be removed automatically if the value is set back to the default.
	// so options in the options menu won't be added to the file if it's set back to default,
	// but I like keeping the variables sorted, so removing variables already in the file is bad.
	bool cvar_from_file = false;

	// set internal_data() inside of set_data() for startup values.
	// done during inside the validation function.
	// this is left true so that startup gets the lock.
	bool cvar_validate_lock = true;

	V_cvar(const char* key, const char* comment, CVAR_T type, const char* file, int line)
	: cvar_key(key)
	, cvar_comment(comment)
	, internal_cvar_type(type)
	, cvar_debug_file(file)
	, cvar_debug_line(line)
	{
	}

	V_cvar(const char* key, const char* file, int line)
	: cvar_key(key)
	, cvar_debug_file(file)
	, cvar_debug_line(line)
	{
	}

	CVAR_T cvar_type()
	{
		if(cvar_get_blame() != nullptr)
		{
			return CVAR_T::DISABLED;
		}
		return internal_cvar_type;
	}

	void cvar_save_changes()
	{
		// if not already in the list.
		if(!cvar_commit_changes)
		{
			// add to the list of modified cvars to save.
			modified_next = g_cvar_modified_list_head;
			g_cvar_modified_list_head = this;
			cvar_commit_changes = true;
		}
	}

	// call the init callback in load_cvar
	virtual void cvar_init_values() = 0;
	virtual bool cvar_revert_to_default(SRC_LOC) = 0;
	virtual bool cvar_is_default() = 0;

	// set the value to the stored cached value.
	virtual void cvar_set_commit() = 0;

	// if this cvar is Disabled but cvar_internal_type != disabled (hardcoded),
	// you can get the cvar responsible for setting disabled.
	virtual V_cvar* cvar_get_blame() = 0;

	NDSERR virtual bool cvar_read(const char* buffer) = 0;
	virtual std::string cvar_write() = 0;

	virtual ~V_cvar() = default; // not used but needed to suppress warnings

	// no copy.
	V_cvar(const V_cvar&) = delete;
	V_cvar& operator=(const V_cvar&) = delete;
};

// const char* as the key because I feel like it.
struct cmp_str
{
	bool operator()(const char* a, const char* b) const
	{
		return strcmp(a, b) < 0;
	}
};

// I think a vector would be better, it should be optimized for autocomplete (if I get to it...)
std::map<const char*, V_cvar&, cmp_str>& get_convars();

// flags_req must be either CVAR_STARTUP,CVAR_GAME,CVAR_RUNTIME.
// returns the number of arguments parsed for one CVAR.
// returns -1 for error.
NDSERR int cvar_arg(CVAR_T flags_req, int argc, const char* const* argv, bool console = false);
// this will modify the string
NDSERR bool cvar_line(CVAR_T flags_req, char* line, bool console = false);
NDSERR bool cvar_file(CVAR_T flags_req, RWops* file);
NDSERR bool cvar_save_file(RWops* infile, RWops* outfile);
// debug shows the file and line number of the cvar, kind of useless...
void cvar_list(bool debug);

// this converts internal_data() to data(), and checks the blames.
// the first time you call this (after loading a file), values will be committed without validate,
// so that the blame() is correct.
NDSERR bool cvar_validate_conditions();

// to define an option for a single source file use this:
// static REGISTER_CVAR_INT(cv_name_of_option, 1, "this is an option", CVAR_NORMAL);
// you can use cv_name_of_option.data for reading and writing the value.
// to share a cvar in a header you can use:
// extern cvar_int cv_name_of_option;
// and then define REGISTER_CVAR_INT somewhere (without static).
#define REGISTER_CVAR_INT(key, value, comment, type) \
	cvar_int key(#key, value, comment, type, NULL, __FILE__, __LINE__)
#define REGISTER_CVAR_DOUBLE(key, value, comment, type) \
	cvar_double key(#key, value, comment, type, NULL, __FILE__, __LINE__)
#define REGISTER_CVAR_STRING(key, value, comment, type) \
	cvar_string key(#key, value, comment, type, NULL, __FILE__, __LINE__)

// conditional callback, usually setting up a dependency
// if the value depends on a compile time macro, just use the macro
// to set the CVAR_T to Disabled, this is for runtime conditions.
#define REGISTER_CVAR_INT_COND(key, value, comment, type, cond) \
	cvar_int key(#key, value, comment, type, cond, __FILE__, __LINE__)
#define REGISTER_CVAR_DOUBLE_COND(key, value, comment, type, cond) \
	cvar_double key(#key, value, comment, type, cond, __FILE__, __LINE__)
#define REGISTER_CVAR_STRING_COND(key, value, comment, type, cond) \
	cvar_string key(#key, value, comment, type, cond, __FILE__, __LINE__)

// TODO: why not do it like this?
#define REGISTER_CVAR(key, value, comment, type) \
	key(#key, value, comment, type, NULL, __FILE__, __LINE__)

// to avoid order violation, and to allow nested dependencies
// (if ASAN_OPTIONS:strict-init-order=1 gives errors)
// this could also help with changing the language (I will never do this for cvars...).
// this might help with avoiding the macro hell, but this will increase lines of code...

#define INIT_CVAR(key, func) key(#key, func, __FILE__, __LINE__)

#if 0
#define INIT_CVAR_FUNC(key, param) void __internal_init_cvar_ ## key(param)
#define INIT_CVAR(key, type) type key(#key, __internal_init_cvar_ ## key, __FILE__, __LINE__)
// this is called inside of data() instead of global init.
// OR I could make this set inside of load_cvar (if the cost is big),
// but then I need void* or dispatch to a virtual function...
static INIT_CVAR_FUNC(cv_test, cvar_int& value)
{
	// needs to be done to make sure the value is initialized.
	cv_other->init_cvar();

	// I want to put the comment into REGISTER_CVAR
	// because maybe I can reuse this function for multiple cvars.
	// but then I can't use an xmacro to concatinate enum -> switch -> comments dynamically.
	value.comment = "a test value, depends on cv_other";
	// optional, multiline, maybe this could be part of a GUI tooltip, and comment is shorter?
	value.long_comment = "...";

	value.internal_data = (cv_other.data() == 0) ? 0 : 1;
	value.internal_type = (cv_other.type() == CVAR_T::DISABLED) ? CVAR_T::RUNTIME : CVAR_T::DISABLED;

	// optional
	//value.cond = [](){};
	//value.min = 0; value.max = 1;
}
static INIT_CVAR(cv_test, cvar_int);
#endif

enum class CVAR_LOAD
{
	SUCCESS,
	ERROR,
	CLOSE
};

CVAR_LOAD load_cvar(int argc, char** argv);
bool save_cvar();

class cvar_int : public V_cvar
{
public:
	typedef void (*cvar_init_cb_type)(cvar_int& data);
	typedef V_cvar* (*cvar_cond_cb_type)(int value);

	// init_cb is called during load_cvar
	cvar_init_cb_type init_cb = nullptr;

	// an optional callback for checking if the cvar is disabled due to another cvar.
	cvar_cond_cb_type cond_cb = nullptr;

	int _internal_data = -1234567;
	int cvar_default_value = -1234567;

	// save this value on exit/reload because it's a startup value.
	// the internal data gets replaced with this on commit.
	int cvar_commit_data = -1234567;

	// you should use REGISTER_CVAR_ to fill in file and line.
	cvar_int(
		const char* key,
		int value,
		const char* comment,
		CVAR_T type,
		cvar_cond_cb_type cb,
		const char* file,
		int line);

	// INIT_CVAR
	cvar_int(const char* key, cvar_init_cb_type cb, const char* file, int line);

	int data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return _internal_data;
	}

	// if you modify a startup value,
	// the value wont change, but it will be saved.
	// this returns the changed value.
	int internal_data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return (internal_cvar_type == CVAR_T::STARTUP) ? cvar_commit_data : _internal_data;
	}

	// NOTE: this should be NDSERR, but I don't really care.
	// it's better to ignore the return than to use _internal_data or a billion if(){return false}
	bool set_data(int i SRC_LOC2);

	void cvar_init_values() override
	{
		if(cvar_init_once)
		{
			return;
		}
		cvar_init_once = true;
		if(init_cb != nullptr)
		{
			init_cb(*this);
		}
		cvar_default_value = _internal_data;
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			cvar_commit_data = _internal_data;
		}
	}
	bool cvar_revert_to_default(SRC_LOC) override
	{
		return set_data(cvar_default_value PASS_SRC_LOC2);
	}
	bool cvar_is_default() override
	{
		return _internal_data == cvar_default_value;
	}
	V_cvar* cvar_get_blame() override
	{
		return cond_cb != nullptr ? cond_cb(_internal_data) : nullptr;
	}
	void cvar_set_commit() override
	{
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			_internal_data = cvar_commit_data;
		}
	}

	NDSERR bool cvar_read(const char* buffer) override;
	std::string cvar_write() override;
};

class cvar_double : public V_cvar
{
public:
	typedef void (*cvar_init_cb_type)(cvar_double& data);
	typedef V_cvar* (*cvar_cond_cb_type)(double& value);

	// init_cb is called during load_cvar
	cvar_init_cb_type init_cb = nullptr;
	// an optional callback for checking if the cvar is disabled due to another cvar.
	cvar_cond_cb_type cond_cb = nullptr;
	double _internal_data = std::nan("");
	double cvar_default_value = std::nan("");

	// save this value on exit/reload because it's a startup value.
	// the internal data gets replaced with this on commit.
	double cvar_commit_data = std::nan("");

	// you should use REGISTER_CVAR_ to fill in file and line.
	cvar_double(
		const char* key,
		double value,
		const char* comment,
		CVAR_T type,
		cvar_cond_cb_type cb,
		const char* file,
		int line);

	// INIT_CVAR
	cvar_double(const char* key, cvar_init_cb_type cb, const char* file, int line);

	double data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return _internal_data;
	}

	// if you modify a startup value,
	// the value wont change, but it will be saved.
	// this returns the changed value.
	double internal_data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return (internal_cvar_type == CVAR_T::STARTUP) ? cvar_commit_data : _internal_data;
	}

	// to supress -Wdouble-promotion warnings.
	// I don't know if ubsan sanitizes overflow, but this is cleaner than static_cast<float>
	float data_float(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return _internal_data;
	}

	bool set_data(double i SRC_LOC2);

	void cvar_init_values() override
	{
		if(cvar_init_once)
		{
			return;
		}
		cvar_init_once = true;
		if(init_cb != nullptr)
		{
			init_cb(*this);
		}
		cvar_default_value = _internal_data;
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			cvar_commit_data = _internal_data;
		}
	}
	bool cvar_revert_to_default(SRC_LOC) override
	{
		return set_data(cvar_default_value PASS_SRC_LOC2);
	}
	bool cvar_is_default() override
	{
		return _internal_data == cvar_default_value;
	}
	V_cvar* cvar_get_blame() override
	{
		return cond_cb != nullptr ? cond_cb(_internal_data) : nullptr;
	}
	void cvar_set_commit() override
	{
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			_internal_data = cvar_commit_data;
		}
	}

	NDSERR bool cvar_read(const char* buffer) override;
	std::string cvar_write() override;
};

class cvar_string : public V_cvar
{
public:
	typedef void (*cvar_init_cb_type)(cvar_string& data);
	typedef V_cvar* (*cvar_cond_cb_type)(std::string& value);

	// init_cb is called during load_cvar
	cvar_init_cb_type init_cb = nullptr;
	// an optional callback for checking if the cvar is disabled due to another cvar.
	cvar_cond_cb_type cond_cb = nullptr;
	std::string _internal_data = "unknown";
	std::string cvar_default_value = "unknown";

	// save this value on exit/reload because it's a startup value.
	// the internal data gets replaced with this on commit.
	std::string cvar_commit_data = "unknown";

	// you should use REGISTER_CVAR_ to fill in file and line.
	cvar_string(
		const char* key,
		std::string value,
		const char* comment,
		CVAR_T type,
		cvar_cond_cb_type cb,
		const char* file,
		int line);

	// INIT_CVAR
	cvar_string(const char* key, cvar_init_cb_type cb, const char* file, int line);

	const std::string& data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return _internal_data;
	}
	const std::string* operator->() const
	{
		ASSERT_M(cvar_init_once && "use of cvar before init", cvar_key);
		return &_internal_data;
	}
	const std::string& operator*() const
	{
		ASSERT_M(cvar_init_once && "use of cvar before init", cvar_key);
		return _internal_data;
	}

	// if you modify a startup value,
	// the value wont change, but it will be saved.
	// this returns the changed value.
	const std::string& internal_data(SRC_LOC) const
	{
		ASSERT_M_SRC_LOC(cvar_init_once && "use of cvar before init", cvar_key);
		return (internal_cvar_type == CVAR_T::STARTUP) ? cvar_commit_data : _internal_data;
	}

	bool set_data(std::string&& str SRC_LOC2);

	// TODO: these 4 funcs are all copy pasted exactly the same... I could just use CRTP
	void cvar_init_values() override
	{
		if(cvar_init_once)
		{
			return;
		}
		cvar_init_once = true;
		if(init_cb != nullptr)
		{
			init_cb(*this);
		}
		cvar_default_value = _internal_data;
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			cvar_commit_data = _internal_data;
		}
	}
	bool cvar_revert_to_default(SRC_LOC) override
	{
		return set_data(std::string(cvar_default_value) PASS_SRC_LOC2);
	}
	bool cvar_is_default() override
	{
		return _internal_data == cvar_default_value;
	}
	V_cvar* cvar_get_blame() override
	{
		return cond_cb != nullptr ? cond_cb(_internal_data) : nullptr;
	}
	void cvar_set_commit() override
	{
		if(internal_cvar_type == CVAR_T::STARTUP)
		{
			_internal_data = cvar_commit_data;
		}
	}

	NDSERR bool cvar_read(const char* buffer) override;
	std::string cvar_write() override;
};


// crrtp hack, I could just make the callback a virtual function,
// but I want to try this out.
template<class T, class CB>
class cvar_callback : public T
{
public:
	using T::T;
	NDSERR bool cvar_read(const char* buffer) override
	{
		bool ret = T::cvar_read(buffer);
		ret = ret && static_cast<CB*>(this)->on_change();
		return ret;
	}
	NDSERR bool cvar_revert_to_default(SRC_LOC) override
	{
		bool ret = T::cvar_revert_to_default(PASS_SRC_LOC);
		ret = ret && static_cast<CB*>(this)->on_change();
		return ret;
	}
};