TL_START(Japanese, JP, "日本語", "2025", "(no git hash)")
// comments work!
TL_INFO("func","main.cpp", 0,0)
TL_TEXT("test!\n", "てすと！\n")

// TL_NO_MATCH means that the merge operation failed to find a matching string
//TL_NO_MATCH("2025", "(no git hash)")
TL_INFO("func","main.cpp", 0,0)
TL_FORMAT("test %s!\n", "てすと %s！\n")

// TL_MAYBE is a comment from the merge operation for lack of confidence in the match.
// So that you could undo the match if desired.
//TL_MAYBE("original string!", "2025", "(no git hash)", 0.7535 (the AI probability score))
TL_INFO("func","main.cpp", 0,0)
TL_FORMAT("test\nnewline: %s!\n", "てすと\nにゅうらいおん: %s！\n")


// TL_ERROR is when a merge passes the AI fuzzy test, but an error is preventing it.
//TL_ERROR(TL_FORMAT_SPECIFIER_ERROR, "original string!", "original translation!", "2025", "(no git hash)", 0.9604 (the AI probability score))
//TL_TEXT("string!", NULL)

TL_END()