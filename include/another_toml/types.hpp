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

#ifndef ANOTHER_TOML_TYPES_HPP
#define ANOTHER_TOML_TYPES_HPP

#include <cstddef>

namespace another_toml
{
	// Simple date type.
	struct date
	{
		std::uint16_t year = {};
		std::uint8_t month = {},
			day = {};
	};

	// Simple time type.
	struct time
	{
		std::uint8_t hours = {},
			minutes = {},
			seconds = {};
		float seconds_frac = {};
	};

	// Compound date/time type
	struct local_date_time
	{
		date date = {};
		time time = {};
	};

	// Compound date/time type with offset
	struct date_time
	{
		local_date_time datetime = {};
		bool offset_positive = {};
		uint8_t offset_hours = {};
		uint8_t offset_minutes = {};
	};

	// TOML value types
	enum class value_type : std::uint8_t
	{
		string,
		integer,
		floating_point,
		boolean,
		date_time,
		local_date_time,
		local_date,
		local_time,
		// The following values are internal only
		bad, // detected as invalid
		out_of_range
	};

	// Enums

	// TOML node types.
	enum class node_type : std::uint8_t
	{
		array,
		array_tables,
		key,
		inline_table,
		value,
		table,
		root_table,
		end
	};

	enum class table_def_type : std::uint8_t
	{
		dotted,
		header,
		array, // internal only: denotes an element in an array table
		end
	};

	// types for controlling string conversion in basic_node and writer.
	enum class int_base : std::uint8_t
	{
		dec,
		hex,
		oct,
		bin
	};

	enum class float_rep : std::uint8_t
	{
		default,
		fixed,
		scientific
	};

	static constexpr auto auto_precision = std::int8_t{ -1 };

	// Tag type
	struct no_throw_t {};
	constexpr auto no_throw = no_throw_t{};
}

#endif
