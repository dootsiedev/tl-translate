#pragma once

#ifndef TL_PERFECT_HASH_TRANSLATION

// simple gettext style translation done during runtime.
// I am not using gettext, but I could if I really wanted to.
// no plural forms, no domain, but that's not essential for my needs.
// I should add domain if I had a lot of strings that are hidden behind a macro,
// since it would cause unresolved translation warnings in the extractor, 
// but I want to avoid having multiple translation files.
const char* translate_gettext(const char* text);


#define _T(x) translate_gettext(x)
#else // TL_PERFECT_HASH_TRANSLATION


// using constexpr hash functions.
// more like imperfect hashing, because if any strings have a collision, you will need to deal with it.
// I don't think this would be that much faster than gettext, but if it was 10x faster with 1000 strings & no collisions & fast build time, maybe I would use it.
// this will not work with indirect strings (BUT the AST string extractor also wont work with indirect strings either).

typedef uint32_t translate_hash_type;

// I assume this works with non-constexpr strings?
constexper translate_hash_type operator"" _hash ()

#define _T(x) translate_hash(x, x##_hash)
#endif // TL_PERFECT_HASH_TRANSLATION