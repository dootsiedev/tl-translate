# TL translate

a smaller version of gettext except without features like plural forms.

Translations are stored in a folder named /translations

You convert translated text using a function like _T("")

pass +cv_language JP to argv to set the language to japanese or use cvar.cfg with the same argument.

it has the neat ability to switch between compile time hardcoded translations or using a parser to load a translations file.
- this is set by TL_COMPILE_TIME_TRANSLATION

# TL_COMPILE_TIME_TRANSLATION = ON (on by default)
- useful because you don't need to worry about finding the path to the translations, and it does not require dependencies.
- the cmake build directory does not need a copy or path to the translations folder
- if the number of strings are small, you can use TL_COMPILE_TIME_ASSERTS to manually sync the translations using compile time asserts (see tl-extract-strings)

# TL_COMPILE_TIME_TRANSLATION = OFF
- runtime translations are useful because you don't need to recompile the binary to change strings.
- sadly, boost parser adds a significant binary size increase and adds a couple seconds to build
- avoid using runtime translation on a debug build (debug+san is 30mb+, reldeb is ~10mb, only for .pdb symbols)
- At the moment you need to copy the boost parser repo directly into the source tree in a folder named "parser" (TODO: use either FetchContent or vcpkg)

# tl-extract-strings
- (TODO: INSERT LINK TO PROJECT HERE)
- LLVM AST parser + maybe use compile_commands.json
- you should use it as a prebuilt binary
- (TODO:) QT GUI for fuzzy matching, possibly using AI (it depends, I don't want to use python/js...)
- If you don't need AI or fuzzy matching or automatically update the languages, see: TL_COMPILE_TIME_ASSERTS.

one downside with tl-extract-strings is that strings hidden behind macros are not extracted.
- one workaround is to manually pass macro definitions and supported combinations (parsing ast multiple times)
- or I add a domain to the translation, so replace _T("") with _TD("",enum) so that I can ignore the missing text.
For now, I will just avoid hiding translations in macros for as long I can until it becomes an issue.

the second downside is that compiling the string extractor needs LLVM, which is a huge library...
(vcpkg has llvm, not sure how usable it will be...)

it should be noted that the language depends on a generic cvar variable (cvar.cfg or argv)
which means that any parsing errors before loading the translation will not be translated.
- This is not a big issue in my opinion, since the cvar values are english, so the errors should also be english.
- I could lazy load the translation on the first string found using the global variable LANG, but don't make any translations while parsing the cvar.cfg file, yet.
- (I am considering the ASSERT dialog, but it will be difficult to make sure _T("") won't call ASSERT... I will probably initialize the assert dialog strings externally...).