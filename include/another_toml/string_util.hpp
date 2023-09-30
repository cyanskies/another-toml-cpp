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

#ifndef ANOTHER_TOML_STRING_UTIL_HPP
#define ANOTHER_TOML_STRING_UTIL_HPP

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "another_toml/another_toml.hpp"

// These functions assume chars, strings and string_views are encoded in utf-8

namespace another_toml
{
	// Parses RFC 3339 formatted date
	// str must be a valid ascii string
	// returns monostate on failure
	std::variant<std::monostate, date, time, date_time, local_date_time>
		parse_date_time(std::string_view str) noexcept;

	struct parse_float_string_return
	{
		double value;
		float_rep representation = float_rep::default;

		enum class error_t : std::uint8_t
		{
			none,
			bad,
			out_of_range
		};

		// if != error_t::none, then an error occured
		error_t error = error_t::none;
	};

	// Parses floating point TOML value strings
	parse_float_string_return parse_float_string(std::string_view str);

	// Throws unicode_error if str is not a valid UTF-8 string
	// to_escaped_string only escapes control characters
	std::string to_escaped_string(std::string_view str);
	// to_escaped_string2 also escapes all unicode characters
	// return value is a valid ASCII string
	std::string to_escaped_string2(std::string_view str);
	// converts all escaped characters to the characters they represent
	// throws unicode_error on bad escape codes
	std::string to_unescaped_string(std::string_view str);

	// Convert strings to multiline TOML strings
	// Escapes all characters except newline and unicode
	std::string to_escaped_multiline(std::string_view str);
	// Escapes all characters except newline
	std::string to_escaped_multiline2(std::string_view str);

	// Escapes and adds quotations around str so that it can be used
	// as a valid toml name(keys, tables).
	// Set ascii_output to return an ASCII string; unicode chars will be escaped.
	std::string escape_toml_name(std::string_view str, bool ascii_ouput = false);

	bool unicode_string_equal(std::string_view lhs, std::string_view rhs);
	std::size_t unicode_count_graphemes(std::string_view);

	// returns true if string contains any unicode code units
	bool contains_unicode(std::string_view s) noexcept;
	
	constexpr auto unicode_error_char = char32_t{ 0x110000 };
	// Converts ch to a utf-8 encoded string representing char
	std::string unicode_u32_to_u8(char32_t ch);

	// Convert UTF-32 string to UTF-8
	// Throws unicode_error
	std::string unicode32_to_unicode8(std::u32string_view str);
	// Convert UTF-8 string to UTF-32
	// Throws unicode_error
	std::u32string unicode8_to_unicode32(std::string_view str);

	// Tests if the char is a unicode code unit
	constexpr bool is_unicode_byte(char) noexcept;
	// Tests if the char is the start of a unicode code point
	constexpr bool is_unicode_start(char) noexcept;
	// Tests if the char is part of a code point, but wasn't the start
	constexpr bool is_unicode_continuation(char) noexcept;

	// Returns true if `val` is a valid code point
	constexpr bool valid_u32_code_point(char32_t val) noexcept;
}

#include "another_toml/string_util.inl"

#endif // !ANOTHER_TOML_STRING_UTIL_HPP
