#ifndef ANOTHER_TOML_STRING_UTIL_HPP
#define ANOTHER_TOML_STRING_UTIL_HPP

#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "another_toml.hpp"

// These functions assume chars, strings and string_views are encoded in utf-8
// char32_t is used to hold utf-32 characters

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
		writer::float_rep representation = writer::float_rep::normal;

		enum class error_t : std::uint8_t
		{
			none,
			bad,
			out_of_range
		};

		// if != error_t::none, then an error occured
		error_t error = error_t::none;
	};

	// parses floating point value strings
	parse_float_string_return parse_float_string(std::string_view str);

	// Throws unicode_error if str is not a valid UTF-8 string
	// to_escaped_string only escapes control characters
	std::string to_escaped_string(std::string_view str);
	// to_escaped_string2 also escapes all unicode characters
	// return value is a valid ascii string
	std::string to_escaped_string2(std::string_view str);
	// converts all escaped characters to the characters they represent
	// throws unicode_error on bad escape codes
	std::string to_unescaped_string(std::string_view str);

	// Escapes and adds quotations around str so that it can be used
	// as a valid toml name(keys, tables)
	// set ascii_output to output in only ascii characters, unicode will be escaped
	std::string escape_toml_name(std::string_view str, bool ascii_ouput = false);

	// returns true if any character was part of a unicode sequence
	bool contains_unicode(std::string_view s) noexcept;
	// returns true if 's' is a valid UTF-8 string
	bool valid_unicode_string(std::string_view s) noexcept;

	constexpr auto unicode_error_char = char32_t{ 0xD7FF17 };
	// Converts the first character in str into a UTF-32 char
	// Returns unicode_error_char on failure
	char32_t unicode_u8_to_u32(std::string_view str) noexcept;
	// Converts ch to a utf-8 encoded string representing char
	std::string unicode_u32_to_u8(char32_t ch);

	// Tests if the char is part of a unicode sequence
	constexpr bool is_unicode_byte(char) noexcept;
	// Tests if the char is the start of a unicode sequence
	constexpr bool is_unicode_start(char) noexcept;
	// Tests if the char is part of a sequence, but wasn't the start
	constexpr bool is_unicode_continuation(char) noexcept;

	constexpr bool valid_u32_char(char32_t val) noexcept;
}

#include "string_util.inl"

#endif // !ANOTHER_TOML_STRING_UTIL_HPP
