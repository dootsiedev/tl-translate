// This is an independent project of an individual developer. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++, C#, and Java: https://pvs-studio.com
#include "utf8_stuff.h"

const char* utf8cpp_get_error(utf8::internal::utf_error err_code)
{
#define UTF8_ERROR(x) \
	case(utf8::internal::x): return #x;
	switch(err_code)
	{
		UTF8_ERROR(UTF8_OK)
		UTF8_ERROR(NOT_ENOUGH_ROOM)
		UTF8_ERROR(INVALID_LEAD)
		UTF8_ERROR(INCOMPLETE_SEQUENCE)
		UTF8_ERROR(OVERLONG_SEQUENCE)
		UTF8_ERROR(INVALID_CODE_POINT)
	}
#undef UTF8_ERROR
	return "UNKNOWN_UTF8_ERROR";
}

// copied from utf8cpp
bool utf8cpp_append_string(std::string& str, char32_t cp)
{
	if(!utf8::internal::is_code_point_valid(cp))
	{
		return false;
	}
	if(cp < 0x80) // one octet
		str += static_cast<char>(cp);
	else if(cp < 0x800)
	{ // two octets
		str += static_cast<char>((cp >> 6) | 0xc0);
		str += static_cast<char>((cp & 0x3f) | 0x80);
	}
	else if(cp < 0x10000)
	{ // three octets
		str += static_cast<char>((cp >> 12) | 0xe0);
		str += static_cast<char>(((cp >> 6) & 0x3f) | 0x80);
		str += static_cast<char>((cp & 0x3f) | 0x80);
	}
	else
	{ // four octets
		str += static_cast<char>((cp >> 18) | 0xf0);
		str += static_cast<char>(((cp >> 12) & 0x3f) | 0x80);
		str += static_cast<char>(((cp >> 6) & 0x3f) | 0x80);
		str += static_cast<char>((cp & 0x3f) | 0x80);
	}
	return true;
}