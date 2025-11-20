#include "../3rdParty/utf8cpp/core.hpp"

#include <string>

/*
for turning utf8 to codepoints without C++ exceptions, I use utf8cpp like this:
const char* str_cur = ...;
const char* str_end = ...;

std::u32string wstr;
// not accurate but faster than nothing.
wstr.reserve(text_len);

while(str_cur != str_end)
{
    uint32_t codepoint;
    utf8::internal::utf_error err_code =
        utf8::internal::validate_next(str_cur, str_end, codepoint);
    if(err_code != utf8::internal::UTF8_OK)
    {
        slogf("info: %s bad utf8: %s\n", __func__, utf8cpp_get_error(err_code));
        break;
    }
    wstr.push_back(codepoint);
}
if the string is truncated, you may want to ignore utf8::internal::NOT_ENOUGH_ROOM
*/

const char* utf8cpp_get_error(utf8::internal::utf_error err_code);
bool utf8cpp_append_string(std::string& str, char32_t cp);