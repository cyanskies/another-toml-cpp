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
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANOTHER_TOML_WRITER_HPP
#define ANOTHER_TOML_WRITER_HPP

#include <memory>
#include <string_view>
#include <vector>

#include "another_toml/internal.hpp"
#include "another_toml/types.hpp"

namespace another_toml
{
	// Configurable options for controlling writer output
	struct writer_options
	{
		// How many characters before splitting next array element to new line.
		// Set to dont_split_lines to never split.
		std::int16_t max_line_length = 80;
		static constexpr auto dont_split_lines = std::numeric_limits<std::int16_t>::max();
		// If true, avoids unrequired whitespace eg: name = value -> name=value.
		bool compact_spacing = false;
		// Add an indentation level for each child table.
		bool indent_child_tables = true;
		// Added to the start of an indented line (may be repeated multiple times)
		// only has an effect if indent_child_tables = true.
		std::string indent_string = { '\t' };
		// Output only ascii characters (unicode sequences are escaped).
		bool ascii_output = false;
		// Skip writing redundant table headers.
		// eg. for [a.b.c], [a] and [a.b] only need to be written if they have keys in them.
		bool skip_empty_tables = true;
		// Date Time separator.
		enum class date_time_separator_t : std::uint8_t
		{
			big_t,
			whitespace
		};

		// Default to big_t.
		date_time_separator_t date_time_separator = {};

		// Ignore per value override specifiers where possible.
		// (eg. all ints output in base 10, floats in normal mode rather than scientific)
		bool simple_numerical_output = false;
		// Write a utf-8 BOM into the start of the stream.
		bool utf8_bom = false;
	};

	class writer
	{
	public:
		writer();

		// note their is an implicit root table
		// you cannot end_table to end it
		// you can write values and arrays into it, before adding
		// other tables

		// [tables]
		// use end table to control nesting
		void begin_table(std::string_view, table_def_type = table_def_type::header);
		void end_table() noexcept;

		// arrays:
		// name = [ elements ]
		// use write_value() to add elements
		// or begin_inline_table to add a table as an element
		void begin_array(std::string_view name);
		void end_array() noexcept;

		// begins an inline table
		// name will be ignored if being added as an array member
		void begin_inline_table(std::string_view name);
		void end_inline_table() noexcept;

		// begin an array of tables
		// [[array]]
		// keep calling begin_array_table with the same name
		// to add new tables to the array
		void begin_array_table(std::string_view);
		void end_array_table() noexcept;

		void write_key(std::string_view);

		// Write values on their own, for arrays

		// Strings are required to be in utf-8
		void write_value(std::string&& value);

		struct literal_string_t {};
		static constexpr auto literal_string_tag = literal_string_t{};

		// pass literal string tag to mark a string as being a literal
		void write_value(std::string&& value, literal_string_t);

		void write_value(std::string_view value);
		void write_value(std::string_view value, literal_string_t);

		// We need these to stop cstrings being converted to bool
		void write_value(const char* value)
		{
			write_value(std::string_view{ value });
			return;
		}

		void write_value(const char* value, literal_string_t l)
		{
			write_value(std::string_view{ value }, l);
			return;
		}

		void write_value(std::int64_t value, int_base = int_base::dec);

		template<typename Integral,
			std::enable_if_t<detail::is_integral_v<Integral>, int> = 0>
		void write_value(Integral i, int_base = int_base::dec);

		void write_value(double value, float_rep = float_rep::default, std::int8_t precision = auto_precision);
		void write_value(bool value);
		void write_value(date_time value);
		void write_value(local_date_time value);
		void write_value(date value);
		void write_value(time value);

		// Write a key value pair.
		template<typename T>
		void write(std::string_view key, T&& value);

		// Allowing passing string_literal_tag when using the above template method.
		template<typename String,
			std::enable_if_t<std::is_convertible_v<String, std::string_view>, int> = 0>
		void write(std::string_view key, String&& value, literal_string_t);
		// Array write for strings
		template<typename Container,
			std::enable_if_t<detail::is_range_v<Container>&&
			std::is_convertible_v<typename Container::value_type, std::string_view>, int> = 0>
		void write(std::string_view key, Container&& value, literal_string_t);
		// Allowing passing an integral base when using the above template method.
		template<typename Integral,
			std::enable_if_t<detail::is_integral_v<Integral>, int> = 0>
		void write(std::string_view key, Integral value, int_base);
		// Array write for integrals
		template<typename Container,
			std::enable_if_t<detail::is_range_v<Container>&&
			detail::is_integral_v<typename Container::value_type>, int> = 0>
		void write(std::string_view key, Container value, int_base);
		// 'write' overload for floating point types
		void write(std::string_view key, double value,
			float_rep representation, std::int8_t precision = auto_precision);
		// Array write for floating point types
		template<typename Container,
			std::enable_if_t<detail::is_range_v<Container>&&
			std::is_convertible_v<typename Container::value_type, double>, int> = 0>
		void write(std::string_view key, Container value,
			float_rep representation, std::int8_t precision = auto_precision);

		// Overload to support initializer_list
		template<typename T>
		void write(std::string_view key, std::initializer_list<T> value);
		// initializer_list String options support
		template<typename String,
			std::enable_if_t<std::is_convertible_v<String, std::string_view>, int> = 0>
		void write(std::string_view key, std::initializer_list<String> value, literal_string_t);
		// initializer_list Integral options support
		template<typename Integral,
			std::enable_if_t<detail::is_integral_v<Integral>, int> = 0>
		void write(std::string_view key, std::initializer_list<Integral> value, int_base);
		// initializer_list Integral options support
		template<typename Floating,
			std::enable_if_t<std::is_convertible_v<Floating, double>, int> = 0>
		void write(std::string_view key, std::initializer_list<Floating> value,
			float_rep representation, std::int8_t precision = auto_precision);

		void set_options(writer_options o)
		{
			_opts = o;
		}

		std::string to_string() const;
		friend std::ostream& operator<<(std::ostream&, const writer& rhs);

	private:
		std::vector<detail::index_t> _stack{ 0 };
		writer_options _opts;
		std::unique_ptr<detail::toml_internal_data, detail::toml_data_deleter> _data;
	};
}

#include "another_toml/writer.inl"

#endif
