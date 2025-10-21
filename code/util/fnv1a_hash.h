#pragma once
#ifdef TL_USE_64bit_HASH
// from: https://gist.github.com/ruby0x1/81308642d0325fd386237cfa3b44785c
// FNV1a c++11 constexpr compile time hash functions, 32 and 64 bit
// str should be a null terminated string literal, value should be left out
// e.g hash_32_fnv1a_const("example")
// code license: public domain or equivalent
// post: https://notes.underscorediscovery.com/constexpr-fnv1a/

constexpr uint64_t val_64_const = 0xcbf29ce484222325;
constexpr uint64_t prime_64_const = 0x100000001b3;
#ifdef __clang__
__attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
constexpr std::uint64_t hash_fnv1a_const2(const char* str, std::uint64_t value = val_64_const) noexcept
{
	for(; *str != '\0'; ++str)
	{
		value = (value ^ static_cast<std::uint64_t>(static_cast<uint8_t>(*str))) * prime_64_const;
	}
	return value;
}
typedef uint64_t fnv1a_hash_type;
#else
constexpr uint32_t val_32_const = 0x811c9dc5;
constexpr uint32_t prime_32_const = 0x1000193;
#ifdef __clang__
__attribute__((no_sanitize("unsigned-integer-overflow")))
#endif
constexpr std::uint32_t hash_fnv1a_const2(const char* str, std::uint32_t value = val_32_const) noexcept
{
	for(; *str != '\0'; ++str)
	{
		value = (value ^ static_cast<std::uint32_t>(static_cast<uint8_t>(*str))) * prime_32_const;
	}
	return value;
}
typedef uint32_t fnv1a_hash_type;
#endif