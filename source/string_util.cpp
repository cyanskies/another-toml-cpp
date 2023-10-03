// MIT License
//
// Copyright (c) 2022 Steven Pilkington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "another_toml/string_util.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <iostream>
#include <locale>
#include <optional>
#include <regex>
#include <sstream>

#include "uni_algo/conv.h"
#include "uni_algo/break_grapheme.h"
#include "uni_algo/norm.h"
#include "uni_algo/ranges.h"

#include "another_toml/another_toml.hpp"

#include "another_toml/except.hpp"

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace another_toml
{
	namespace
	{
		enum class match_index
		{
			date = 1,
			year,
			month,
			day,
			date_time_seperator,
			time,
			hours,
			minues,
			seconds,
			seconds_frac,
			offset,
			off_z,
			off_unused,
			off_sign,
			off_hours,
			off_minues
		};
	}
	using reg_matches = std::match_results<std::string_view::iterator>;

	template<bool NoThrow>
	static std::optional<date> fill_date(const reg_matches& matches) noexcept(NoThrow)
	{
		auto out = date{};
		assert(matches[static_cast<std::size_t>(match_index::date)].matched);
		const auto& years = matches[static_cast<std::size_t>(match_index::year)];
		assert(years.matched);
		auto ret = std::from_chars(&*years.first, &*years.second, out.year);
		
		constexpr auto years_range = "Year value out of range.\n";

		if (ret.ptr == &*years.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << years_range;
				return {};
			}
			else throw parsing_error{ years_range };
		}
		
		if (ret.ptr != &*years.second)
		{
			constexpr auto year_error = "Error parsing year.\n";
			if constexpr (NoThrow)
			{
				std::cerr << year_error;
				return {};
			}
			else throw parsing_error{ year_error };
		}

		const auto& months = matches[static_cast<std::size_t>(match_index::month)];
		assert(months.matched);
		ret = std::from_chars(&*months.first, &*months.second, out.month);
		
		constexpr auto month_range = "Month value out of range.\n";

		if (ret.ptr == &*months.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << month_range;
				return {};
			}
			else throw parsing_error{ month_range };
		}

		if (ret.ptr != &*months.second)
		{
			constexpr auto month_error = "Error parsing month.\n";
			if constexpr (NoThrow)
			{
				std::cerr << month_error;
				return {};
			}
			else throw parsing_error{ month_error };
		}

		if (out.month == 0 || out.month > 12)
		{
			if constexpr (NoThrow)
			{
				std::cerr << month_range;
				return {};
			}
			else throw parsing_error{ month_range };
		}

		const auto& days = matches[static_cast<std::size_t>(match_index::day)];
		assert(days.matched);
		const auto day_end = &*days.first + days.length();
		ret = std::from_chars(&*days.first, day_end, out.day);
		
		constexpr auto day_range = "Day value out of range.\n";

		if (ret.ptr == &*days.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << day_range;
				return {};
			}
			else throw parsing_error{ day_range };
		}

		if (ret.ptr != day_end)
		{
			constexpr auto day_error = "Error parsing day value.\n";
			if constexpr (NoThrow)
			{
				std::cerr << day_error;
				return {};
			}
			else throw parsing_error{ day_error };
		}

		auto max_days = 31;
		switch (out.month)
		{
		case 2:
			max_days = 28;
			break;
		case 4:
			[[fallthrough]];
		case 6:
			[[fallthrough]];
		case 9:
			[[fallthrough]];
		case 11:
			max_days = 30;
		}

		if (out.day == 0 || out.day > max_days)
		{
			if constexpr (NoThrow)
			{
				std::cerr << day_range;
				return {};
			}
			else throw parsing_error{ day_range };
		}

		return out;
	}

	template<bool NoThrow>
	static std::optional<time> fill_time(const reg_matches& matches) noexcept(NoThrow)
	{
		auto out = time{};
		assert(matches[static_cast<std::size_t>(match_index::time)].matched);
		const auto& hours = matches[static_cast<std::size_t>(match_index::hours)];
		assert(hours.matched);
		auto ret = std::from_chars(&*hours.first, &*hours.second, out.hours);
		
		constexpr auto hour_error = "Hours value out of range.\n";
		if (ret.ptr == &*hours.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << hour_error;
				return {};
			}
			else throw parsing_error{ hour_error };
		}
		else if (ret.ptr != &*hours.second)
		{
			constexpr auto hours_error2 = "Error while parsing hours.\n";
			if constexpr (NoThrow)
			{
				std::cerr << hours_error2;
				return {};
			}
			else throw parsing_error{ hours_error2 };
		}

		if (out.hours > 23)
		{
			if constexpr (NoThrow)
			{
				std::cerr << hour_error;
				return {};
			}
			else throw parsing_error{ hour_error };
		}

		const auto& minutes = matches[static_cast<std::size_t>(match_index::minues)];
		assert(minutes.matched);
		ret = std::from_chars(&*minutes.first, &*minutes.second, out.minutes);
		constexpr auto minutes_range = "Minutes value out of range.\n";
		
		if (ret.ptr == &*minutes.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << minutes_range;
				return {};
			}
			else throw parsing_error{ minutes_range };
		}
		
		if (ret.ptr != &*minutes.second)
		{
			constexpr auto minutes_error2 = "Error parsing minutes.\n";
			if constexpr (NoThrow)
			{
				std::cerr << minutes_error2;
				return{};
			}
			else throw parsing_error{ minutes_error2 };
		}

		if (out.minutes > 59)
		{
			if constexpr (NoThrow)
			{
				std::cerr << minutes_range;
				return {};
			}
			else throw parsing_error{ minutes_range };
		}

		const auto& seconds = matches[static_cast<std::size_t>(match_index::seconds)];
		assert(seconds.matched);
		const auto seconds_end = &*seconds.first + seconds.length();
		ret = std::from_chars(&*seconds.first, seconds_end, out.seconds);
		
		constexpr auto seconds_error = "Seconds value out of range.\n";

		if (ret.ptr == &*seconds.first && ret.ec == std::errc::result_out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << seconds_error;
				return {};
			}
			else throw parsing_error{ seconds_error };
		}
		
		if (ret.ptr != seconds_end)
		{
			constexpr auto seconds_error2 = "Error parsing seconds.\n";
			if constexpr (NoThrow)
			{
				std::cerr << seconds_error2;
				return{};
			}
			else throw parsing_error{ seconds_error2 };
		}

		if (out.seconds > 60)
		{
			if constexpr (NoThrow)
			{
				std::cerr << seconds_error;
				return {};
			}
			else throw parsing_error{ seconds_error };
		}

		const auto& seconds_frac = matches[static_cast<std::size_t>(match_index::seconds_frac)];
		if (seconds_frac.matched)
		{
			const auto frac_end = &*seconds_frac.first + seconds_frac.length();
			ret = std::from_chars(&*seconds_frac.first, frac_end, out.seconds_frac);
			constexpr auto seconds_frac_range = "Seconds fractional component out of range.\n";
			
			if (ret.ptr == &*seconds_frac.first && ret.ec == std::errc::result_out_of_range)
			{
				if constexpr (NoThrow)
				{
					std::cerr << seconds_frac_range;
					return {};
				}
				else throw parsing_error{ seconds_frac_range };
			}
			
			if (ret.ptr != frac_end)
			{
				constexpr auto seconds_frac_error = "Error parsing seconds fractional component.\n";
				if constexpr (NoThrow)
				{
					std::cerr << seconds_frac_error;
					return{};
				}
				else throw parsing_error{ seconds_frac_error };
			}
		}

		return out;
	}

	template<bool NoThrow>
	static std::optional<local_date_time> fill_date_time(const reg_matches& matches) noexcept(NoThrow)
	{
		const auto date = fill_date<NoThrow>(matches);
		const auto time = fill_time<NoThrow>(matches);

		if (!date || !time)
			return {};

		return local_date_time{ *date, *time };
	}

	template<bool NoThrow>
	std::variant<std::monostate, date, time, date_time, local_date_time> parse_date_time_ex(std::string_view str) noexcept(NoThrow)
	{
		constexpr auto date_time_reg =
			R"(^((\d{4})-(\d{2})-(\d{2}))?([Tt ])?((\d{2}):(\d{2}):(\d{2})(\.\d+)?)?(([zZ])|(([\+\-])(\d{2}):(\d{2})))?)";
		if (auto matches = std::match_results<std::string_view::iterator>{};
			std::regex_match(begin(str), end(str), matches, std::regex{ date_time_reg }))
		{
			//date
			const auto& date = matches[static_cast<std::size_t>(match_index::date)];
			const auto& d_t_seperator = matches[static_cast<std::size_t>(match_index::date_time_seperator)];
			const auto& time = matches[static_cast<std::size_t>(match_index::time)];
			const auto& offset = matches[static_cast<std::size_t>(match_index::offset)];

			const auto offset_date_time = date.matched &&
				d_t_seperator.matched &&
				time.matched &&
				offset.matched;

			const auto local_date_time = date.matched &&
				d_t_seperator.matched &&
				time.matched;

			const auto local_date = date.matched &&
				!d_t_seperator.matched &&
				!time.matched &&
				!offset.matched;

			const auto local_time = !date.matched &&
				!d_t_seperator.matched &&
				time.matched &&
				!offset.matched;

			if (offset_date_time)
			{
				const auto dt = fill_date_time<NoThrow>(matches);
				if (dt)
				{
					auto odt = date_time{ *dt, false };
					const auto& off_z = matches[static_cast<std::size_t>(match_index::off_z)];
					if (off_z.matched)
					{
						// default offset
						odt.offset_hours = {};
						odt.offset_minutes = {};
						odt.offset_positive = true;
						return odt;
					}

					const auto& off_sign = matches[static_cast<std::size_t>(match_index::off_sign)];
					assert(off_sign.matched);
					if (*off_sign.first == '+')
						odt.offset_positive = true;

					const auto& hours = matches[static_cast<std::size_t>(match_index::off_hours)];
					assert(hours.matched);
					auto ret = std::from_chars(&*hours.first, &*hours.second, odt.offset_hours);
					
					constexpr auto hours_range = "Offset hours out of range.\n";

					if (ret.ptr == &*hours.first && ret.ec == std::errc::result_out_of_range)
					{
						if constexpr (NoThrow)
						{
							std::cerr << hours_range;
							return {};
						}
						else throw parsing_error{ hours_range };
					}

					if (ret.ptr == &*hours.second)
					{
						if (odt.offset_hours > 23)
						{
							if constexpr (NoThrow)
							{
								std::cerr << hours_range;
								return {};
							}
							else throw parsing_error{ hours_range };
						}

						const auto& minutes = matches[static_cast<std::size_t>(match_index::off_minues)];
						assert(minutes.matched);
						ret = std::from_chars(&*minutes.first, &*minutes.first + minutes.length(), odt.offset_minutes);
						
						constexpr auto minutes_range = "Offset minutes out of range.\n";
						
						if (ret.ptr == &*minutes.first && ret.ec == std::errc::result_out_of_range)
						{
							if constexpr (NoThrow)
							{
								std::cerr << minutes_range;
								return {};
							}
							else throw parsing_error{ minutes_range };
						}

						if (ret.ptr == &*minutes.first + minutes.length())
						{
							if (minutes < 60)
								return odt;
							else
							{
								if constexpr (NoThrow)
								{
									std::cerr << minutes_range;
									return {};
								}
								else throw parsing_error{ minutes_range };
							}
						}
					}
				}
			}
			else if (local_date_time)
			{
				auto dt = fill_date_time<NoThrow>(matches);
				if (dt)
					return *dt;
			}
			else if (local_date)
			{
				auto d = fill_date<NoThrow>(matches);
				if (d)
					return *d;
			}
			else if (local_time)
			{
				auto t = fill_time<NoThrow>(matches);
				if (t)
					return *t;
			}
		}

		if constexpr (NoThrow)
		{
			return {};
		}
		else 
			throw parsing_error{ "Error parsing value.\n"s };
	}

	template std::variant<std::monostate, date, time, date_time, local_date_time> parse_date_time_ex<false>(std::string_view str);

	std::variant<std::monostate, date, time, date_time, local_date_time> parse_date_time(std::string_view str) noexcept
	{
		return parse_date_time_ex<true>(str);
	}

	// removes underscores and leading positive signs from sv
	std::string remove_underscores(std::string_view sv)
	{
		auto str = std::string{ sv };
		for (auto iter = begin(str); iter != std::end(str); ++iter)
		{
			if (*iter == '_')
				iter = str.erase(iter);
		}
		if (!empty(str) && str.front() == '+')
			str.erase(begin(str));
		return str;
	}

	parse_float_string_return parse_float_string(std::string_view str)
	{
		using error_t = parse_float_string_return::error_t;

		//	inf, +inf, -inf
		if (str == "inf"sv || str == "+inf"sv)
			return parse_float_string_return{ std::numeric_limits<double>::infinity() };
		if (str == "-inf"sv)
			return parse_float_string_return{ -std::numeric_limits<double>::infinity() };
		//	nan, +nan, -nan
		if (str == "nan"sv || str == "+nan"sv || str == "-nan"sv)
			return parse_float_string_return{ std::numeric_limits<double>::quiet_NaN() };

		constexpr auto float_reg = R"(^[\+\-]?([1-9]+(_?([\d])+)*|0)(\.[\d]+(_?[\d])*)?([eE][\+\-]?[\d]+(_?[\d]+)?)?$)";
		if (auto matches = std::match_results<std::string_view::iterator>{};
			std::regex_match(begin(str), end(str), matches, std::regex{float_reg}))
		{
			const auto string = remove_underscores(str);
			auto floating_val = double{};
			auto rep = float_rep::default;
			const auto string_end = &string[0] + size(string);
			const auto ret = std::from_chars(&string[0], string_end, floating_val);
			if (ret.ptr == string_end)
			{
				// The sub match that contains the scientific notation portion
				// of a float
				constexpr auto scientific_e = 6;
				if (matches[scientific_e].matched)
					rep = float_rep::scientific;
				return parse_float_string_return{ floating_val, rep };
			}
			else if (ret.ec == std::errc::result_out_of_range)
				return parse_float_string_return{ {}, {}, error_t::out_of_range };
		}

		return parse_float_string_return{ {}, {}, error_t::bad };
	}

	namespace
	{
		const auto unicode32_bad_conversion = U"conversion_error"s; 
		const auto unicode_bad_conversion = "conversion_error"s;
	}

	template<bool NoThrow, bool EscapeAllUnicode, bool EscapeNewline>
	static std::optional<std::string> to_escaped_string(std::string_view unicode)
	{
		if (!uni::is_valid_utf8(unicode))
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Invalid utf-8 string"s;
				return {};
			}
			else
				throw toml_error{ "Invalid utf-8 string"s };
		}

		const auto u32 = uni::utf8to32u(unicode);
		auto out = std::string{};
		const auto end = std::end(u32);
		for (auto ch : u32)
		{
			switch (ch)
			{
			case U'\b':
				out += "\\b"s;
				continue;
			case U'\n':
				out += "\\n"s;
				continue;
			case U'\f':
				out += "\\f"s;
				continue;
			case U'\r':
				out += "\\r"s;
				continue;
			case U'\"':
				out += "\\\""s;
				continue;
			case U'\\':
				out += "\\\\"s;
				continue;
			case U'\t':
				out += "\\t"s;
				continue;
			case U'\u007F': // DEL 127
				// TOML 1.1
				// out += "\\x7F"s;
				out += "\\u007F"s;
				continue;
			}

			bool escape = ch < 0x20; // control chars

			if constexpr (EscapeAllUnicode)
				escape = escape || ch > 0x7F; // non-ascii unicode

			if (escape)
			{
				const auto integral = static_cast<uint32_t>(ch);
				auto chars = std::array<char, 8>{};
				const auto ret = std::to_chars(chars.data(), chars.data() + size(chars), integral, 16);
				assert(ret.ec == std::errc{});
				const auto dist = ret.ptr - chars.data();
				auto pad_limit = 8;
				// TODO: TOML 1.1
				// output \xHH unicode escapes
				if (dist > 3)
					out += "\\U"s;
				else
				{
					out += "\\u"s;
					pad_limit = 4;
				}

				const auto pad = pad_limit - dist;
				for (auto i = int{}; i < pad; ++i)
					out.push_back('0');

				for (auto i = int{}; i < dist; ++i)
					out.push_back(chars[i]);
			}
			else if (ch > 0x7F) // check for unicode chars
			{
				const auto u8 = uni::utf32to8(std::u32string_view{ &ch, 1 });
				out += u8;
			}
			else
				out.push_back(static_cast<char>(ch));
		}

		return out;
	}

	std::string to_escaped_string(std::string_view str)
	{
		return *to_escaped_string<false, false, true>(str);
	}

	std::string to_escaped_string2(std::string_view str)
	{
		return *to_escaped_string<false, true, true>(str);
	}

	std::string escape_toml_name(std::string_view s, bool ascii)
	{
		if (empty(s))
			return "\"\""s;

		auto out = ascii ?
			to_escaped_string2(s) : 
			to_escaped_string(s);
		if (out != s ||
			contains_unicode(s) ||
			out.find(' ') != std::string::npos ||
			out.find('.') != std::string::npos ||
			out.find('#') != std::string::npos)
			out = '\"' + out + '\"';

		return out;
	}

	// defined in another_toml.cpp
	std::string_view block_control(std::string_view s) noexcept;

	// replace string chars with proper escape codes
	template<bool NoThrow, bool Pairs = false>
	std::optional<std::string> replace_escape_chars(std::string_view str)
	{
		auto s = std::string{ str };
		auto pos = std::size_t{};
		while (pos < size(s))
		{
			const auto code_beg = s.find("\\", pos);
			if (code_beg == std::string::npos)
				break; //we're done

			const auto code_mid = code_beg + 1;
			if (code_mid >= size(s))
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Invalid escape code: unmatched '\'\n"s;
					return {};
				}
				else
					throw unicode_error{ "Invalid escape code: unmatched '\'\n"s };
			}

			auto code_end = code_mid + 1;
			
			switch (s[code_mid])
			{
			case 'b':
				s.replace(code_beg, 2, 1, '\b');
				pos = code_beg + 1;
				continue;
			case 'n':
				s.replace(code_beg, 2, 1, '\n');
				pos = code_beg + 1;
				continue;
			case 'f':
				s.replace(code_beg, 2, 1, '\f');
				pos = code_beg + 1;
				continue;
			case 'r':
				s.replace(code_beg, 2, 1, '\r');
				pos = code_beg + 1;
				continue;
			case '\"':
				s.replace(code_beg, 2, 1, '\"');
				pos = code_beg + 1;
				continue;
			case '\\':
				s.replace(code_beg, 2, 1, '\\');
				pos = code_beg + 1;
				continue;
			case 't':
				s.replace(code_beg, 2, 1, '\t');
				pos = code_beg + 1;
				continue;
			case 'u': // unicode \uHHHH
				code_end = code_mid + 5;
				break;
			case 'U': // unicode \UHHHHHHHH
				code_end = code_mid + 9;
				break;
			// TODO: TOML 1.1
			//case 'x': // unicode \xHH
			//	code_end = code_mid + 3;
			//	break;
			default:
			{
				const auto write_error = [str, code_mid](std::ostream& o) {
					const auto string = std::string_view{ &str[code_mid] };
					auto graph_rng = uni::ranges::grapheme::utf8_view{ string };
					o << "Illigal escape code in quoted string: \"\\"s <<
						block_control(*begin(graph_rng)) <<
						"\".\n"s;
					};

				if constexpr (NoThrow)
				{
					write_error(std::cerr);
					return {};
				}
				else
				{
					auto string = std::ostringstream{};
					write_error(string);
					throw unicode_error{ string.str() };
				}
			}
			}

			// Unicode chars are 1-4 bytes, the escape code is at least 6 chars
			// so the resulting string will always be shorter, string won't have
			// to alloc.
			const auto escape_size = code_end - code_beg;

			if (size(s) < code_end)
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Invalid unicode escape code: "s << std::string_view{ &s[code_beg], escape_size } << '\n';
					return {};
				}
				else
					throw unicode_error{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], escape_size} };
			}

			auto int_val = std::uint_least32_t{};
			const auto result = std::from_chars(&s[code_mid + 1], &s[code_end], int_val, 16);
			static_assert(sizeof(std::uint_least32_t) <= sizeof(char32_t));
				
			// parse surragate pairs
			if constexpr (Pairs)
			{
				if (int_val >= 0xD800 && int_val <= 0xDFFF)
				{
					auto str = std::u16string{ { static_cast<char16_t>(int_val), {} } };
					if (code_end + 5 < size(s) && 
						s[code_end] == '\\' &&
						s[code_end+1] == 'u')
					{
						//second part of JSON surrogate pair
						std::from_chars(&s[code_end + 2], &s[code_end + 6], int_val, 16);
						str[1] = static_cast<char16_t>(int_val);
						if (uni::is_valid_utf16(str))
						{
							const auto u8 = uni::utf16to8(str);
							s.replace(code_beg, escape_size * 2, u8);
							pos = code_beg + size(u8);
							continue;
						}
					}
				}
			}

			const auto unicode_char = static_cast<char32_t>(int_val);
			if (result.ptr != &s[code_end] ||
				result.ec == std::errc::result_out_of_range ||
				!valid_u32_code_point(unicode_char))
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Invalid unicode escape code: "s << std::string_view{ &s[code_beg], escape_size } << '\n';
					return {};
				}
				else
					throw unicode_error{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], escape_size} };
			}

			const auto u8 = unicode_u32_to_u8(unicode_char);
			s.replace(code_beg, escape_size, u8);
			pos = code_beg + size(u8);
		}

		return s;
	}

	// instantiate with true for another_toml.cpp to use
	template std::optional<std::string> replace_escape_chars<true>(std::string_view);

	std::string to_unescaped_string(std::string_view str)
	{
		return *replace_escape_chars<false>(str);
	}

	// same as above, except it also matches surrogate pair escape codes, 
	// such as those used by JSON
	// This is an internal function only used for testing
	std::string to_unescaped_string2(std::string_view str)
	{
		return *replace_escape_chars<false, true>(str);
	}

	std::string to_escaped_multiline(std::string_view str)
	{
		return *to_escaped_string<false, false, false>(str);
	}

	std::string to_escaped_multiline2(std::string_view str)
	{
		return *to_escaped_string<false, true, false>(str);
	}

	// based on uni_algo/examples/cpp_ranges.h
	bool unicode_string_equal(std::string_view lhs, std::string_view rhs)
	{
		// UTF-8 -> NFC for both strings
		auto view1 = uni::ranges::norm::nfc_view{ uni::ranges::utf8_view{ lhs } };
		auto view2 = uni::ranges::norm::nfc_view{ uni::ranges::utf8_view{ rhs } };

		auto it1 = view1.begin();
		auto it2 = view2.begin();

		// Compare the strings by code points
		for (; it1 != uni::sentinel && it2 != uni::sentinel; ++it1, ++it2)
		{
			if (*it1 != *it2)
				return false;
		}

		// Reached the end in both strings then the strings are equal
		if (it1 == uni::sentinel && it2 == uni::sentinel)
			return true;

		return false;
	}

	bool contains_unicode(std::string_view s) noexcept
	{
		return std::any_of(begin(s), end(s), is_unicode_byte);
	}

	std::string unicode_u32_to_u8(char32_t ch)
	{
		return unicode32_to_unicode8({ &ch, 1 });
	}

	std::string unicode32_to_unicode8(std::u32string_view unicode)
	{
		return uni::utf32to8(unicode);
	}

	std::u32string unicode8_to_unicode32(std::string_view unicode)
	{
		return uni::utf8to32u(unicode);
	}

	bool valid_u32_code_point(char32_t val) noexcept
	{
		return uni::is_valid_utf32(std::u32string_view{ &val, 1 });
	}
}
