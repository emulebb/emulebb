#pragma once

#include <regex>
#include <string>

namespace RegexMatchSeams
{
enum class MatchMode
{
	Full,
	Search
};

template <typename TChar>
bool IsValidPattern(const std::basic_string<TChar> &rPattern, const std::regex_constants::syntax_option_type eFlags = std::regex_constants::ECMAScript)
{
	try {
		const std::basic_regex<TChar> rePattern(rPattern, eFlags);
		(void)rePattern;
		return true;
	} catch (const std::regex_error&) {
		return false;
	}
}

template <typename TChar>
bool Match(const std::basic_string<TChar> &rText, const std::basic_string<TChar> &rPattern, const MatchMode eMode, const std::regex_constants::syntax_option_type eFlags = std::regex_constants::ECMAScript)
{
	if (rText.empty() || rPattern.empty())
		return false;
	try {
		const std::basic_regex<TChar> rePattern(rPattern, eFlags);
		return eMode == MatchMode::Full
			? std::regex_match(rText, rePattern)
			: std::regex_search(rText, rePattern);
	} catch (const std::regex_error&) {
		return false;
	}
}
}

#define EMULEBB_TEST_HAVE_REGEX_MATCH_SEAMS 1
