TL_START(Japanese, JP, "日本語", "2025", "(no git hash)")
// comments work!
INFO("main.cpp","just to test", 0)
TL("test!\n", "てすと！\n")

// unresolved means that the string extractor did not match with any pre newly added strings.
// the string extractor can be run with a flag that deletes all unresolved strings.
//UNRESOLVED("2025", "(no git hash)")
INFO("main.cpp","just to test", 1)
TL("test %s!\n", "てすと %s！\n")

// MAYBE means that this string did not match perfectly, but it passed the basic tests
// tests include: same string (PERFECT), same function... signature? (IMPERFECT), exact same line (PERFECT), same number of non escaped % arguments (UNRESOLVED), 
// only one string within the function is missing and one new string was found (requires more than 50% matching words).
// and probably some dumb fuzzy match (count the number of matching words or something, if over 25% of the letters weighted words are the same, keep it)
// or just use AI
// if it is correct, just delete it in the template.inl file 
// (and the next time you run the string extractor, it should erase the propogated MAYBE for all translation files)
//MAYBE("original string!", "2025", "(no git hash)")
INFO("main.cpp","/code/main.cpp", 2)
TL("test\nnewline:%s!\n", "てすと\nにゅうらいおん:%s！\n")

TL_END()