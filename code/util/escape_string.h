#pragma once

#include "../core/global.h"

#include <string>
#include <string_view>
#include <cstring>

// a string conversion so '\n' turns into "\\\n"
inline std::string escape_string(std::string_view input_string)
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

inline bool rem_escape_string(char* input_string)
{
	ASSERT(input_string != NULL);

	int i = 0;
	int j = 0;
	while(input_string[i] != '\0')
	{
		if(input_string[i] == '\\')
		{
			++i;
			switch(input_string[i])
			{
			case 'n': input_string[j] = '\n'; break;
			case 't': input_string[j] = '\t'; break;
			case '\"': input_string[j] = '\"'; break;
			case '\\': input_string[j] = '\\'; break;
			case '\0':
				serr("expected escape code, got null\n");
				return false;
			default:
				if(isalnum(input_string[i]) != 0)
				{
					serrf("expected escape code, got %c\n", input_string[i]);
					return false;
				}
				serrf("expected escape code, got #%d\n", input_string[i]);
				return false;
			}
		}
		if(j < i)
		{
			input_string[j] = input_string[i];
		}
		++j;
		++i;
	}
	input_string[j] = '\0';
	return true;
}