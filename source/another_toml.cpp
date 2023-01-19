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

#include <array>
#include <bitset>
#include <cassert>
#include <charconv>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string_view>
#include <variant>
#include <vector>

#include "uni_algo/break_grapheme.h"

#include "another_toml/except.hpp"
#include "another_toml/internal.hpp"
#include "another_toml/node.hpp"
#include "another_toml/parser.hpp"
#include "another_toml/writer.hpp"

#include "another_toml/string_util.hpp"

// Disable warning for characters and the current code page.
// All string operations are for utf8 encoded strings.
#pragma warning(disable : 4566)

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace another_toml
{
	// byte order mark for utf-8, optionally included at the beginning of utf8 text documents
	constexpr auto utf8_bom = std::array{
		0xEF, 0xBB, 0xBF
	};

	namespace detail
	{	
		struct integral
		{
			std::int64_t value;
			int_base base;
		};

		struct floating
		{
			double value;
			float_rep rep;
			std::int8_t precision = -1;
		};

		struct string_t
		{
			// string value is stored in internal_node::name
			bool literal = false;
		};

		using variant_t = std::variant<std::monostate, string_t, integral, floating, bool,
			//dates,
			date_time, local_date_time, date, time>;

		struct internal_node
		{
			std::string name;
			node_type type = node_type::end;
			value_type v_type = value_type::bad;
			variant_t value;
			table_def_type table_type = table_def_type::end;
			// a closed table can still have child tables added, but not child keys
			bool closed = true;
			index_t next = bad_index;
			index_t child = bad_index;
		};

		struct toml_internal_data
		{
			std::vector<internal_node> nodes = { internal_node{{}, node_type::table} };
#ifndef NDEBUG
			std::string input_log;
#endif
		};

		void toml_data_deleter::operator()(toml_internal_data* ptr) noexcept
		{
			delete ptr;
		}

		index_t get_next(const toml_internal_data& d, const index_t i) noexcept
		{
			assert(size(d.nodes) > i);
			return d.nodes[i].next;
		}
	}

	using namespace detail;

	static std::string to_string(node_type n)
	{
		switch (n)
		{
		case node_type::array:
			return "array"s;
		case node_type::array_tables:
			return "array table"s;
		case node_type::inline_table:
			return "inline table"s;
		case node_type::key:
			return "key"s;
		case node_type::value:
			return "value"s;
		case node_type::table:
			return "table"s;
		}

		return "error type"s;
	}

	constexpr auto root_table = index_t{};

	// method defs for nodes
	template<bool R>
	bool basic_node<R>::good() const noexcept
	{
		return _data && !(_index == bad_index ||
			node_type::end == _data->nodes[_index].type);
	}

	// test node type
	template<bool R>
	bool basic_node<R>::table() const noexcept
	{
		return good() && node_type::table == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::array() const noexcept
	{
		return good() && node_type::array == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::array_table() const noexcept
	{
		return good() && node_type::array_tables == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::key() const noexcept
	{
		return good() && node_type::key == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::value() const noexcept
	{
		return good() && node_type::value == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::inline_table() const noexcept
	{
		return good() && node_type::inline_table == _data->nodes[_index].type;
	}

	template<bool R>
	value_type basic_node<R>::type() const noexcept
	{
		if (!good())
			return value_type::bad;
		return _data->nodes[_index].v_type;
	}

	template<bool R>
	bool basic_node<R>::has_children() const noexcept
	{
		return good() && _data->nodes[_index].child != bad_index;
	}

	template<bool R>
	std::vector<basic_node<>> basic_node<R>::get_children() const
	{
		if (!good())
			throw bad_node{ "Called get_children on a bad node"s };

		auto out = std::vector<basic_node<>>{};
		auto child = _data->nodes[_index].child;
		while (child != bad_index)
		{
			if constexpr (R)
				out.emplace_back(basic_node<>{ _data.get(), child });
			else	
				out.emplace_back(basic_node<>{ _data, child });

			child = _data->nodes[child].next;
		}
		return out;
	}

	template<>
	basic_node<> basic_node<true>::get_first_child() const
	{
		if (!good())
			throw bad_node{ "Called get_first_child on a bad node"s };

		return basic_node<>{ _data.get(), _data->nodes[_index].child};
	}

	template<>
	basic_node<> basic_node<>::get_first_child() const
	{
		if (!good())
			throw bad_node{ "Called get_first_child on a bad node"s };

		return basic_node<>{ _data, _data->nodes[_index].child};
	}

	template<bool R>
	bool basic_node<R>::has_sibling() const noexcept
	{
		return good() && _data->nodes[_index].next != bad_index;
	}

	template<>
	basic_node<> basic_node<true>::get_next_sibling() const
	{
		if (!good())
			throw bad_node{ "Called get_next_sibling on a bad node"s };

		return basic_node<>{ _data.get(), _data->nodes[_index].next};
	}

	template<>
	basic_node<> basic_node<>::get_next_sibling() const
	{
		if (!good())
			throw bad_node{ "Called get_next_sibling on a bad node"s };

		return basic_node<>{ _data, _data->nodes[_index].next};
	}

	template<bool R>
	basic_node<> basic_node<R>::find_child(std::string_view name) const
	{
		if (!good())
			throw bad_node{ "Called find_child on a bad node"s };

		if (!table() && !inline_table())
			throw wrong_node_type{ "Cannot call find_child on this type of node"s };

		auto child = get_first_child();
		while (child.good() && child.as_string() != name)
			child = child.get_next_sibling();

		if (!child.good())
			throw node_not_found{ "Failed to find child node"s };

		if (child.key())
			return child.get_first_child();

		return child;
	}

	template<bool R>
	basic_node<> basic_node<R>::find_child(std::string_view name, no_throw_t) const noexcept
	{
		if (!table() && !inline_table())
			return basic_node<>{};

		auto child = get_first_child();
		while (child.good() && child.as_string() != name)
			child = child.get_next_sibling();

		if (child.key())
			return child.get_first_child();

		return child;
	}

	template<bool R>
	node_iterator basic_node<R>::begin() const noexcept
	{
		const auto child = _data->nodes[_index].child;
		if (child != bad_index)
		{
			if constexpr (R)
				return node_iterator{ _data.get(), child};
			else
				return node_iterator{ _data, child };
		}
		else
			return end();
	}

	template<bool R>
	node_iterator basic_node<R>::end() const noexcept
	{
		return node_iterator{};
	}

	struct to_string_visitor
	{
	public:
		const writer_options& options;

		std::string operator()(std::monostate)
		{
			throw wrong_type{ "This node type cannot be converted to string"s };
		}

		std::string operator()(string_t)
		{
			throw toml_error{ "Error outputing string value"s };
		}

		std::string operator()(detail::integral i)
		{
			auto out = std::stringstream{};
			using base = int_base;
			if (options.simple_numerical_output ||
				i.value < 0)
				i.base = base::dec;

			if (i.base == base::bin)
			{
				auto bin = std::bitset<64>{ *reinterpret_cast<uint64_t*>(&i.value) }.to_string();
				auto beg = begin(bin);
				auto end = std::end(bin);
				while (beg != end && *beg == '0')
					++beg;
				const auto size = std::distance(beg, end);
				if (size < 1)
					out << "0b0"s;
				else
					out << "0b"s << std::string_view{ &*beg, static_cast<std::size_t>(size) };
				return out.str();
			}
			else
			{
				switch (i.base)
				{
				case base::dec:
					out << std::dec;
					break;
				case base::hex:
					out << std::hex << "0x"s;
					break;
				case base::oct:
					out << std::oct << "0o"s;
					break;
				}

				out << i.value;
				return out.str();
			}
		}

		std::string operator()(detail::floating d)
		{
			if (std::isnan(d.value))
				return "nan"s;
			else if (d.value == std::numeric_limits<double>::infinity())
				return "inf"s;
			else if (d.value == -std::numeric_limits<double>::infinity())
				return "-inf"s;

			auto strm = std::ostringstream{};
			if (d.rep == float_rep::scientific &&
				!options.simple_numerical_output)
				strm << std::scientific;
			else if (d.rep == float_rep::fixed &&
				!options.simple_numerical_output)
				strm << std::fixed;

			if (d.precision > auto_precision)
				strm << std::setprecision(d.precision);
	
			strm << d.value;
			auto str = strm.str();

			if (str.find('.') == std::string::npos &&
				str.find('e') == std::string::npos)
				str += ".0"s;

			return str;
		}

		std::string operator()(date v)
		{
			auto strm = std::ostringstream{};
			strm << std::setfill('0');
			strm << std::setw(4) << v.year;
			strm << '-' << std::setw(2) << static_cast<uint16_t>(v.month);
			strm << '-' << std::setw(2) << static_cast<uint16_t>(v.day);
			return strm.str();
		}

		std::string operator()(date_time v)
		{
			auto out = this->operator()(v.datetime);
			if (v.offset_hours == 0 &&
				v.offset_minutes == 0)
			{
				out.push_back('Z');
			}
			else
			{
				auto strm = std::ostringstream{};
				strm << std::setfill('0');
				out.push_back(v.offset_positive ? '+' : '-');
				//strm << v.offset_positive ? '+' : '-';
				strm << std::setw(2) << static_cast<uint16_t>(v.offset_hours);
				strm << ':' << std::setw(2) << static_cast<uint16_t>(v.offset_minutes);
				out += strm.str();
			}
			return out;
		}

		std::string operator()(time v)
		{
			auto strm = std::ostringstream{};
			strm << std::setfill('0');
			strm << std::setw(2) << static_cast<uint16_t>(v.hours);
			strm << ':' << std::setw(2) << static_cast<uint16_t>(v.minutes);
			// TOML 1.1 optional seconds
			strm << ':' << std::setw(2) << static_cast<uint16_t>(v.seconds);
			if (v.seconds_frac != 0.f)
			{
				auto fracs = std::to_string(v.seconds_frac);
				while (!empty(fracs) && fracs.back() == '0')
					fracs.pop_back();
				strm << fracs.substr(1);
			}
			return strm.str();
		}

		std::string operator()(local_date_time v)
		{
			auto out = this->operator()(v.date);
			using sep = writer_options::date_time_separator_t;
			switch (options.date_time_separator)
			{
			case sep::big_t:
				out.push_back('T');
				break;
			case sep::whitespace:
				out.push_back(' ');
				break;
			}

			out += this->operator()(v.time);
			return out;
		}

		std::string operator()(bool b)
		{
			return b ? "true"s : "false"s;
		}
	};

	template<bool R>
	std::string basic_node<R>::as_string() const
	{
		if (!good())
			throw bad_node{ "Called as_string on a bad node"s };

		if(_data->nodes[_index].v_type == value_type::string ||
			_data->nodes[_index].type != node_type::value)
			return _data->nodes[_index].name;

		return std::visit(to_string_visitor{ writer_options{} }, _data->nodes[_index].value);
	}

	template<bool R>
	std::string basic_node<R>::as_string(int_base b) const
	{
		if (!good())
			throw bad_node{ "Called as_string on a bad node"s };

		try
		{
			auto value = std::get<integral>(_data->nodes[_index].value);
			value.base = b;

			return to_string_visitor{ writer_options{} }(value);
		}
		catch (const std::bad_variant_access&)
		{
			throw wrong_type{ "This overload only works on integral types"s };
		}
	}

	template<bool R>
	std::string basic_node<R>::as_string(float_rep rep, std::int8_t prec) const
	{
		if (!good())
			throw bad_node{ "Called as_string on a bad node"s };

		try
		{
			auto value = std::get<floating>(_data->nodes[_index].value);
			value.rep = rep;
			value.precision = prec;

			return to_string_visitor{ writer_options{} }(value);
		}
		catch (const std::bad_variant_access&)
		{
			throw wrong_type{ "This overload only works on floating point types"s };
		}
	}

	template<bool R>
	std::int64_t basic_node<R>::as_integer() const
	{
		if (!good())
			throw bad_node{ "Called as_integer on a bad node"s };

		try
		{
			const auto integral = std::get<detail::integral>(_data->nodes[_index].value);
			return integral.value;
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	double basic_node<R>::as_floating() const
	{
		if (!good())
			throw bad_node{ "Called as_floating on a bad node"s };

		try
		{
			return std::get<floating>(_data->nodes[_index].value).value;
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	bool basic_node<R>::as_boolean() const
	{
		if (!good())
			throw bad_node{ "Called as_boolean on a bad node"s };

		try
		{
			return std::get<bool>(_data->nodes[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	date_time basic_node<R>::as_date_time() const
	{
		if (!good())
			throw bad_node{ "Called as_date_time on a bad node"s };

		try
		{
			return std::get<date_time>(_data->nodes[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	local_date_time basic_node<R>::as_date_time_local() const
	{
		if (!good())
			throw bad_node{ "Called as_date_time_local on a bad node"s };

		try 
		{
			return std::get<local_date_time>(_data->nodes[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	date basic_node<R>::as_date_local() const
	{
		if (!good())
			throw bad_node{ "Called as_date_local on a bad node"s };

		try
		{
			return std::get<date>(_data->nodes[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template<bool R>
	time basic_node<R>::as_time_local() const
	{
		if (!good())
			throw bad_node{ "Called as_time_local on a bad node"s };

		try
		{
			return std::get<time>(_data->nodes[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	template class basic_node<true>;
	template class basic_node<false>;

	// helpers for adding elements to the internal data structure
	template<bool NoThrow>
	static index_t insert_child(detail::toml_internal_data& d, const index_t parent, detail::internal_node n);
	template<bool NoThrow>
	static index_t insert_child_table(const index_t parent, std::string name, detail::toml_internal_data& d, table_def_type t);
	template<bool NoThrow>
	static index_t insert_child_table_array(index_t parent, std::string name, detail::toml_internal_data& d);

	//method defs for writer
	writer::writer()
		: _data{ new toml_internal_data{} }
	{}

	void writer::begin_table(std::string_view table_name, table_def_type table_type)
	{
		assert(table_type != table_def_type::end);
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::inline_table ||
			t == node_type::array ||
			t == node_type::array_tables);
		assert(table_type == table_def_type::dotted ||
			table_type == table_def_type::header);

		auto new_table = insert_child_table<false>(i, std::string{ table_name }, *_data, table_type);
		assert(new_table != bad_index);
		_stack.emplace_back(new_table);
	}

	void writer::end_table() noexcept
	{
		assert(!empty(_stack));
		auto i = _stack.back();
		assert(_data->nodes[i].type == node_type::table);
		_stack.pop_back();
		return;
	}

	void writer::begin_array(std::string_view name)
	{
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::inline_table ||
			t == node_type::array);

		auto new_arr = insert_child<false>(*_data, i, internal_node{ std::string{ name }, node_type::array });
		assert(new_arr != bad_index);
		_stack.emplace_back(new_arr);
		return;
	}

	void writer::end_array() noexcept
	{
		assert(!empty(_stack));
		auto i = _stack.back();
		assert(_data->nodes[i].type == node_type::array);
		_stack.pop_back();
		return;
	}

	void writer::begin_inline_table(std::string_view name)
	{
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::array ||
			t == node_type::inline_table);

		auto new_table = insert_child<false>(*_data, i, internal_node{ std::string{ name }, node_type::inline_table });
		assert(new_table != bad_index);
		_stack.emplace_back(new_table);
		return;
	}

	void writer::end_inline_table() noexcept
	{
		assert(!empty(_stack));
		auto i = _stack.back();
		assert(_data->nodes[i].type == node_type::inline_table);
		_stack.pop_back();
		return;
	}

	void writer::begin_array_table(std::string_view name)
	{
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::array_tables);

		auto new_table = insert_child_table_array<false>(i, std::string{ name }, *_data);
		assert(new_table != bad_index);
		_stack.emplace_back(new_table);
	}

	void writer::end_array_table() noexcept
	{
		assert(!empty(_stack));
		auto i = _stack.back();
		assert(_data->nodes[i].type == node_type::table);
		_stack.pop_back();
		return;
	}

	// write values on their own, for arrays
	void writer::write_key(std::string_view name)
	{
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::inline_table);

		auto new_table = insert_child<false>(*_data, i, internal_node{ std::string{ name }, node_type::key });
		assert(new_table != bad_index);
		_stack.emplace_back(new_table);
		return;
	}

	struct string_cont
	{
		std::string value;
		bool literal;
	};

	template<typename Value>
	static index_t write_value_impl(index_t parent, toml_internal_data& d, value_type ty, Value v)
	{
		const auto t = d.nodes[parent].type;
		assert(t == node_type::key ||
			t == node_type::array);

		auto new_node = bad_index;
		if constexpr(std::is_same_v<string_cont, std::decay_t<Value>>)
			new_node = insert_child<false>(d, parent, internal_node{ std::move(v.value), node_type::value, ty, string_t{ v.literal } });
		else
			new_node = insert_child<false>(d, parent, internal_node{ {}, node_type::value, ty, std::move(v) });

		assert(new_node != bad_index);
		return new_node;
	}
	
	void writer::write_value(std::string&& value)
	{
		write_value_impl(_stack.back(), *_data, value_type::string, string_cont{ std::move(value), false });
		if (_data->nodes[_stack.back()].type == node_type::key)
			_stack.pop_back();
		return;
	}

	void writer::write_value(std::string&& value, literal_string_t)
	{
		write_value_impl(_stack.back(), *_data, value_type::string, string_cont{ std::move(value), true });
		if (_data->nodes[_stack.back()].type == node_type::key)
			_stack.pop_back();
		return;
	}

	void writer::write_value(std::string_view value)
	{
		write_value_impl(_stack.back(), *_data, value_type::string, string_cont{ std::string{ value }, false });
		if (_data->nodes[_stack.back()].type == node_type::key)
			_stack.pop_back();
		return;
	}

	void writer::write_value(std::string_view value, literal_string_t)
	{
		write_value_impl(_stack.back(), *_data, value_type::string, string_cont{ std::string{ value }, true });
		if (_data->nodes[_stack.back()].type == node_type::key)
			_stack.pop_back();
		return;
	}

	void writer::write_value(std::int64_t value, int_base base)
	{
		write_value_impl(_stack.back(), *_data, value_type::integer, detail::integral{ value, base });
		if(_data->nodes[_stack.back()].type == node_type::key)
			_stack.pop_back(); 
		return;
	}

	void writer::write_value(double value, float_rep rep, std::int8_t precision)
	{
		write_value_impl(_stack.back(), *_data, value_type::floating_point, detail::floating{ value, rep, precision });
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write_value(bool value)
	{
		write_value_impl(_stack.back(), *_data, value_type::boolean, std::move(value));
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write_value(date_time value)
	{
		write_value_impl(_stack.back(), *_data, value_type::date_time, std::move(value));
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write_value(local_date_time value)
	{
		write_value_impl(_stack.back(), *_data, value_type::local_date_time, std::move(value));
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write_value(date value)
	{
		write_value_impl(_stack.back(), *_data, value_type::local_date, std::move(value));
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write_value(time value)
	{
		write_value_impl(_stack.back(), *_data, value_type::local_time, std::move(value));
		if(_data->nodes[_stack.back()].type == node_type::key) 
			_stack.pop_back();
		return;
	}

	void writer::write(std::string_view key, double value, float_rep rep, std::int8_t precision)
	{
		write_key(key);
		write_value(value, rep, precision);
		return;
	}

	std::string writer::to_string() const
	{
		auto sstream = std::ostringstream{};
		sstream << *this;
		return std::move(sstream).str();
	}

	// Returns true if a table header should be written for i
	static bool is_headered_table(index_t i, const toml_internal_data& d) noexcept
	{
		auto& table = d.nodes[i];
		auto child = table.child;
		while (child != bad_index)
		{
			const auto& c_ref = d.nodes[child];
			
			if (node_type::value != c_ref.type &&
				!empty(c_ref.name))
				return true;

			child = c_ref.next;
		}
		return false;
	}

	static std::string make_table_name(const std::vector<index_t>& nodes, const toml_internal_data& d,
		const writer_options& o)
	{
		assert(!empty(nodes));
		if (nodes.empty())
			return "\"\""s;

		auto out = std::string{};
		auto beg = begin(nodes);
		const auto end = std::end(nodes);
		for (beg; beg != end; ++beg)
		{
			if (empty(d.nodes[*beg].name))
				continue;

			out += escape_toml_name(d.nodes[*beg].name, o.ascii_output);
			if (next(beg) != end)
				out.push_back('.');
		}

		return out;
	}

	static std::vector<index_t> get_children(index_t i, const toml_internal_data& d)
	{
		auto children = std::vector<index_t>{};
		auto child = d.nodes[i].child;
		while (child != bad_index)
		{
			children.emplace_back(child);
			child = d.nodes[child].next;
		}
		return children;
	}

	template<typename UnaryFunction>
	void for_each_child(index_t i, const toml_internal_data& d, UnaryFunction f) 
		noexcept(std::is_nothrow_invocable_v<UnaryFunction, const internal_node&>)
	{
		auto child = d.nodes[i].child;
		while (child != bad_index)
		{
			std::invoke(f, d.nodes[child]);
			child = d.nodes[child].next;
		}
		return;
	}

	template<typename Table>
	static bool dotted_table_has_keys(const Table& i, const toml_internal_data& d)
	{
		const internal_node* table = {};
		if constexpr (std::is_same_v<Table, internal_node>)
			table = &i;
		else
			table = &d.nodes[i];

		assert(table->type == node_type::table &&
			table->table_type == table_def_type::dotted);

		auto child = table->child;
		while (child != bad_index)
		{
			auto& child_ref = d.nodes[child];

			if (child_ref.type == node_type::key)
				return true;

			if (child_ref.type == node_type::table &&
				child_ref.table_type == table_def_type::dotted &&
				dotted_table_has_keys(child, d))
				return true;

			child = child_ref.next;
		}

		return false;
	}

	static bool skip_table_header(index_t table, index_t child, const toml_internal_data& d)
	{
		if (child == bad_index)
			return false;

		auto tables = true;
		const auto func = [&tables, &d](const internal_node& n) noexcept {
			if (n.type != node_type::table &&
				n.type != node_type::array_tables)
				tables = false;

			if(n.type == node_type::table &&
				n.table_type == table_def_type::dotted &&
				dotted_table_has_keys(n, d))
				tables = false;

			return;
		};

		for_each_child(table, d, func);
		return tables;
	}

	using char_count_t = std::int16_t;
	
	static void append_line_length(char_count_t& line, std::size_t length, const writer_options& o) noexcept
	{
		if (length > o.max_line_length)
			line = o.max_line_length + 1;
		else
			line += static_cast<char_count_t>(length);

		if (line > o.max_line_length)
			line = o.max_line_length + 1;
		return;
	}

	static bool optional_newline(std::ostream& strm, char_count_t& last_newline, const writer_options& o)
	{
		if (last_newline > 0 && last_newline > o.max_line_length)
		{
			strm << '\n';
			last_newline = {};
			return true;
		}
		return false;
	}

	using indent_level_t = std::int32_t;

	static void optional_indentation(std::ostream& strm, indent_level_t indent, const writer_options& o,
		char_count_t& last_newline_dist)
	{
		if (o.indent_child_tables)
		{
			for (auto i = indent_level_t{}; i < indent; ++i)
			{
				strm << o.indent_string;
				const auto str_size = size(o.indent_string);
				append_line_length(last_newline_dist, str_size, o);
			}
		}
	}

	// Returns true if the string can be output as a literal
	static bool string_can_be_literal(std::string_view str) noexcept
	{
		return std::search_n(begin(str), end(str), 3, '\'') == end(str);
	}

	enum string_out_type
	{
		default,
		literal,
		multiline,
		literal_multiline_one_line,
		literal_multiline
	};

	constexpr bool is_unicode_whitespace(char32_t c) noexcept
	{
		switch (c)
		{
		case '\t':
			[[fallthrough]];
		case ' ':
			[[fallthrough]];
		//case U'\xA0': // No break space
		//	[[fallthrough]];
		case U'\u1680': // OGHAM space mark
			[[fallthrough]];
		case U'\u2000': // en quad
			[[fallthrough]];
		case U'\u2001': // em quad
			[[fallthrough]];
		case U'\u2002': // en space
			[[fallthrough]];
		case U'\u2003': // em space
			[[fallthrough]];
		case U'\u2004': // 3 em space
			[[fallthrough]];
		case U'\u2005': // 4 em space 
			[[fallthrough]]; 
		case U'\u2006': // 6 em space
			[[fallthrough]];
		case U'\u2008': // punc space
			[[fallthrough]];
		case U'\u2009': // thin space
			[[fallthrough]];
		case U'\u200A': // hair space
			[[fallthrough]];
		case U'\u205F': // medium math space
			[[fallthrough]];
		case U'\u3000': // ideographic space
			return true;
		default:
			return false;
		}
	}

	static std::string add_multiline_wraps(std::string_view str, const writer_options& o, std::int16_t& last_newline_dist)
	{
		auto unicode = unicode8_to_unicode32(str);
		// need to detect spaces and unicode newline chars
		for (auto i = std::size_t{}; i < size(unicode); ++i)
		{
			if (last_newline_dist > o.max_line_length
				&& is_unicode_whitespace(unicode[i]))
			{
				unicode.insert(++i, U"\\\n"s);
				++i;
				last_newline_dist = {};
				continue;
			}

			if (unicode[i] == '\n')
				last_newline_dist = {};
			else
				++last_newline_dist;
		}

		return unicode32_to_unicode8(unicode);
	}

	static void write_out_string(std::ostream& strm, const string_t& string_extra, std::string_view str,
		const writer_options& o, char_count_t& last_newline_dist)
	{
		auto type = string_out_type::default;
		if (string_extra.literal)
			type = string_out_type::literal;

		if (type == string_out_type::literal && std::find(begin(str), end(str), '\'') != end(str))
		{
			if ((o.ascii_output && contains_unicode(str))
				|| !string_can_be_literal(str))
				type = string_out_type::default;
			else
				type = string_out_type::literal_multiline_one_line;
		}

		if (size(str) > o.max_line_length)
		{
			if (type == string_out_type::default)
				type = string_out_type::multiline;
			if (type == string_out_type::literal ||
				type == string_out_type::literal_multiline_one_line)
				type = string_out_type::literal_multiline;
		}

		if(type == string_out_type::literal_multiline_one_line &&
			str.find('\n') != std::string_view::npos)
			type = type = string_out_type::literal_multiline;

		switch (type)
		{
		case string_out_type::default:
		{
			strm.put('\"');
			++last_newline_dist;
			const auto esc_str = o.ascii_output ? to_escaped_string2(str) : to_escaped_string(str);
			strm.write(data(esc_str), size(esc_str));
			strm << '\"';
			append_line_length(last_newline_dist, 2 + size(esc_str), o);
		}break;
		case string_out_type::literal:
		{
			strm.put('\'');
			++last_newline_dist;
		}break;
		case string_out_type::multiline:
		{
			strm << "\"\"\"\n"s;
			last_newline_dist = {};
			auto esc_str = o.ascii_output ? to_escaped_multiline2(str) : to_escaped_multiline(str);
			esc_str = add_multiline_wraps(esc_str, o, last_newline_dist);
			auto newline = esc_str.find_last_of('\n');
			if (newline == std::string::npos)
				newline = {};
			strm << esc_str << "\"\"\""s;
			append_line_length(last_newline_dist, size(esc_str) - newline + 3, o);
		}break;
		case string_out_type::literal_multiline:
		{
			auto newline = str.find_last_of('\n');
			if (newline == std::string_view::npos)
				newline = {};
			strm << "\'\'\'\n"s << str << "\'\'\'"s;
			append_line_length(last_newline_dist, size(str) - newline + 3, o);
		}break;
		case string_out_type::literal_multiline_one_line:
		{
			strm << "\'\'\'"s << str << "\'\'\'"s;
			append_line_length(last_newline_dist, size(str) + 6, o);
		}break;
		}

		return;
	}

	constexpr uint8_t sort_value(node_type t) noexcept
	{
		switch (t)
		{
		case node_type::key:
			[[fallthrough]];
		case node_type::array:
			[[fallthrough]];
		case node_type::inline_table:
			[[fallthrough]];
		case node_type::value:
			return 1;
		default:
			return 2;
		}
	}

	template<bool WriteOne>
	static void write_children(std::ostream& strm, const toml_internal_data& d,
		const writer_options& o, std::vector<index_t> stack, char_count_t& last_newline_dist,
		indent_level_t indent_level)
	{
		assert(!empty(stack));
		const auto parent = stack.back();
		const auto parent_type = d.nodes[parent].type;

		auto children = get_children(parent, d);

		// make sure we write out keys and dotted tables before
		// tables and table arrays
		std::stable_sort(begin(children), end(children), [&d](auto&& l, auto&& r) {
			const auto& left = d.nodes[l];
			const auto& right = d.nodes[r];
			const auto left_type = sort_value(left.type);
			const auto right_type = sort_value(right.type);
			return std::tie(left_type, left.table_type) <
				   std::tie(right_type, right.table_type);
			});

		auto beg = begin(children);
		const auto end = std::end(children);
		for (beg; beg != end; ++beg)
		{
			const auto& c_ref = d.nodes[*beg];
			switch (c_ref.type)
			{
			case node_type::table:
			{
				assert(parent_type == node_type::table ||
					parent_type == node_type::array_tables);

				auto name_stack = stack;
				name_stack.emplace_back(*beg);

				if (parent_type != node_type::array_tables && 
					(is_headered_table(*beg, d) || c_ref.child == bad_index))
				{
					// skip if all their children are also tables
					// skip writing empty tables unless they are leafs
					if((o.skip_empty_tables && skip_table_header(*beg, c_ref.child, d)) ||
						c_ref.table_type == table_def_type::dotted)
					{
						write_children<false>(strm, d, o, name_stack, last_newline_dist, indent_level);
						break;
					}

					if (last_newline_dist != -1 &&
						!o.compact_spacing)
					{
						strm << '\n';
						last_newline_dist = {};
					}

					optional_indentation(strm, indent_level + 1, o, last_newline_dist);

					strm << '[' << make_table_name(name_stack, d, o) << "]\n"s;
					last_newline_dist = {};
				}

				write_children<false>(strm, d, o, std::move(name_stack), last_newline_dist, indent_level + 1);
			} break;
			case node_type::array:
			{
				assert(parent_type == node_type::table ||
					parent_type == node_type::inline_table ||
					parent_type == node_type::array);

				if (parent_type != node_type::array)
				{
					optional_indentation(strm, indent_level, o, last_newline_dist);

					strm << escape_toml_name(c_ref.name, o.ascii_output);
					if (o.compact_spacing)
					{
						strm << '=';
						last_newline_dist += 1;
					}
					else
					{
						strm << " = ";
						last_newline_dist += 3;
					}
				}

				strm << '[';
				++last_newline_dist;
				if (!o.compact_spacing)
				{
					strm << ' ';
					++last_newline_dist;
				}

				if (optional_newline(strm, last_newline_dist, o))
					optional_indentation(strm, indent_level, o, last_newline_dist);

				write_children<false>(strm, d, o, { *beg }, last_newline_dist, indent_level);

				strm << ']';
				++last_newline_dist;
				if (parent_type == node_type::table)
				{
					strm << '\n';
					last_newline_dist = {};
				}
				else if (parent_type == node_type::array &&
					next(beg) != end)
				{
					if (!o.compact_spacing)
					{
						strm << ", "s;
						last_newline_dist += 2;
					}
					else
					{
						strm << ',';
						++last_newline_dist;
					}
				}
				else if (!o.compact_spacing)
				{
					strm << ' ';
					++last_newline_dist;
				}
			} break;
			case node_type::array_tables:
			{
				const auto indent = indent_level + 1;

				auto name_stack = stack;
				name_stack.emplace_back(*beg);
				const auto child_tables = get_children(*beg, d);
				const auto child_end = std::end(child_tables);
				for(auto beg = begin(child_tables); beg != child_end; ++beg)
				{
					if (last_newline_dist != -1 &&
						size(stack) < 2 &&
						!o.compact_spacing)
					{
						strm << '\n';
						last_newline_dist = {};
					}

					optional_indentation(strm, indent, o, last_newline_dist);

					strm << "[["s << make_table_name(name_stack, d, o) << "]]\n"s;
					last_newline_dist = {};
					name_stack.emplace_back(*beg);
					write_children<false>(strm, d, o, name_stack, last_newline_dist, indent);
					name_stack.pop_back();
				}
			} break;
			case node_type::key:
			{
				assert(parent_type == node_type::table ||
					parent_type == node_type::inline_table);

				optional_indentation(strm, indent_level, o, last_newline_dist);

				// get dotted tables that contribute to this key name
				auto dotted_tables = std::vector<const std::string*>{};
				{
					const auto end = rend(stack);
					auto iter = rbegin(stack);
					for (iter; iter != end; ++iter)
					{
						const auto &ref = d.nodes[*iter];
						if (ref.type != node_type::table ||
							ref.table_type != table_def_type::dotted)
							break;

						dotted_tables.emplace_back(&ref.name);
					}
				}

				// start the name with any dotted table names
				auto key_name = std::string{};
				std::for_each(rbegin(dotted_tables), rend(dotted_tables), [&key_name, &o](auto str_ptr) {
					assert(str_ptr);
					const auto escaped_name = escape_toml_name(*str_ptr, o.ascii_output);
					key_name += escaped_name;
					key_name.push_back('.');
					return;
					});

				// finaly
				key_name += escape_toml_name(c_ref.name, o.ascii_output);
				append_line_length(last_newline_dist, size(key_name), o);
				strm << key_name;
				if (!o.compact_spacing)
				{
					strm << " = "s;
					last_newline_dist += 3;
				}
				else
				{
					strm << '=';
					++last_newline_dist;
				}

				//value
				write_children<true>(strm, d, o, { *beg }, last_newline_dist, indent_level);

				if (parent_type == node_type::inline_table)
				{
					if (o.compact_spacing)
					{
						if (next(beg) != end)
						{
							strm << ',';
							++last_newline_dist;
						}
					}
					else
					{
						if (next(beg) != end)
						{
							strm << ", "s;
							last_newline_dist += 2;
						}
						else
						{
							strm << ' ';
							++last_newline_dist;
						}
					}

					//TOML1.x allow newlines in inline tables just like arrays
					//optional_newline(strm, last_newline, o);
				}
				else
				{
					strm << '\n';
					last_newline_dist = {};
				}
			} break;
			case node_type::inline_table:
			{
				assert(parent_type == node_type::table ||
					parent_type == node_type::inline_table ||
					parent_type == node_type::array);

				if (parent_type != node_type::array)
				{
					optional_indentation(strm, indent_level, o, last_newline_dist);

					const auto table_name = escape_toml_name(c_ref.name, o.ascii_output);
					append_line_length(last_newline_dist, size(table_name), o);
					strm << table_name;
					if (!o.compact_spacing)
					{
						strm << " = "s;
						last_newline_dist += 3;
					}
					else
					{
						strm << '=';
						++last_newline_dist;
					}
				}

				if (!o.compact_spacing)
				{
					strm << "{ "s;
					last_newline_dist += 2;
				}
				else
				{
					strm << '{';
					++last_newline_dist;
				}

				write_children<false>(strm, d, o, { *beg }, last_newline_dist, indent_level);

				if (!o.compact_spacing)
				{
					strm << "} "s;
					last_newline_dist += 2;
				}
				else
				{
					strm << '}';
					++last_newline_dist;
				}

				if (parent_type != node_type::table &&
					next(beg) != end)
				{
					if (o.compact_spacing)
					{
						strm << ',';
						++last_newline_dist;
					}
					else
					{
						strm << ", "s;
						last_newline_dist += 2;
					}

					optional_newline(strm, last_newline_dist, o);
				}
				else
				{
					strm << '\n';
					last_newline_dist = {};
				}
			} break;
			case node_type::value:
			{
				if (c_ref.v_type == value_type::string)
				{
					const auto& string_extra = std::get<string_t>(c_ref.value);
					write_out_string(strm, string_extra, c_ref.name, o, last_newline_dist);
				}
				else if (c_ref.v_type == value_type::bad)
					throw toml_error{ "Value node with bad data, unable to output"s };
				else
				{
					const auto str = std::visit(to_string_visitor{ o }, c_ref.value);
					strm << str;
					append_line_length(last_newline_dist, size(str), o);
				}

				if (parent_type == node_type::array)
				{
					if (o.compact_spacing)
					{
						if (next(beg) != end)
						{
							strm << ',';
							++last_newline_dist;
						}
					}
					else
					{
						if (next(beg) != end)
						{
							strm << ", "s;
							last_newline_dist += 2;
						}
						else
						{
							strm << ' ';
							++last_newline_dist;
						}
					}

					optional_newline(strm, last_newline_dist, o);
				}
				
				if constexpr (WriteOne)
				{
					if (next(beg) != end)
						assert(false);
				}
			} break;
			default:
				assert(false);// bad
			}
		}
		return;
	}

	std::ostream& operator<<(std::ostream& o, const writer& w)
	{
		//write the byte order mark
		if (w._opts.utf8_bom)
		{
			for (const auto ch : utf8_bom)
				o.put(static_cast<char>(ch));
		}

		auto last_newline = char_count_t{ -1 };
		write_children<false>(o, *w._data, w._opts, { 0 }, last_newline, -1);

		return o;
	}

	void insert_bad(detail::toml_internal_data& d)
	{
		d.nodes.emplace_back(internal_node{ {}, node_type::end });
		return;
	}

	template<bool NoThrow>
	index_t insert_child(detail::toml_internal_data& d, const index_t parent, detail::internal_node n)
	{
		assert(parent != bad_index);
		const auto new_index = size(d.nodes);
		auto& p = d.nodes[parent];
		auto allow_duplicates = p.type == node_type::array || p.type == node_type::array_tables;
		if (p.child != bad_index)
		{
			auto child = p.child;
			while (true)
			{
				auto& child_ref = d.nodes[child];
				if (unicode_string_equal(child_ref.name, n.name) && !allow_duplicates)
				{
					if (child_ref.type == node_type::table && 
						n.type == node_type::table &&
						!child_ref.closed &&
						p.table_type == n.table_type)
						return child;

					const auto msg = "Tried to insert duplicate element: "s + n.name +
						", into: "s + (parent == 0 ? "root table"s : p.name) + ".\n"s;

					if constexpr (NoThrow)
					{
						std::cerr << msg;
						return bad_index;
					}
					else
						throw duplicate_element{ msg, {}, {}, n.name };
				}

				if (child_ref.next == bad_index)
					break;

				child = child_ref.next;
			}

			d.nodes[child].next = new_index;
		}
		else
			p.child = new_index;

		d.nodes.emplace_back(std::move(n));
		return new_index;
	}

	static index_t find_child(const detail::toml_internal_data& d, const index_t parent, const std::string_view s) noexcept
	{
		auto& p = d.nodes[parent];
		if (p.child == bad_index)
			return bad_index;

		auto next = p.child;
		while (next != bad_index)
		{
			auto c = &d.nodes[next];
			if (unicode_string_equal(c->name, s))
				break;
			next = c->next;
		}
		return next;
	}

	enum class token_type
	{
		table,
		array_table,
		inline_table,
		inline_table_end,
		array,
		array_end,
		key,
		value, // element end
		comma,
		newline,
		bad
	};

	struct key_name
	{
		index_t parent;
		std::optional<std::string> name;
	};

	struct parser_state
	{
		template<bool NoThrow>
		std::pair<char, bool> get_char() noexcept(NoThrow)
		{
			auto val = strm.get();
			if (val == std::istream::traits_type::eof())
			{
				if constexpr (NoThrow)
				{
					return { {}, true };
				}
				else
				{
					auto string = std::ostringstream{};
					string << "Encountered an unexpected eof.\n";
					print_error_string(*this, col, error_entire_line, string);
					throw unexpected_eof{ string.str(), line, col };
				}
			}

			toml_file.push_back(static_cast<char>(val));
			++col;
			return { static_cast<char>(val), {} };
		}

		void nextline() noexcept
		{
			col = {};
			++line;
			toml_file.clear();
			return;
		}

		void ignore()
		{
			toml_file.push_back(strm.peek());
			strm.ignore();
			++col;
			return;
		}

		void putback() noexcept
		{
			--col;
			strm.putback(toml_file.back());
			toml_file.pop_back();
			return;
		}

		void putback(const char ch) noexcept
		{
			--col;
			strm.putback(ch);
			toml_file.pop_back();
			return;
		}

		void close_tables(toml_internal_data& toml_data) noexcept
		{
			for (auto t : open_tables)
			{
				assert(toml_data.nodes[t].type == node_type::table);
				toml_data.nodes[t].closed = true;
			}
			open_tables.clear();
		}

		std::istream& strm;
		// stack is never empty, but may contain table->key->inline table->key->array->etc.
		std::vector<index_t> stack;
		// tables that need to be closed when encountering the next table header
		std::vector<index_t> open_tables;
		std::vector<token_type> token_stream;
		std::size_t line = {};
		std::size_t col = {};
		// Stores the previously parsed line.
		std::string toml_file;
	};

	//new line(we check both, since file may have been opened in binary mode)
	static bool newline(parser_state& strm, char ch) noexcept
	{
		if (ch == '\r' && strm.strm.peek() == '\n')
		{
			strm.ignore();
			return true;
		}

		switch (ch)
		{
		case '\n':
			[[fallthrough]];
		case '\f':
			[[fallthrough]];
		case '\v':
			return true;
		default:
			return false;
		}

		//return ch == '\n';
	}

	//test ch against the list of forbidden chars above
	constexpr bool comment_forbidden_char(char ch) noexcept
	{
		/*
		constexpr auto comment_forbidden_chars = std::array<char, 32>{
			//U+0000 to U+0008
			'\0', 1, 2, 3, 4, 5, 6, '\a', '\b',
			//U+000A to U+001F
			'\n', '\v', '\f', '\r', 14, 15, 16, 17, 18, 19, 20,
			21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
			//U+007F
			127
		};
		*/

		// simplified version of the above array
		return ch >= 0 && ch < 9 ||
			ch > 9 && ch < 32 ||
			ch == 127;
	} 

	// limit to a-z, A-Z, 0-9, '-' and '_'
	// NOTE: this changes in TOML 1.1
	constexpr bool valid_key_name_char(char ch) noexcept
	{
		return ch == '-'			||
			ch >= '0' && ch <= '9'	||
			ch >= 'A' && ch <= 'Z'	||
			ch == '_'				||
			ch >= 'a' && ch <= 'z';
	}

	//skip through whitespace
	// returns true if you need to get a new char
	static bool whitespace(char ch, parser_state& strm) noexcept
	{
		if (ch != ' ' && ch != '\t')
			return false;

		auto eof = false;
		while (ch == ' ' || ch == '\t')
		{
			std::tie(ch, eof) = strm.get_char<true>();
			if (eof)
				return true;
		}

		strm.putback(ch);
		return true;
	}

	template<bool NoThrow>
	static index_t insert_child_table(const index_t parent, std::string name, detail::toml_internal_data& d, table_def_type t)
	{
		auto table = detail::internal_node{ std::move(name) , node_type::table };
		table.closed = false;
		table.table_type = t;
		return insert_child<NoThrow>(d, parent, std::move(table));
	}

	constexpr auto error_entire_line = std::numeric_limits<std::size_t>::max();
	constexpr auto error_current_col = std::numeric_limits<std::size_t>::max() - 1;

	static void parse_line(parser_state& strm)
	{
		if (strm.toml_file.back() != '\n')
		{
			auto [ch, eof] = strm.get_char<true>();
			while (!eof && ch != '\n')
				std::tie(ch, eof) = strm.get_char<true>();
		}
	}

	static void print_error_string(parser_state& strm, std::size_t error_begin, std::size_t error_end,
		std::ostream& cerr)
	{
		auto line_display_strm = std::ostringstream{};
		line_display_strm << strm.line + 1 << '>';
		const auto line_display = line_display_strm.str();

		if (empty(strm.toml_file))
		{
			cerr << line_display_strm.str() << "\uFFFD\n";
			for (auto i = std::size_t{}; i < size(line_display); ++i)
				cerr.put(' ');
			cerr << '^';
			return;
		}

		if (error_end == error_current_col)
		{
			error_end = strm.toml_file.find_last_not_of(' ');
			if (error_end > size(strm.toml_file))
				error_end = error_entire_line;
		}

		if (strm.toml_file.back() != '\n')
		{
			auto [ch, eof] = strm.get_char<true>();
			while (!eof && ch != '\n')
				std::tie(ch, eof) = strm.get_char<true>();
		}

		if (error_end == error_entire_line)
			error_end = strm.toml_file.find_last_not_of(' ');

		const auto is_position_control_char = [](auto ch) {
			return ch == '\v' ||
				ch == '\r' ||
				ch == '\f';
		};

		auto iter = std::find_if(begin(strm.toml_file), end(strm.toml_file), is_position_control_char);

		// replace any positional control characters
		constexpr auto replacement_char = "\uFFFD"sv;
		while (iter != end(strm.toml_file))
		{
			iter = strm.toml_file.erase(iter);
			iter = strm.toml_file.insert(iter, begin(replacement_char), std::end(replacement_char));
			iter = std::find_if(iter, end(strm.toml_file), is_position_control_char);
		}

		cerr << line_display << strm.toml_file;
		if(strm.toml_file.back() != '\n')
			cerr << '\n';

		for (auto i = std::size_t{}; i < size(line_display); ++i)
			cerr.put(' ');
		
		auto begin_addr = &strm.toml_file[error_begin];
		auto end_addr = &strm.toml_file[error_end];

		auto view = uni::ranges::grapheme::utf8_view{ strm.toml_file };

		auto begin_found = false;
		auto end_found = false;

		const char* prev = {};
		
		auto count = std::size_t{};
		for (auto& ch : view)
		{
			if (!begin_found &&
				ch.data() >= begin_addr)
			{
				begin_found = true;
				error_begin = count;
			}

			if (!end_found &&
				ch.data() >= end_addr)
			{
				end_found = true;
				error_end = count;
			}

			++count;
			prev = ch.data();
		}

		count = {};

		for (auto& ch : view)
		{
			if (count == error_end - 1)
				cerr.put('^');
			else if (count < error_begin )
				cerr.put(' ');
			else if (count == error_begin)
				cerr.put('^');
			else if (count > error_end - 1)
				break;
			else
				cerr.put('~');

			++count;
		}

		return;
	}

	template<bool NoThrow>
	static index_t insert_child_table_array(index_t parent, std::string name, detail::toml_internal_data& d)
	{
		if (auto* node = &d.nodes[parent];
			node->child != bad_index)
		{
			auto child = node->child;
			node = &d.nodes[child];

			while (node)
			{
				if (unicode_string_equal(node->name, name))
				{
					if (node->type == node_type::array_tables)
					{
						parent = child;
						break;
					}
					else
					{
						const auto msg = "Attempted to redefine \""s + name +
							"\" as an array table; was previously defined as: "s + to_string(node->type) + ".\n"s;
						
						if constexpr (NoThrow)
						{
							// additional information is added by the calling func
							std::cerr << msg;
							insert_bad(d);
							return bad_index;
						}
						else
						{
							// additional information is added by the calling func
							throw duplicate_element{ msg, {}, {}, std::move(name) };
						}
					}
				}

				if (node->next == bad_index)
					break;
				child = node->next;
				node = &d.nodes[child];
			}

			//create array
			if (node->name != name)
			{
				auto n = internal_node{ std::move(name), node_type::array_tables };
				n.table_type = table_def_type::header;
				parent = insert_child<NoThrow>(d, parent, std::move(n));
				if constexpr (NoThrow)
				{
					if (parent == bad_index)
						return parent;
				}
			}
		}
		else
		{
			//create array as child
			auto n = internal_node{ std::move(name), node_type::array_tables };
			n.table_type = table_def_type::header;
			parent = insert_child<NoThrow>(d, parent, std::move(n));
			if constexpr (NoThrow)
			{
				if (parent == bad_index)
					return parent;
			}
		}

		auto& node = d.nodes[parent];
		assert(node.type == node_type::array_tables);

		// insert array member
		auto ret = insert_child_table<NoThrow>(parent, {}, d, table_def_type::header);		
		if constexpr (NoThrow)
		{
			if (ret == bad_index)
			{
				insert_bad(d);
				return bad_index;
			}
		}
		return ret;
	}

	// variant of unicode_u8_to_u32 that reads from parser_state
	static char32_t parse_unicode_char(char c, parser_state& strm) noexcept
	{
		if (!is_unicode_start(c))
			return unicode_error_char;

		int bytes = 1;
		if (c & 0b01000000)
		{
			++bytes;
			if (c & 0b00100000)
			{
				++bytes;
				if (c & 0b00010000)
					++bytes;
			}
		}

		assert(bytes >= 1);
		auto out = std::uint32_t{};
		switch (bytes)
		{
		case 2:
			out = c & 0b00011111;
			break;
		case 3:
			out = c & 0b00001111;
			break;
		case 4:
			out = c & 0b00000111;
			break;
		}

		while (bytes-- > 1)
		{
			auto [ch, eof] = strm.get_char<true>();
			if (eof || !is_unicode_continuation(ch))
				return unicode_error_char;
			out = out << 6; 
			out |= ch & 0b00111111;
		}

		if (valid_u32_code_point(out))
			return out;

		return unicode_error_char;
	}

	constexpr bool invalid_string_chars(char ch) noexcept
	{
		return ((ch >= 0 && ch < 32) || ch == 127) && ch != '\t';
	}

	template<bool NoThrow, bool DoubleQuoted>
	static std::optional<std::string> get_quoted_str(parser_state& strm)
	{
		constexpr auto delim = DoubleQuoted ? '\"' : '\'';
		auto out = std::string{};
		char ch;
		bool eof;
		const auto string_begin = strm.col - 1;
		constexpr auto missing_end_error_msg = "Quoted string missing end quote\n"sv;

		while (strm.strm.good())
		{
			try
			{
				std::tie(ch, eof) = strm.get_char<NoThrow>();
			}
			catch (const unexpected_eof&)
			{
				auto string = std::ostringstream{};
				string << missing_end_error_msg;
				print_error_string(strm, string_begin, strm.col, string);
				throw unexpected_eof{ string.str(), strm.line, strm.col };
			}

			if constexpr (NoThrow)
			{
				if (eof)
				{
					std::cerr << missing_end_error_msg;
					print_error_string(strm, string_begin, strm.col, std::cerr);
					return {};
				}
			}

			if (newline(strm, ch))
			{
				if constexpr (NoThrow)
				{
					std::cerr << missing_end_error_msg;
					print_error_string(strm, string_begin, error_entire_line, std::cerr);
					return {};
				}
				else
				{
					auto string = std::ostringstream{};
					string << missing_end_error_msg;
					print_error_string(strm, string_begin, error_entire_line, string);
					throw unexpected_character{ string.str(), strm.line, strm.col };
				}
			}

			if (ch == delim)
			{
				if constexpr (DoubleQuoted)
				{
					if (size(out) > 0)
					{
						//don't break on escaped quote: '\"'
						if (out.back() != '\\')
						{
							strm.putback(ch);
							break;
						}
						// but do break if escaped slash followed by quote: "\\""
						else if (size(out) > 2 && out[size(out) - 2] == '\\')
						{
							strm.putback(ch);
							break;
						}
					}
					else
					{
						strm.putback(ch);
						break;
					}
				}
				else 
				{
					strm.putback(ch);
					break;
				}
			}

			if (invalid_string_chars(ch))
			{
				const auto print_error = [&strm, ch](std::ostream& o) {
					o << "Unexpected character found in table/key name: \'\uFFFD\'.\n"s;
					print_error_string(strm, strm.col - 1, strm.col, o);
				};

				if constexpr (NoThrow)
				{
					print_error(std::cerr);
					return {};
				}
				else
				{
					const auto line = strm.line,
						col = strm.col;
					auto str = std::ostringstream{};
					print_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}
			else if (is_unicode_byte(ch))
			{
				const auto ch_index = strm.col - 1;
				const auto unicode = parse_unicode_char(ch, strm);

				if (unicode == unicode_error_char)
				{
					const auto print_error = [&strm, ch_index](std::ostream& o) {
						o << "Invalid unicode character in string: \'\uFFFD\'.\n"s;
						print_error_string(strm, ch_index, error_current_col, o);
					};

					if constexpr (NoThrow)
					{
						print_error(std::cerr);
						return {};
					}
					else
					{
						const auto line = strm.line,
							col = strm.col;
						auto str = std::ostringstream{};
						print_error(str);
						throw invalid_unicode_char{ str.str(), line, col };
					}
				}
				else
				{
					const auto u8 = unicode_u32_to_u8(unicode);
					out.append(u8);
					continue;
				}
			}

			out.push_back(ch);
		}
		
		return out;
	}
	
	static std::optional<std::string> get_unquoted_name(parser_state& strm, char ch)
	{
		if (!valid_key_name_char(ch))
		{
			strm.putback(ch);
			return {};
		}
			
		auto out = std::string{ ch };
		auto eof = false;
		while (strm.strm.good())
		{
			std::tie(ch, eof) = strm.get_char<true>();
			if (eof) return {};

			if (ch == ' ' ||
				ch == '\t' ||
				ch == '.' ||
				ch == '=' ||
				ch == ']')
			{
				strm.putback(ch);
				break;
			}

			if (!valid_key_name_char(ch))
			{
				strm.putback(ch);
				return {};
			}

			out.push_back(ch);
		}

		return out;
	}

	// Defined in another_toml_string_util.cpp
	template<bool NoThrow>
	std::optional<std::string> replace_escape_chars(std::string_view);

	extern template std::optional<std::string> replace_escape_chars<true>(std::string_view);
	extern template std::optional<std::string> replace_escape_chars<false>(std::string_view);

	std::string_view block_control(std::string_view s) noexcept
	{
		if (empty(s) || s[0] < 0x20)
			return "\ufffd"sv;
		else
			return s;
	}

	template<bool NoThrow, bool Table = false>
	static key_name parse_key_name(parser_state& strm, detail::toml_internal_data& d, std::size_t& key_char_begin)
	{
		auto name = std::optional<std::string>{};
		auto parent = root_table;
		if constexpr (!Table)
		{
			if(!empty(strm.stack))
				parent = strm.stack.back();
		}

		while (strm.strm.good())
		{
			// eof exception is given more context in the calling function.
			auto [ch, eof] = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
				{
					std::cerr << "Unexpected end-of-file in table/key name.\n";
					print_error_string(strm, strm.col - 1, strm.col - 1, std::cerr);
					insert_bad(d);
					return {};
				}
			}

			if (whitespace(ch, strm))
				continue;

			if (ch == '=' || ch == ']')
			{
				if (!name)
				{
					constexpr auto msg = "Missing name\n"sv;
					const auto name_begin = strm.col < 2 ? 0 : strm.col - 2;
					if constexpr (NoThrow)
					{
						std::cerr << msg;
						insert_bad(d);
						print_error_string(strm, name_begin, strm.col, std::cerr);
						return {};
					}
					else
					{
						auto string = std::ostringstream{};
						string << msg;
						print_error_string(strm, name_begin, strm.col, string);
						throw unexpected_character{ string.str(), strm.line, strm.col };
					}
				}

				strm.putback(ch);
				break;
			}

			if (ch == '\"')
			{
				if (name)
				{
					constexpr auto msg = "Unexpected character in key name: \"\n";

					if constexpr (NoThrow)
					{
						std::cerr << msg;
						print_error_string(strm, strm.col - 1, strm.col, std::cerr);
						return {};
					}
					else
					{
						auto str = std::ostringstream{};
						str << msg;
						print_error_string(strm, strm.col - 1, strm.col, str);
						throw unexpected_character{ str.str(), strm.line, strm.col };
					}
				}
				name = get_quoted_str<NoThrow, true>(strm);
				if constexpr (NoThrow)
				{
					if (!name)
					{
						insert_bad(d);
						return {};
					}
				}

				assert(name);
				name = replace_escape_chars<NoThrow>(*name);

				if (!name)
				{
					if constexpr (NoThrow)
					{
						insert_bad(d);
						return {};
					}
				}

				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if constexpr (NoThrow)
				{
					if (eof || ch != '\"')
					{
						std::cerr << "Unexpected end of quoted string: "s << name.value_or("\"\""s) << '\n';
						insert_bad(d);
						return {};
					}
				}
				
				continue;
			}
			else if(ch == '\'')
			{
				if (name)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Illigal character in name: \'\n";
						insert_bad(d);
						return {};
					}
					else
						throw unexpected_character{ "Illigal character in name: \'" };
				}
				name = get_quoted_str<NoThrow, false>(strm);
				if constexpr (NoThrow)
				{
					if(!name)
					{
						insert_bad(d);
						return {};
					}
				}

				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if constexpr (NoThrow)
				{
					if (eof || ch!= '\'')
					{
						insert_bad(d);
						return{};
					}
				}
				
				continue;
			}
			else if (ch == '.')
			{
				if (!name)
				{
					constexpr auto msg = "Missing name\n"sv;
					const auto name_begin = strm.col < 2 ? 0 : strm.col - 2;
					if constexpr (NoThrow)
					{
						insert_bad(d);
						std::cerr << msg;
						print_error_string(strm, name_begin, strm.col, std::cerr);
						return {};
					}
					else
					{
						auto string = std::ostringstream{};
						string << msg;
						print_error_string(strm, name_begin, strm.col, string);
						throw unexpected_character{ string.str(), strm.line, strm.col };
					}
				}
				else
				{
					auto child = find_child(d, parent, *name);
					
					if (child == bad_index)
						child = insert_child_table<NoThrow>(parent, std::move(*name), d, table_def_type::dotted);
					else if(auto& c = d.nodes[child]; 
						c.type == node_type::array_tables)
					{
						if constexpr (!Table)
						{
							if constexpr (NoThrow)
							{
								std::cerr << "Name heirarchy for keys shouldn't include table arrays"s;
								insert_bad(d);
								return {};
							}
							else
								throw toml_error{ "Name heirarchy for keys shouldn't include table arrays"s };
						}

						child = c.child;
						while (true)
						{
							auto* node = &d.nodes[child];
							if (node->next == bad_index)
								break;
							child = node->next;
						}
					}
					else if (c.closed && c.table_type == table_def_type::header)
					{
						if constexpr(!Table)
						{
							const auto write_error = [&strm, &c](std::ostream& o) {
								const auto msg = "Attempted to add to a previously defined table: \"" + c.name +
									"\" using dotted keys.\n"s;
								const auto name_end = strm.col - 1;
								const auto name_beg = name_end - size(c.name);
								o << msg;
								print_error_string(strm, name_beg, name_end, o);
								return;
							};

							if constexpr (NoThrow)
							{
								write_error(std::cerr);
								insert_bad(d);
								return {};
							}
							else
							{
								auto string = std::ostringstream{};
								write_error(string);
								throw duplicate_element{ string.str(), strm.line, strm.col, c.name };
							}
						}
						// fall out of if
					}
					else if (!(c.type == node_type::table || c.type == node_type::array_tables))
					{
						const auto write_error = [&strm, &c](std::ostream& o) {
							const auto msg = "Attempted to redefine \""s + c.name +
								"\" as a table using dotted keys. Was previously defined as: \""s +
								to_string(c.type) + "\".\n"s;
							const auto name_end = strm.col - 1;
							const auto name_beg = name_end - size(c.name);
							o << msg;
							print_error_string(strm, name_beg, name_end, o);
							return;
						};

						if constexpr (NoThrow)
						{
							write_error(std::cerr);
							insert_bad(d);
							return {};
						}
						else
						{
							auto string = std::ostringstream{};
							write_error(string);
							throw duplicate_element{ string.str(), strm.line, strm.col, c.name };
						}
					}

					parent = child;
					name = {};
					key_char_begin = strm.col;
					continue;
				}
			}

			const auto handle_character_error = [&strm, &d] {
				// eof exception is given more context in the calling function.
				const auto [ch, eof] = strm.get_char<NoThrow>();
				if constexpr (NoThrow)
				{
					if (eof)
					{
						std::cerr << "Unexpected end-of-file in table/key name.\n";
						print_error_string(strm, strm.col - 1, strm.col, std::cerr);
						insert_bad(d);
						return;
					}
				}

				const auto ch_index = strm.col - 1;
				const auto print_error = [&strm, ch_index](std::ostream& o) {
					parse_line(strm);
					const auto str = std::string_view{ &strm.toml_file[ch_index] };
					auto graph_rng = uni::ranges::grapheme::utf8_view{ str };
					o << "Unexpected character found in table/key name: \'"s <<
						block_control(*begin(graph_rng)) <<
						"\'.\n"s;
					print_error_string(strm, ch_index, ch_index + 1, o);
				};

				if constexpr (NoThrow)
				{
					print_error(std::cerr);
					insert_bad(d);
					return;
				}
				else
				{
					const auto line = strm.line,
						col = ch_index;
					auto str = std::ostringstream{};
					print_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			};

			if (!name)
			{
				auto str = get_unquoted_name(strm, ch);
				if (str)
					name = str;
				else
				{
					handle_character_error();
					return {};
				}
			}
			else
			{
				strm.putback();
				strm.putback();
				handle_character_error();
				return {};
			}
		}

		return { parent, name };
	}

	using get_value_type_ret = std::tuple<value_type, variant_t, std::string>;

	// access func from string_util.cpp
	std::string remove_underscores(std::string_view sv);

	template<bool NoThrow>
	extern std::variant<std::monostate, date, time, date_time, local_date_time> parse_date_time_ex(std::string_view str) noexcept(NoThrow);

	template<bool NoThrow>
	static get_value_type_ret get_value_type(std::string_view str) noexcept(NoThrow)
	{
		if (empty(str))
		{
			if constexpr (NoThrow)
			{

				return { value_type::bad, {}, {} };
			}
			else
				throw parsing_error{ "Error parsing value.\n" };
		}

		//keywords
		//	true, false
		if (str == "true"sv)
			return { value_type::boolean, true, std::string{ str } };
		if (str == "false"sv)
			return { value_type::boolean, false, std::string{ str } };
		
		const auto beg = begin(str);
		const auto end = std::end(str);

		// matches all valid integers with optional underscores after the first digit
		//  -normal digits(with no leading 0)
		//	-hex
		//	-binary
		//	-oct
		// doesn't check for min/max value
		constexpr auto int_reg =
			R"(^[\+\-]?[1-9]+(_?([\d])+)*$|^0x[\dA-Fa-f]+(_?[\dA-Fa-f]+)*|0b[01]+(_?[01]+)*|0o[0-7]+(_?([0-7])+)*|^[\+\-]?0$)";

		if (std::regex_match(beg, end, std::regex{ int_reg }))
		{
			auto string = remove_underscores(str);
			
			auto base = 10;
			auto base_en = int_base::dec;
			if (size(string) > 1)
			{
				const auto base_chars = std::string_view{ &*begin(string), 2 };
				if (base_chars == "0x"sv)
				{
					base = 16;
					base_en = int_base::hex;
				}
				else if (base_chars == "0b"sv)
				{
					base = 2;
					base_en = int_base::bin;
				}
				else if (base_chars == "0o"sv)
				{
					base = 8;
					base_en = int_base::oct;
				}

				if (base != 10)
					string.erase(0, 2);
			}
			
			//convert to int
			auto int_val = std::int64_t{};
			const auto string_end = &string[0] + size(string);
			auto ret = std::from_chars(&string[0], string_end, int_val, base);

			if (ret.ptr == string_end)
			{
				//str = std::to_string(int_val);
				return { value_type::integer, detail::integral{int_val, base_en}, std::to_string(int_val) };
			}
			else if (ret.ec == std::errc::invalid_argument)
			{
				if constexpr (NoThrow)
				{
					return { value_type::bad, {}, {} };
				}
				else
				{
					const auto pos = std::distance(data(str), ret.ptr);
					assert(pos >= 0);
					throw parsing_error{ "Failed to parse integer value\n"s, {}, static_cast<std::size_t>(pos) };
				}
			}
			else if (ret.ec == std::errc::result_out_of_range)
			{
				if constexpr (NoThrow)
				{
					return { value_type::out_of_range, {}, {} };
				}
				else
					throw parsing_error{ "Integer value out of storable range\n"s, };
			}
		}
		
		using error_t = parse_float_string_return::error_t;
		const auto float_ret = parse_float_string(str);
		if (float_ret.error == error_t::none)
			return { value_type::floating_point, floating{ float_ret.value, float_ret.representation }, std::string{str} };
		else if (float_ret.error == error_t::out_of_range)
		{
			if constexpr (NoThrow)
			{
				return { value_type::out_of_range, {}, {} };
			}
			else
				throw parsing_error{ "Foating point value outside storable range.\n"s };
		}


		const auto ret = parse_date_time_ex<NoThrow>(str);
		return std::visit([str](auto&& val)->get_value_type_ret {
			using T = std::decay_t<decltype(val)>;
		if constexpr (std::is_same_v<date_time, T>)
			return { value_type::date_time, val, std::string{str} };
		else if constexpr (std::is_same_v<local_date_time, T>)
			return { value_type::local_date_time, val, std::string{str} };
		else if constexpr (std::is_same_v<date, T>)
			return { value_type::local_date, val, std::string{str} };
		else if constexpr (std::is_same_v<time, T>)
			return { value_type::local_time, val, std::string{str} };
		else
			return { value_type::bad, {}, {} };
		}, ret);
	}

	struct normal_tag_t {};
	struct array_tag_t {};
	struct inline_tag_t {};

	// for parsing keywords, dates or numerical values
	template<bool NoThrow, typename Tag>
	static bool parse_unquoted_value(parser_state& strm, detail::toml_internal_data& toml_data)
	{
		auto out = std::string{};
		auto ch = char{};
		auto ch_index = strm.col;
		auto eof = bool{};
		const auto parent = strm.stack.back();
		const auto parent_type = toml_data.nodes[parent].type;
		constexpr auto array = std::is_same_v<Tag, array_tag_t>;
		constexpr auto inline_table = std::is_same_v<Tag, inline_tag_t>;
		assert((parent_type == node_type::array) == array);

		while (strm.strm.good())
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					break;
			}

			if (ch == '#')
			{
				strm.putback(ch);
				break;
			}

			if constexpr (array || inline_table)
			{
				if (ch == ',')
				{
					strm.putback(ch);
					break;
				}

				if constexpr (inline_table)
				{
					if (ch == '}')
					{
						strm.putback(ch);
						break;
					}
				}

				if constexpr (array)
				{
					if (ch == ']')
					{
						strm.putback(ch);
						break;
					}
				}

				if (newline(strm, ch))
				{
					const auto write_error = [&strm, ch_index](std::ostream& o) {
						if constexpr (std::is_same_v<Tag, array_tag_t>)
							o << "Unexpected newline in array element.\n"s;
						else
							o << "Unexpected newline in inline table value.\n"s;
						print_error_string(strm, strm.col, strm.col, o);
						return;
					};

					if constexpr (NoThrow)
					{
						write_error(std::cerr);
						insert_bad(toml_data);
						return false;
					}
					else
					{
						auto str = std::ostringstream{};
						const auto line = strm.line, col = ch_index;
						write_error(str);
						throw parsing_error{ str.str(), line, col };
					}
				}
			}
			else if (newline(strm, ch))
			{
				strm.putback(ch);
				break;
			}

			out.push_back(ch);
		}

		while (!empty(out) && (out.back() == '\t' || out.back() == ' '))
			out.pop_back();

		if (empty(out))
		{
			const auto write_error = [&strm, ch_index](std::ostream& o) {
				o << "A value was expected here.\n"s;
				print_error_string(strm, ch_index, ch_index + 1, o);
				return;
			};

			if constexpr (NoThrow)
			{
				write_error(std::cerr);
				insert_bad(toml_data);
				return false;
			}
			else
			{
				auto str = std::ostringstream{};
				const auto line = strm.line, col = ch_index;
				write_error(str);
				throw parsing_error{ str.str(), line, col };
			}
		}

		try
		{
			auto [type, value, string] = get_value_type<NoThrow>(out);
		
			if constexpr (NoThrow)
			{
				if (type == value_type::bad)
				{
					std::cerr << "Error parsing value.\n";
					print_error_string(strm, ch_index, strm.col, std::cerr);
					insert_bad(toml_data);
					return false;
				}

				if (type == value_type::out_of_range)
				{
					const auto write_error = [&strm, ch_index](std::ostream& o) {
						o << "Value is outside the range this type can store.\n"s;
						print_error_string(strm, ch_index, strm.col, o);
						return;
					};

					if constexpr (NoThrow)
					{
						write_error(std::cerr);
						insert_bad(toml_data);
						return false;
					}
				}
			}

			strm.token_stream.push_back(token_type::value);
			return insert_child<NoThrow>(toml_data, parent, 
				internal_node{
					std::move(string), node_type::value,
					type, std::move(value)
				}) != bad_index;

		}
		catch (parsing_error& e)
		{
			auto str = std::ostringstream{};
			str << e.what();
			const auto line = strm.line, col = ch_index;
			print_error_string(strm, col, strm.col, str);
			throw parsing_error{ str.str(), line, col };
		}
	}

	template<bool NoThrow, bool DoubleQuote>
	static std::optional<std::string> multiline_string(parser_state& strm)
	{
		constexpr char quote_char = DoubleQuote ? '\"' : '\'';

		// opening quotes have already been collected
		auto [ch, eof] = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
				return {};
		}

		// consume the first newline after '''/"""
		if (newline(strm, ch))
			strm.nextline();
		else
			strm.putback(ch);

		auto str = std::string{};

		while (strm.strm.good())
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return {};
			}

			// implements '\' behaviour for double quoted strings
			if constexpr (DoubleQuote)
			{
				if(ch == '\\')
				{
					auto peek = strm.strm.peek();
					if (peek == '\\')
					{
						strm.ignore();
						str += "\\\\"s;
						continue;
					}

					if (peek == '\n' ||
						peek == ' ' ||
						peek == '\t')
					{
						const auto line = strm.line;
						while (strm.strm.good())
						{
							std::tie(ch, eof) = strm.get_char<NoThrow>();
							if constexpr (NoThrow)
							{
								if (eof)
									return {};
							}

							if (whitespace(ch, strm))
							{
								; // do nothing
							}
							else if (newline(strm, ch))
								strm.nextline();
							else
							{
								strm.putback(ch);
								break;
							}
						}

						if (line == strm.line)
						{
							const auto print_error = [&strm, ch](std::ostream& o) {
								o << "Illigal character following '\\' line break in multiline string: \'"s << ch << "\'.\n"s;
								print_error_string(strm, strm.col - 1, strm.col, o);
							};

							if constexpr (NoThrow)
							{
								print_error(std::cerr);
								return {};
							}
							else
							{
								const auto line = strm.line,
									col = strm.col;
								auto str = std::ostringstream{};
								print_error(str);
								throw unexpected_character{ str.str(), line, col };
							}
						}

						continue;
					}
				} // ! '\'
			} // !doublequote

			if (size(str) > 2)
			{
				auto rb = rbegin(str);
				const auto re = rend(str);
				while (rb != re && *rb == quote_char)
				{
					if constexpr (DoubleQuote)
					{
						auto next = std::next(rb);
						if (next != re && *next == '\\')
							break;
					}

					++rb;
				}

				const auto dist = distance(rbegin(str), rb) + (ch == quote_char ? 1 : 0);
				const auto end = dist > 2 && dist < 6;
				if (end && ch != quote_char)
				{
					strm.putback(ch);
					return std::string{ begin(str), next(begin(str), size(str) - 3) };
				}
				else if (dist > 5 && ch != quote_char)
				{
					auto seq_begin = strm.col - distance(rbegin(str), rb) - 1;
					const auto print_error = [&strm, seq_begin](std::ostream& o) {
						o << "Invalid sequence in multiline string.\n"s;
						print_error_string(strm, seq_begin, error_current_col, o);
					};

					if constexpr (NoThrow)
					{
						print_error(std::cerr);
						return {};
					}
					else
					{
						const auto line = strm.line,
							col = strm.col;
						auto str = std::ostringstream{};
						print_error(str);
						throw parsing_error{ str.str(), line, col };
					}
				}
			}

			if (invalid_string_chars(ch) && ch != '\n')
			{
				const auto print_error = [&strm](std::ostream& o) {
					o << "Unexpected character in multiline string: \'\uFFFD\'.\n"s;
					print_error_string(strm, strm.col - 1, strm.col, o);
				};

				if constexpr (NoThrow)
				{
					print_error(std::cerr);
					return {};
				}
				else
				{
					const auto line = strm.line,
						col = strm.col;
					auto str = std::ostringstream{};
					print_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}

			str.push_back(ch);
		}

		if constexpr (NoThrow)
		{
			return {};
		}
		else
			throw parsing_error{ "Error parsing multiline string"s };
	}

	template<bool NoThrow, bool DoubleQuote>
	static bool parse_str_value(parser_state& strm, detail::toml_internal_data& toml_data)
	{
		constexpr char quote_char = DoubleQuote ? '\"' : '\'';
		auto str = std::optional<std::string>{};
		if (strm.strm.peek() == quote_char)
		{
			//quoted str was empty, or start of multiline string
			strm.ignore();
			if (strm.strm.peek() == quote_char)
			{
				const auto str_start = strm.col - 2;
				strm.ignore();
				str = multiline_string<NoThrow, DoubleQuote>(strm);
				if (!str)
					return false;

				if constexpr (DoubleQuote)
				{
					try
					{
						str = replace_escape_chars<NoThrow>(*str);
						if (!str)
						{
							print_error_string(strm, str_start, strm.col, std::cerr);
							insert_bad(toml_data);
							return false;
						}
					}
					catch (const unicode_error& e)
					{
						auto string = std::ostringstream{};
						string << e.what() << '\n';
						print_error_string(strm, str_start, strm.col, string);
						throw unicode_error{ string.str(), strm.line, strm.col };
					}
				}
			}
			else
				str = std::string{};
		}
		else
		{
			//start normal quote str
			const auto str_start = strm.col - 1;
			str = get_quoted_str<NoThrow, DoubleQuote>(strm);
			if constexpr (NoThrow)
			{
				if (!str)
				{
					insert_bad(toml_data);
					return false;
				}
			}

			if constexpr (DoubleQuote)
			{
				try
				{
					str = replace_escape_chars<NoThrow>(*str);
					if (!str)
					{
						print_error_string(strm, str_start, strm.col + 1, std::cerr);
						insert_bad(toml_data);
						return false;
					}
				}
				catch (const unicode_error& e)
				{
					auto string = std::ostringstream{};
					string << e.what() << '\n';
					print_error_string(strm, str_start, strm.col + 1, string);
					throw unicode_error{ string.str(), strm.line, strm.col};
				}
			}

			if (strm.strm.peek() == quote_char)
				strm.ignore();
			else
			{
				constexpr auto msg = "Unexpected error in quoted string.\n";
				if constexpr (NoThrow)
				{
					std::cerr << msg;
					print_error_string(strm, str_start, strm.col + 1, std::cerr);
					return false;
				}
				else
				{
					auto string = std::ostringstream{};
					print_error_string(strm, str_start, strm.col + 1, string);
					throw unicode_error{ string.str(), strm.line, strm.col };
				}
			}
		}

		assert(!strm.stack.empty());
		assert(str);
		insert_child<NoThrow>(toml_data, strm.stack.back(), internal_node{ std::move(*str), node_type::value, value_type::string, string_t{ !DoubleQuote } });
		strm.token_stream.emplace_back(token_type::value);

		return true;
	}

	// consumes the next endline if present
	template<bool NoThrow>
	static bool parse_comment(parser_state& strm) noexcept(NoThrow)
	{
		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<true>();
			if (eof)
				break;

			if (newline(strm, ch))
			{
				strm.nextline();
				break;
			}

			if (comment_forbidden_char(ch))
			{
				const auto write_error = [&strm](std::ostream& o) {
					o << "Forbidden character in comment: \'";
					const auto ch_index = strm.col - 1;
					parse_line(strm);
					const auto str = std::string_view{ &strm.toml_file[ch_index] };
					auto graph_rng = uni::ranges::grapheme::utf8_view{ str };
					o << block_control(*begin(graph_rng)) << "\'.\n";
					print_error_string(strm, ch_index, ch_index + 1, o);
					return;
				};

				if constexpr (NoThrow)
				{
					write_error(std::cerr);
					return false;
				}
				else
				{
					auto str = std::ostringstream{};
					const auto line = strm.line, col = strm.col - 1;
					write_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}

			if (is_unicode_byte(ch))
			{
				const auto ch_index = strm.col - 1;
				auto u_ch = parse_unicode_char(ch, strm);
				if (u_ch == unicode_error_char)
				{
					const auto write_error = [&strm, ch_index](std::ostream& o) {
						o << "Invalid unicode character(s) in stream: \'\uFFFD.\'\n";
						print_error_string(strm, ch_index, error_current_col, o);
						return;
					};

					if constexpr (NoThrow)
					{
						write_error(std::cerr);
						return false;
					}
					else
					{
						auto str = std::ostringstream{};
						const auto line = strm.line, col = strm.col - 1;
						write_error(str);
						throw invalid_unicode_char{ str.str(), line, col };
					}
				}
			}
		}

		return true;
	}

	template<bool NoThrow, typename Tag>
	static bool parse_value(parser_state&, detail::toml_internal_data&);

	template<bool NoThrow>
	static bool parse_array(parser_state& strm, detail::toml_internal_data& toml_data)
	{
		assert(!empty(strm.stack));
		auto arr = insert_child<NoThrow>(toml_data, strm.stack.back(), internal_node{
				{}, node_type::array
			});

		strm.token_stream.emplace_back(token_type::array);
		strm.stack.emplace_back(arr);

		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
				{
					std::cerr << "Encountered an unexpected eof.\n";
					print_error_string(strm, strm.col, error_entire_line, std::cerr);
					return false;
				}
			}

			if (whitespace(ch, strm))
				continue;

			if (newline(strm, ch))
			{
				strm.nextline();
				continue;
			}

			if (ch == ']')
			{
				strm.token_stream.emplace_back(token_type::array_end);
				strm.stack.pop_back();
				return true;
			}
			
			if (ch == ',')
			{
				if (strm.token_stream.back() != token_type::value)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Unexpected comma in array element.\n";
						print_error_string(strm, strm.col, error_entire_line, std::cerr);
						return false;
					}
					else
					{
						auto str = std::ostringstream{};
						const auto line = strm.line, col = strm.col;
						str << "Unexpected comma in array element.\n";
						print_error_string(strm, col - 1, col, str);
						throw unexpected_character{ str.str(), strm.line, strm.col};
					}
				}

				strm.token_stream.emplace_back(token_type::comma);
				continue;
			}

			if (ch == '#')
			{
				if (!parse_comment<NoThrow>(strm))
				{
					if constexpr (NoThrow)
					{
						insert_bad(toml_data);
						return false;
					}
				}

				continue;
			}

			//get value
			strm.putback(ch);
			if (!parse_value<NoThrow, array_tag_t>(strm, toml_data))
				return false;
			
			continue;
		}

		if constexpr (NoThrow)
		{
			std::cerr << "Stream error while parsing array\n";
			return false;
		}
		else 
			throw toml_error{ "Stream error while parsing array"s };
	}

	template<bool NoThrow>
	static bool parse_inline_table(parser_state& strm, toml_internal_data& toml_data)
	{
		// push back an inline table
		assert(!empty(strm.stack));
		const auto parent = strm.stack.back();
		const auto& p = toml_data.nodes[parent];
		assert(p.type == node_type::key || p.type == node_type::array);
		const auto table = insert_child<NoThrow>(toml_data, parent, internal_node{ p.name, node_type::inline_table });
		strm.stack.emplace_back(table);
		strm.token_stream.emplace_back(token_type::inline_table);
		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return false;
			}

			if (whitespace(ch, strm))
				continue;

			if (newline(strm, ch))
			{
				const auto write_error = [&strm](std::ostream& o) {
					o << "Illigal newline in inline table\n";
					print_error_string(strm, strm.col, strm.col, o);
					return;
				};

				if constexpr (NoThrow)
				{
					write_error(std::cerr);
					insert_bad(toml_data);
					return false;
				}
				else
				{
					auto str = std::ostringstream{};
					const auto line = strm.line, col = strm.col;
					write_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}

			if (ch == '}')
			{
				if (strm.token_stream.back() == token_type::comma)
				{
					const auto write_error = [&strm](std::ostream& o) {
						o << "Trailing comma is forbidden in inline tables\n"s;
						print_error_string(strm, strm.col, strm.col, o);
						return;
					};

					if constexpr (NoThrow)
					{
						write_error(std::cerr);
						insert_bad(toml_data);
						return false;
					}
					else
					{
						auto str = std::ostringstream{};
						const auto line = strm.line, col = strm.col;
						write_error(str);
						throw unexpected_character{ str.str(), line, col };
					}
				}

				toml_data.nodes[table].closed = true;
				assert(table == strm.stack.back());
				strm.stack.pop_back();
				return true;
			}

			if (const auto back = strm.token_stream.back();
				!(back == token_type::inline_table || back == token_type::comma))
			{
				const auto write_error = [&strm](std::ostream& o) {
					o << "Expected comma or end of inline table\n"s;
					print_error_string(strm, strm.col + 1, error_entire_line, o);
					return;
				};

				if constexpr (NoThrow)
				{
					write_error(std::cerr);
					insert_bad(toml_data);
					return false;
				}
				else
				{
					auto str = std::ostringstream{};
					const auto line = strm.line, col = strm.col;
					write_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}

			strm.putback(ch);
			if (!parse_key_value<NoThrow, inline_tag_t>(strm, toml_data))
				return false;

			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return false;
			}

			if(whitespace(ch, strm))
			{
				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if constexpr (NoThrow)
				{
					if (eof)
						return false;
				}
			}

			//next must be ',' or '}'
			if (ch == ',')
			{
				strm.token_stream.emplace_back(token_type::comma);
				continue;
			}
			strm.putback(ch);
		}

		return true;
	}

	template<bool NoThrow, typename Tag>
	static bool parse_key_value(parser_state& strm, toml_internal_data& toml_data)
	{
		static_assert(std::is_same_v<Tag, normal_tag_t> || std::is_same_v<Tag, inline_tag_t>);
		auto key_name_begin = strm.col;
		auto key_str = key_name{};

		try 
		{
			key_str = parse_key_name<NoThrow>(strm, toml_data, key_name_begin);
		}
		catch (const unexpected_eof& err)
		{
			auto str = std::ostringstream{};
			str << "Unexpected end-of-file in table/key name.\n";
			print_error_string(strm, strm.col - 1, strm.col, str);

			throw unexpected_eof{ str.str(), err.line(), err.column() };
		}

		if constexpr (NoThrow)
		{
			if (!key_str.name)
				return false;
		}

		auto [ch, eof] = strm.get_char<NoThrow>();

		if constexpr (NoThrow)
		{
			if (eof)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
		}

		if (toml_data.nodes[key_str.parent].closed == false)
			strm.open_tables.emplace_back(key_str.parent);

		auto key = detail::internal_node{ std::move(*key_str.name), node_type::key };
		try
		{
			const auto key_index = insert_child<NoThrow>(toml_data, key_str.parent, std::move(key));

			if constexpr (NoThrow)
			{
				if (key_index == bad_index)
				{
					strm.putback();
					print_error_string(strm, key_name_begin, error_current_col, std::cerr);
					return false;
				}
			}

			strm.stack.emplace_back(key_index);
			strm.token_stream.emplace_back(token_type::key);
		}
		catch (duplicate_element& e)
		{
			auto str = std::ostringstream{};
			str << e.what();
			strm.putback();
			print_error_string(strm, key_name_begin, error_current_col, str);
			throw duplicate_element{ str.str(), strm.line, key_name_begin, e.name() };
		}

		if (whitespace(ch, strm))
			std::tie(ch, eof) = strm.get_char<NoThrow>();

		if constexpr (NoThrow)
		{
			if (eof)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
		}

		if (ch != '=')
		{
			if constexpr (NoThrow)
			{
				insert_bad(toml_data);
				std::cerr << "key names must be followed by '='"s;
				return false;
			}
			else
				throw unexpected_character{ "key names must be followed by '='"s };
		}

		std::tie(ch, eof) = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting value\n"s;
				return false;
			}
		}

		if (!whitespace(ch, strm))
			strm.putback(ch);

		const auto ret = parse_value<NoThrow, Tag>(strm, toml_data);
		if constexpr (NoThrow)
		{
			if (!ret)
			{
				insert_bad(toml_data);
				return false;
			}
		}

		return true;
	}

	template<bool NoThrow, typename Tag>
	static bool parse_value(parser_state& strm, detail::toml_internal_data& toml_data)
	{
		//whitespace has already been consumed before here
		auto [ch, eof] = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
				return false;
		}

		auto ret = false;
		
		if (ch == '[')
			ret = parse_array<NoThrow>(strm, toml_data);
		else if (ch == '{')
			ret = parse_inline_table<NoThrow>(strm, toml_data);
		else if (ch == '\"')
			ret = parse_str_value<NoThrow, true>(strm, toml_data);
		else if (ch == '\'')
			ret = parse_str_value<NoThrow, false>(strm, toml_data);
		else
		{
			strm.putback(ch);
			ret = parse_unquoted_value<NoThrow, Tag>(strm, toml_data);
			if constexpr (NoThrow)
			{
				if (!ret)
					return false;
			}
		}

		if(ret)
		{
			strm.token_stream.emplace_back(token_type::value);
			const auto parent_type = toml_data.nodes[strm.stack.back()].type;
			if (parent_type == node_type::key)
				strm.stack.pop_back();
		}

		return ret;
	}

	template<bool NoThrow, bool Array>
	static index_t parse_table_header(parser_state& strm, toml_internal_data& toml_data)
	{
		assert(toml_data.nodes[strm.stack.back()].type == node_type::table ||
			toml_data.nodes[strm.stack.back()].type == node_type::array_tables);
		strm.stack.pop_back();
		strm.close_tables(toml_data);

		auto key_name_begin = strm.col;
		auto name = key_name{};

		try
		{
			name = parse_key_name<NoThrow, true>(strm, toml_data, key_name_begin);
		}
		catch (const unexpected_eof& err)
		{
			auto str = std::ostringstream{};
			str << "Unexpected end-of-file in table name.\n";
			print_error_string(strm, strm.col - 1, strm.col, str);

			throw unexpected_eof{ str.str(), err.line(), err.column() };
		}

		if constexpr (NoThrow)
		{
			if (!name.name)
				return bad_index;
		}
		
		auto [ch, eof] = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
				return bad_index;
		}

		if (whitespace(ch, strm))
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return bad_index;
			}
		}

		if (ch != ']')
		{
			const auto ch_index = strm.col - 1;
			parse_line(strm);
			const auto str = std::string_view{ &strm.toml_file[ch_index] };
			auto graph_rng = uni::ranges::grapheme::utf8_view{ str };
			const auto msg = "Unexpected character following table name: \'"s +
				std::string{ block_control(*begin(graph_rng)) } +
				"\'; was expecting \']\'\n"s;
			if constexpr (NoThrow)
			{
				std::cerr << msg;
				print_error_string(strm, ch_index, ch_index + 1, std::cerr);
				insert_bad(toml_data);
				return bad_index;
			}
			else
			{
				auto string = std::ostringstream{};
				string << msg;
				print_error_string(strm, ch_index, ch_index + 1, string);
				throw unexpected_character{ string.str(), strm.line, ch_index };
			}
		}

		if constexpr (Array)
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return bad_index;
			}

			if (ch != ']')
			{
				const auto ch_index = strm.col - 1;
				const auto write_error = [&strm, ch_index](std::ostream& o) {
					o << "Unexpected character while parsing array table header : \'"s;
					// split the first grapheme off from the current location and draw it.
					parse_line(strm);
					const auto str = std::string_view{ &strm.toml_file[ch_index] };
					auto graph_rng = uni::ranges::grapheme::utf8_view{ str };
					o << block_control(*begin(graph_rng));
					o << "\', was expecting ']'.\n";
					print_error_string(strm, ch_index, ch_index + 1, o);
					return;
				};

				if constexpr (NoThrow)
				{
					write_error(std::cerr);
					insert_bad(toml_data);
					return bad_index;
				}
				else
				{
					auto str = std::ostringstream{};
					const auto line = strm.line, col = ch_index;
					write_error(str);
					throw unexpected_character{ str.str(), line, col };
				}
			}
		}

		if (!name.name)
		{
			if constexpr (NoThrow)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting table name\n";
				return bad_index;
			}
			else
				throw toml_error{ "Error getting table name" };
		}

		auto& parent = name.parent;
		auto table = bad_index;
		try
		{
			if constexpr (Array)
			{
				// error is handled after the catch block
				table = insert_child_table_array<NoThrow>(name.parent, std::move(*name.name), toml_data);
				strm.token_stream.emplace_back(token_type::array_table);
			}
			else
			{
				table = find_child(toml_data, name.parent, *name.name);
				if (table == bad_index)
					table = insert_child_table<NoThrow>(name.parent, std::move(*name.name), toml_data, table_def_type::header);
				const auto type = toml_data.nodes[table].type;
				if (type != node_type::table)
				{
					const auto msg = "Attempted to redefine \""s + *name.name +
						"\" as a table; was previously defined as: "s + to_string(type) + ".\n"s;
					// type redifinition
					if constexpr (NoThrow)
					{
						std::cerr << msg;
						print_error_string(strm, key_name_begin, error_current_col, std::cerr);
						return bad_index;
					}
					else
						throw duplicate_element{ msg, {}, {}, *name.name };
				}
				else if (toml_data.nodes[table].closed)
				{
					const auto msg = "Attepted to reopen table: \""s + toml_data.nodes[table].name +
						"\", but this table has already been defined.\n"s;

					if constexpr (NoThrow)
					{
						std::cerr << msg;
						print_error_string(strm, key_name_begin, error_current_col, std::cerr);
						insert_bad(toml_data);
						return bad_index;
					}
					else
						throw duplicate_element{ msg, {}, {}, *name.name };
				}

				strm.token_stream.emplace_back(token_type::table);
			}
		}
		// NOTE: we have to catch this here becuase insert_child_table_array can throw 
		// duplicate element, that function doesn't have line information, so we need
		// to add it here, as a result, the other throws in the try block end up here, 
		// even though they could otherwise have been able to go through to the calling
		// func.
		catch (duplicate_element& err)
		{
			// add missing line/column information to exception
			auto str = std::ostringstream{};
			const auto line = strm.line, col = strm.col;
			constexpr auto end_offset = Array ? 2 : 1;
			print_error_string(strm, key_name_begin, end_offset, str);
			throw duplicate_element{ err.what() + str.str(), line, col, err.name() };
		}

		if constexpr (NoThrow)
		{
			if (table == bad_index)
			{
				constexpr auto end_offset = Array ? 2 : 1;
				print_error_string(strm, key_name_begin, strm.col - end_offset, std::cerr);
				return bad_index;
			}			
		}

		strm.stack.emplace_back(table);
		strm.open_tables.emplace_back(table);

		std::tie(ch, eof) = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
				return table;
		}

		if (whitespace(ch, strm))
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return table;
			}
		}

		if (ch == '#')
		{
			if (!parse_comment<NoThrow>(strm))
			{
				if constexpr (NoThrow)
				{
					insert_bad(toml_data);
					return bad_index;
				}
			}
		}
		else if (!newline(strm, ch))
		{
			auto str = std::ostringstream{};
			str << "Unexpected character after table header.\n"s;
			const auto line = strm.line, col = strm.col;
			
			// Subtract 2 from the current coloumn position to get back to 
			// the first char after the header close ']' in the event of
			//	[table] error
			// only take 1 in the case of
			//	[table]error
			const auto error_begin = *strm.toml_file.rbegin() == ' ' ? strm.col - 2 : strm.col - 1;
			print_error_string(strm, error_begin, error_entire_line, str);

			if constexpr (NoThrow)
			{
				std::cerr << str.str();
				insert_bad(toml_data);
				return bad_index;
			}
			else
				throw unexpected_character{ str.str(), line, col };
		}
		else
			strm.nextline();

		return table;
	}

	template<bool NoThrow>
	static root_node parse_toml(std::istream& strm)
	{
		auto toml_data = root_node::data_type{ new detail::toml_internal_data{} };
		auto& t = toml_data->nodes;
		auto p_state = parser_state{ strm };

		// consume the BOM if it is present
		for (auto bom : utf8_bom)
		{
			if (strm.peek() == bom)
				strm.ignore();
			else
				break;
		}

		// implicit global table
		// always stored at index 0
		p_state.stack.emplace_back(root_table);
		p_state.open_tables.emplace_back(root_table);
		
		//parse
		//[tables]
		//keys
		//[[arrays of tables]]
		//#comments
		//
		//	inline tables and their members are parsed under parse_value
		while(strm.good())
		{
			auto [ch, eof] = p_state.get_char<true>();
			if (eof)
				break;

			if (whitespace(ch, p_state))
				continue;

			if (newline(p_state, ch))
			{
				p_state.nextline();
				continue;
			}

			if (ch == '[') // start table or array
			{
				if (p_state.strm.peek() == '[') //array of tables
				{
					p_state.ignore();
					auto table = parse_table_header<NoThrow, true>(p_state, *toml_data);
					if constexpr (NoThrow)
					{
						if (table == bad_index)
							break;
					}

					continue;
				}
				else
				{
					// Errors handled in parse_table_header
					auto table = parse_table_header<NoThrow, false>(p_state, *toml_data);
					if constexpr (NoThrow)
					{
						if (table == bad_index)
							break;
					}

					continue;
				}
			}

			if (ch == '#')
			{
				if (parse_comment<NoThrow>(p_state))
					continue;
				else
				{
					if constexpr (NoThrow)
					{
						insert_bad(*toml_data);
						break;
					}
				}
			}

			p_state.putback(ch);
			if (parse_key_value<NoThrow, normal_tag_t>(p_state, *toml_data))
			{
				std::tie(ch, eof) = p_state.get_char<true>();
				if (eof)
					break;

				if (whitespace(ch, p_state))
				{
					std::tie(ch, eof) = p_state.get_char<true>();
					if (eof)
						break;
				}

				if (ch == '#')
				{
					if (!parse_comment<NoThrow>(p_state))
					{
						if constexpr (NoThrow)
						{
							insert_bad(*toml_data);
							break;
						}
					}
				}
				else if (!newline(p_state, ch))
				{
					const auto ch_index = p_state.col - 1;
					const auto print_error = [&p_state, ch_index](std::ostream& o) {
						parse_line(p_state);
						const auto str = std::string_view{ &p_state.toml_file[ch_index] };
						auto graph_rng = uni::ranges::grapheme::utf8_view{ str };
						o << "Unexpected character found: \'"s <<
							block_control(*begin(graph_rng)) <<
							"\', following value; expected newline.\n"s;
						print_error_string(p_state, ch_index, ch_index + 1, o);
					};

					if constexpr (NoThrow)
					{
						print_error(std::cerr);
						insert_bad(*toml_data);
						break;
					}
					else
					{
						const auto line = p_state.line,
							col = ch_index;
						auto str = std::ostringstream{};
						print_error(str);
						throw unexpected_character{ str.str(), line, col };
					}
				}

				p_state.nextline();
			}
			else
			{
				insert_bad(*toml_data);
				break;
			}
		}

		if (toml_data->nodes.back().type == node_type::end)
			return root_node{};

#ifndef NDEBUG
		toml_data->input_log = std::move(p_state.toml_file);
#endif

		return root_node{ std::move(toml_data), {} };
	}

	template<bool NoThrow>
	root_node parse(std::istream& strm)
	{
		if (!strm.good())
			return root_node{};

		return parse_toml<NoThrow>(strm);
	}

	template<bool NoThrow>
	root_node parse(std::string_view toml)
	{
		auto strstream = std::stringstream{ std::string{ toml }, std::ios_base::in };
		return parse<NoThrow>(strstream);
	}

	template<bool NoThrow>
	root_node parse(const std::filesystem::path& path)
	{
		if constexpr (NoThrow)
		{
			auto ec = std::error_code{};
			if (!std::filesystem::exists(path, ec) ||
				std::filesystem::is_directory(path, ec))
			{
				std::cerr << ec << ": "s << ec.message();
				return root_node{};
			}
		}

		auto strm = std::ifstream{ path };
		return parse<NoThrow>(strm);
	}

	root_node parse(std::string_view toml)
	{
		return parse<false>(toml);
	}

	root_node parse(const std::string& toml)
	{
		return parse(std::string_view{ toml });
	}

	root_node parse(const char* toml)
	{
		return parse(std::string_view{ toml });
	}

	root_node parse(std::istream& strm)
	{
		return parse<false>(strm);
	}

	root_node parse(const std::filesystem::path& path)
	{
		return parse<false>(path);
	}

	root_node parse(std::string_view toml, no_throw_t)
	{
		return parse<true>(toml);
	}

	root_node parse(const std::string& toml, no_throw_t)
	{
		return parse<true>(std::string_view{ toml });
	}

	root_node parse(const char* toml, no_throw_t)
	{
		return parse<true>(std::string_view{ toml });
	}

	root_node parse(std::istream& strm, no_throw_t)
	{
		return parse<true>(strm);
	}

	root_node parse(const std::filesystem::path& filename, no_throw_t)
	{
		return parse<true>(filename);
	}
}
