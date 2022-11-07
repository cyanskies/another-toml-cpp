// MIT License
//
// Copyright(c) 2022 Steven Pilkington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Deprecated but not marked for removal
// This is the easiest way for us to convert unicode escape codes
// We dont use the functionallity that was considered a vulnerability AFAIK
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "another_toml_string_util.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <codecvt>
#include <iostream>
#include <locale>
#include <optional>
#include <regex>

#include "another_toml.hpp"

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

	static std::optional<date> fill_date(const reg_matches& matches) noexcept
	{
		const auto error_msg = "Error parsing date"s;

		auto out = date{};
		assert(matches[static_cast<std::size_t>(match_index::date)].matched);
		const auto& years = matches[static_cast<std::size_t>(match_index::year)];
		assert(years.matched);
		auto ret = std::from_chars(&*years.first, &*years.second, out.year);
		if (ret.ptr != &*years.second)
		{
			std::cerr << error_msg;
			return {};
		}

		const auto& months = matches[static_cast<std::size_t>(match_index::month)];
		assert(months.matched);
		ret = std::from_chars(&*months.first, &*months.second, out.month);
		if (ret.ptr != &*months.second)
		{
			std::cerr << error_msg;
			return {};
		}

		if (out.month == 0 || out.month > 12)
		{
			std::cerr << "Month out of range\n"s;
			return {};
		}

		const auto& days = matches[static_cast<std::size_t>(match_index::day)];
		assert(days.matched);
		const auto day_end = &*days.first + days.length();
		ret = std::from_chars(&*days.first, day_end, out.day);
		if (ret.ptr != day_end)
		{
			std::cerr << error_msg;
			return {};
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
			std::cerr << "Day out of range\n"s;
			return {};
		}

		return out;
	}

	static std::optional<time> fill_time(const reg_matches& matches) noexcept
	{
		const auto error_parsing_time_msg = "Error parsing time"s;

		auto out = time{};
		assert(matches[static_cast<std::size_t>(match_index::time)].matched);
		const auto& hours = matches[static_cast<std::size_t>(match_index::hours)];
		assert(hours.matched);
		auto ret = std::from_chars(&*hours.first, &*hours.second, out.hours);
		if (ret.ptr != &*hours.second)
		{
			std::cerr << error_parsing_time_msg;
			return {};
		}

		if (out.hours > 23)
		{
			std::cerr << "Hours out of range\n"s;
			return {};
		}

		const auto& minutes = matches[static_cast<std::size_t>(match_index::minues)];
		assert(minutes.matched);
		ret = std::from_chars(&*minutes.first, &*minutes.second, out.minutes);
		if (ret.ptr != &*minutes.second)
		{
			std::cerr << error_parsing_time_msg;
			return{};
		}

		if (out.minutes > 59)
		{
			std::cerr << "Minutes out of range\n"s;
			return {};
		}

		const auto& seconds = matches[static_cast<std::size_t>(match_index::seconds)];
		assert(seconds.matched);
		const auto seconds_end = &*seconds.first + seconds.length();
		ret = std::from_chars(&*seconds.first, seconds_end, out.seconds);
		if (ret.ptr != seconds_end)
		{
			std::cerr << error_parsing_time_msg;
			return{};
		}

		if (out.seconds > 60)
		{
			std::cerr << "Seconds out of range\n"s;
			return {};
		}

		const auto& seconds_frac = matches[static_cast<std::size_t>(match_index::seconds_frac)];
		if (seconds_frac.matched)
		{
			const auto frac_end = &*seconds_frac.first + seconds_frac.length();
			ret = std::from_chars(&*seconds_frac.first, frac_end, out.seconds_frac);
			if (ret.ptr != frac_end)
			{
				std::cerr << error_parsing_time_msg;
				return{};
			}
		}

		return out;
	}

	static std::optional<local_date_time> fill_date_time(const reg_matches& matches) noexcept
	{
		const auto date = fill_date(matches);
		const auto time = fill_time(matches);

		if (!date || !time)
			return {};

		return local_date_time{ *date, *time };
	}

	std::variant<std::monostate, date, time, date_time, local_date_time> parse_date_time(std::string_view str) noexcept
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
				const auto dt = fill_date_time(matches);
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
					else if (*off_sign.first != '-')
						return {};

					const auto& hours = matches[static_cast<std::size_t>(match_index::off_hours)];
					assert(hours.matched);
					auto ret = std::from_chars(&*hours.first, &*hours.second, odt.offset_hours);
					if (ret.ptr == &*hours.second)
					{
						const auto& minutes = matches[static_cast<std::size_t>(match_index::off_minues)];
						assert(minutes.matched);
						ret = std::from_chars(&*minutes.first, &*minutes.first + minutes.length(), odt.offset_minutes);
						if (ret.ptr == &*minutes.first + minutes.length())
							return odt;
					}
				}
			}
			else if (local_date_time)
			{
				auto dt = fill_date_time(matches);
				if (dt)
					return *dt;
			}
			else if (local_date)
			{
				auto d = fill_date(matches);
				if (d)
					return *d;
			}
			else if (local_time)
			{
				auto t = fill_time(matches);
				if (t)
					return *t;
			}
		}

		return {};
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

	template<bool NoThrow, bool EscapeAllUnicode>
	static std::optional<std::string> to_escaped_string(std::string_view unicode)
	{
		auto cvt = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{ {}, unicode32_bad_conversion };
		const auto u32 = cvt.from_bytes(data(unicode), data(unicode) + size(unicode));
		if (u32 == unicode32_bad_conversion)
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Invalid utf-8 string"s;
				return {};
			}
			else
				throw parser_error{ "Invalid utf-8 string"s };
		}

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
				escape = ch > 0x7F; // non-ascii unicode

			if (escape)
			{
				const auto integral = static_cast<uint32_t>(ch);
				auto chars = std::array<char, 8>{};
				const auto ret = std::to_chars(chars.data(), chars.data() + size(chars), integral, 16);
				assert(ret.ec == std::errc{});
				const auto dist = ret.ptr - chars.data();
				auto pad_limit = 8;
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
				const auto u8 = cvt.to_bytes(ch);
				out += u8;
			}
			else
				out.push_back(static_cast<char>(ch));
		}

		return out;
	}

	std::string to_escaped_string(std::string_view str)
	{
		return *to_escaped_string<false, false>(str);
	}

	std::string to_escaped_string2(std::string_view str)
	{
		return *to_escaped_string<false, true>(str);
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

	//constexpr auto escape_codes_raw = std::array{
	//	"\\b"sv, "\\t"sv, "\\n"sv, "\\f"sv, "\\r"sv, "\\\""sv, "\\\\"sv // "\\uXXXX" "\\UXXXXXXXX"
	//};

	//constexpr auto escape_codes_char = std::array{
	//	'\b', '\t', '\n', '\f', '\r', '\"', '\\' // "\\uXXXX" "\\UXXXXXXXX"
	//};

	// replace string chars with proper escape codes
	template<bool NoThrow>
	std::optional<std::string> replace_escape_chars(std::string_view str)
	{
		/*static_assert(size(escape_codes_raw) == size(escape_codes_char));
		constexpr auto codes_size = size(escape_codes_raw);
		*/auto s = std::string{ str };
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
			auto found_code = false;

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
			case U'\\':
				s.replace(code_beg, 2, 1, '\\');
				pos = code_beg + 1;
				continue;
			case 't':
				s.replace(code_beg, 2, 1, '\t');
				pos = code_beg + 1;
				continue;
			}

			// Unicode chars are 1-4 bytes, the escape code is at least 6 chars
			// so the resulting string will always be shorter, string won't have
			// to alloc.
			if (const auto escape_char = s[code_mid];
				escape_char == 'u' || escape_char == 'U')
			{
				if (escape_char == 'u') // \uHHHH
					code_end = code_mid + 5;
				else if (escape_char == 'U') // \UHHHHHHHH
					code_end = code_mid + 9;
				// TOML 1.1
				//else if (escape_char == 'x') // \xHH
				//	code_end = code_mid + 3;

				const auto escape_size = code_end - code_beg;

				if (size(s) < code_end)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Invalid unicode escape code: "s << std::string_view{ &s[code_beg], escape_size } << '\n';
						return {};
					}
					else
						throw unicode_error{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], escape_size } };
				}

				auto int_val = std::uint_least32_t{};
				const auto result = std::from_chars(&s[code_mid + 1], &s[code_end], int_val, 16);
				static_assert(sizeof(std::uint_least32_t) <= sizeof(char32_t));
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
						throw unicode_error{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], escape_size } };
				}

				const auto u8 = to_u8_str<NoThrow>(unicode_char);
				if constexpr (NoThrow)
				{
					if (!u8)
					{
						std::cerr << "Failed to convert unicode escape code to utf-8: "s << std::string_view{ &s[code_beg], escape_size } << '\n';
						return {};
					}
				}

				assert(u8);
				s.replace(code_beg, escape_size, *u8);
				pos = code_beg + size(*u8);
				continue;
			}

			if constexpr (NoThrow)
			{
				std::cerr << "Illigal escape code in quoted string: \"\\"s << s[code_mid] << "\"\n"s;
				return {};
			}
			else
				throw unicode_error{ "Illigal escape code in quoted string: \"\\"s + s[code_mid] + "\""s };

			++pos;
		}

		return s;
	}

	// instantiate with true for another_toml.cpp to use
	template std::optional<std::string> replace_escape_chars<true>(std::string_view);

	std::string to_unescaped_string(std::string_view str)
	{
		return *replace_escape_chars<false>(str);
	}

	bool contains_unicode(std::string_view s) noexcept
	{
		return std::any_of(begin(s), end(s), is_unicode_byte);
	}

	bool valid_unicode_string(std::string_view s) noexcept
	{
		const auto end = std::end(s);
		for (auto beg = begin(s); beg != end; ++beg)
		{
			if (is_unicode_start(*beg))
			{
				auto u8 = std::string{ *beg };
				while (++beg != end && is_unicode_continuation(*beg))
					u8.push_back(*beg);

				const auto ch = unicode_u8_to_u32(u8);
				if (!valid_u32_code_point(ch))
					return false;
			}
			else if (is_unicode_continuation(*beg)) // Misplaced continuation byte
				return false;
			else if (!valid_u32_code_point(static_cast<char32_t>(*beg))) // Ascii
				return false;
		}

		return true;
	}

	char32_t unicode_u8_to_u32(std::string_view str) noexcept
	{
		if (empty(str))
			return {};

		const auto end = std::end(str);
		auto beg = begin(str);

		if (!is_unicode_start(*beg))
			return unicode_error_char;

		int bytes = 1;
		if (*beg & 0b01000000)
			++bytes;
		if (*beg & 0b00100000)
			++bytes;
		if (*beg & 0b00010000)
			++bytes;

		assert(bytes >= 1);
		auto out = char32_t{};
		switch (bytes)
		{
		case 2:
			out = *beg & 0b00011111;
			break;
		case 3:
			out = *beg & 0b00001111;
			break;
		case 4:
			out = *beg & 0b00000111;
			break;
		}

		while (bytes-- > 1)
		{
			++beg;
			if (beg == end || !is_unicode_continuation(*beg))
				return unicode_error_char;
			out = out << 6;
			out |= *beg & 0b00111111;
		}

		if (valid_u32_code_point(out))
			return out;

		return unicode_error_char;
	}

	template<bool NoThrow>
	std::optional<std::string> to_u8_str(char32_t ch)
	{
		auto cvt = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{ unicode_bad_conversion };
		const auto u8 = cvt.to_bytes(ch);
		if (u8 == unicode_bad_conversion)
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Invalid utf-8 char"s;
				return {};
			}
			else
				throw parser_error{ "Invalid utf-8 chars"s };
		}
		return u8;
	}

	// instantiate with true for another_toml.cpp to use
	template std::optional<std::string> to_u8_str<true>(char32_t);

	std::string unicode_u32_to_u8(char32_t ch)
	{
		return *to_u8_str<false>(ch);
	}
}
