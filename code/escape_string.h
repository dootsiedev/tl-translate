#pragma once

#include <string>
#include <string_view>

// a string conversion so '\n' turns into "\\\n"
std::string escape_string(std::string_view input_string)
{
	std::string str;
	if(input_string.empty())
	{
		return str;
	}

	int escape_count = 0;
	for(char c: input_string)
	{
		switch(c)
		{
		case '\n':
		case '\t':
		case '\"':
		case '\\':
			escape_count++;
			break;
		}
	}
	// I could use __cpp_lib_string_resize_and_overwrite
	str.reserve(input_string.size() + escape_count);

	for(char c: input_string)
	{
		switch(c)
		{
		case '\n':
			str.push_back('\\');
			str.push_back('n');
			break;
		case '\t':
			str.push_back('\\');
			str.push_back('t');
			break;
		case '\"':
			str.push_back('\\');
			str.push_back('\"');
			break;
		case '\\':
			str.push_back('\\');
			str.push_back('\\');
			break;
		default:
			str.push_back(c);
		}
	}
	//ASSERT(str.size() == input_string.size() + escape_count);

	return str;
}