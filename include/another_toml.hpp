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

#ifndef ANOTHER_TOML_HPP
#define ANOTHER_TOML_HPP

#include <cassert>
#include <filesystem>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

// library for parsing toml 1.0 documents

namespace another_toml
{
	//thown by any of the parse functions
	class toml_error : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	//thrown if eof is encountered in an unexpected location(inside a quote or table name, etc.)
	class unexpected_eof : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown when encountering an unexpected character
	class unexpected_character :public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown if the toml file contains duplicate table or key declarations
	class duplicate_element : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// Thrown by basic_node when calling fucntions on a node where good() == false
	class bad_node : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown when calling node::as_int... if the type
	// stored doesn't match the function return type
	class wrong_type : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown if calling a function that isn't 
	// supported by the current node type
	class wrong_node_type : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown by some functions that search for keys,
	// but do not have another way to report failure
	class node_not_found : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown from unicode handling functions(in string_util.hpp)
	// also thrown while parsing or writing unicode text
	class unicode_error : public toml_error
	{
	public:
		using toml_error::toml_error;
	};
	
	// thrown if an invalid raw unicode or escaped unicode char was found
	class invalid_unicode_char : public unicode_error
	{
	public:
		using unicode_error::unicode_error;
	};

	namespace detail
	{
		// Internal value, used to indicate that the index of a node
		// couldn't be found (or creation of a node failed).
		using index_t = std::size_t;
		constexpr auto bad_index = std::numeric_limits<detail::index_t>::max();

		// Trait for iterable ranges (excluding string).
		template<typename Range, typename = void>
		constexpr auto is_range_v = false;
		template<typename Range>
		constexpr auto is_range_v<Range,
			std::void_t<
			decltype(std::declval<Range>().begin()),
			decltype(std::declval<Range>().end())
			>
		> = !std::is_same_v<Range, std::string>;

		// Trait for appendable containers (excluding string).
		template<typename Cont, typename = void>
		constexpr auto is_container_v = false;
		template<typename Cont>
		constexpr auto is_container_v<Cont,
			std::void_t<
			typename Cont::value_type,
			decltype(std::declval<Cont>().push_back(typename Cont::value_type{}))
			>
		> = !std::is_same_v<Cont, std::string>;

		// Trait for integral types, exluding bool.
		template<typename Integral>
		constexpr auto is_integral_v = std::is_integral_v<Integral> &&
			!std::is_same_v<Integral, bool>;

		// Internal data storage type
		struct toml_internal_data;
		// Deleter above type
		struct toml_data_deleter
		{
		public:
			void operator()(toml_internal_data*) noexcept;
		};

		// Returns the sibling node of index_t, or bad_index.
		index_t get_next(const toml_internal_data&, index_t) noexcept;

		// Tag type
		struct no_throw_t {};
	}

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

	// TOML node types.
	enum class node_type : std::uint8_t
	{
		array, 
		array_tables,
		key,
		inline_table,
		value,
		table,
		end
	};

	enum class table_def_type : std::uint8_t
	{
		dotted, 
		header,
		end
	};

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
		std::int8_t hours = {},
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

	namespace detail
	{
		template<typename T>
		constexpr auto is_toml_type = std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, double> ||
			std::is_same_v<T, bool> ||
			std::is_same_v<T, std::string> ||
			std::is_same_v<T, date> ||
			std::is_same_v<T, time> ||
			std::is_same_v<T, local_date_time> ||
			std::is_same_v<T, date_time>;
	}

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

	// FWD def
	class node_iterator;
	constexpr auto no_throw = detail::no_throw_t{};

	// TOML node for accessing parsed data
	// If RootNode = true then the type holds ownership of the 
	// internal TOML data.
	template<bool RootNode = false>
	class basic_node 
	{
	public:
		using data_type = std::conditional_t<RootNode,
			std::unique_ptr<detail::toml_internal_data, detail::toml_data_deleter>,
			const detail::toml_internal_data*>;

		// Child node constructor
		explicit basic_node(data_type shared_data = data_type{},
			detail::index_t i = detail::bad_index)
			: _data{ std::move(shared_data) }, _index{ i } {}

		// Test if this is a valid node.
		// If you use iterator/ranges to access child and sibling nodes then you don't have to worry about this.
		// NOTE: the non-throwing parse functions return values should be checked for this
		//		before use
		bool good() const noexcept;

		// Test node type
		bool table() const noexcept;
		bool array() const noexcept;
		bool array_table() const noexcept;
		bool key() const noexcept;
		bool value() const noexcept;
		bool inline_table() const noexcept;

		// if this is a value node, this returns
		// the type of the stored value
		value_type type() const noexcept;

		// Get the nodes children
		// children are any nodes deeper in the heirarchy than the current node
		// eg. for a table: subtables, keys, arrays(of subtables)
		//		for an array: arrays, values, tables
		//		for a value: none
		//		for a key: anonymous table, value, array(of arrays of values)
		bool has_children() const noexcept;
		// Throws: bad_node if good() == false for this node
		std::vector<basic_node<>> get_children() const;
		// Throws: bad_node if good() == false for this node
		// If this node has no chilren, then the returned node will be bad
		basic_node<> get_first_child() const;

		// Get siblings. If this node isn't the only child of its parent
		// then you can iterate through the siblings by calling get_next_sibling.
		bool has_sibling() const noexcept;

		// Throws: bad_node if good() == false for this node
		// Throws: node_not_found if has_children() == false
		basic_node<> get_next_sibling() const;

		// get child with the provided name
		// test the return value using .good()
		// NOTE: Only searches immediate children.
		//		 Doesn't support dotted keynames.
		// Throws : bad_node and node_not_found
		basic_node<> find_child(std::string_view) const;

		// As above, but returns a bad node on error instead of throwing
		basic_node<> find_child(std::string_view, detail::no_throw_t) const noexcept;

		// Searches for a child node called key_name,
		// if that node is a Key, then returns it's child
		// converted to `T`.
		// Requires that this node is a table or inline_table
		// Both versions of this function throw wrong_type if
		// 'T' is not able to store the value
		// Throws: node_not_found
		template<typename T>
		T get_value(std::string_view key_name) const;
		// Provide a default value to be returned if the key isn't found
		template<typename T>
		T get_value(std::string_view key_name, T default_return) const;

		// Iterator based interface for accessing child nodes
		node_iterator begin() const noexcept;
		node_iterator end() const noexcept;
		
		// Extract the value of this node as various types
		// as_string can extract the names of tables, keys, arrays, array_tables
		// and convert value nodes to string representations
		std::string as_string() const;
		std::string as_string(int_base) const;
		std::string as_string(float_rep, std::int8_t = auto_precision) const;

		// The following functions should only be called on nodes
		// matching the value_type of the node
		// this requires value() to return true and type() to return
		// the type desired.
		std::int64_t as_integer() const;
		double as_floating() const;
		bool as_boolean() const;
		date_time as_date_time() const;
		local_date_time as_date_time_local() const;
		date as_date_local() const;
		time as_time_local() const;

		// Extracts this node as the requested type.
		// `T` can be a container if this node is a heterogeneous array
		// `T` or `T::value_type` must be one of the types returned by 
		// the extraction functions above (as_XXXXX()).
		template<typename T>
		T as_type() const;

		// Shorthand for find_child
		basic_node<> operator[](std::string_view str) const
		{
			return find_child(str);
		}

		// Disambiguate from the built-in ptr dereference operator
		basic_node<> operator[](const char* str) const
		{
			return find_child(str);
		}

		// Allow implicit testing of the node
		operator bool() const noexcept
		{
			return good();
		}

	private:
		data_type _data;
		detail::index_t _index;
	};

	// The two node types are the `root_node`, which is returned by 
	// another_toml::parse and owns the parsed TOML data and
	// `node` which is a lightweight view into the root_node. 
	using root_node = basic_node<true>;
	using node = basic_node<>;
	extern template class basic_node<true>;
	extern template class basic_node<>;

	// Iterator for iterating through node siblings
	// Returned by node functions.
	class node_iterator
	{
	public:
		using iterator_concept = std::forward_iterator_tag;

		node_iterator(const detail::toml_internal_data* sh = {},
			detail::index_t i = detail::bad_index)
			: _data{ sh }, _index{ i }
		{}

		node operator*() const noexcept
		{
			assert(_index != detail::bad_index &&
				_data);
			return basic_node{ _data, _index };
		}

		node_iterator& operator++() noexcept
		{
			if (_index == detail::bad_index)
				return *this;
			assert(_data);
			if (const auto next = detail::get_next(*_data, _index);
				next == detail::bad_index)
			{
				_data = {};
				_index = detail::bad_index;
			}
			else
				_index = next;
			return *this;
		}

		node_iterator operator++(int) const noexcept {
			if (_index == detail::bad_index)
				return *this;
			assert(_data);
			const auto next = detail::get_next(*_data, _index);
			if (next == detail::bad_index)
				return node_iterator{};
			return node_iterator{ _data, next };
		}

		bool operator==(const node_iterator& rhs) const noexcept
		{
			return _data == rhs._data && _index == rhs._index;
		}

		bool operator!=(const node_iterator& rhs) const noexcept
		{
			return !(*this == rhs);
		}

	private:
		const detail::toml_internal_data* _data;
		detail::index_t _index;
	};

	// Parse a TOML document.
	root_node parse(std::string_view toml);
	root_node parse(const std::string& toml);
	root_node parse(const char* toml);
	root_node parse(std::istream&);
	// NOTE: user must handle std exceptions related to file reading
	// eg. std::filesystem_error and its children.
	root_node parse(const std::filesystem::path& filename);

	// Parse a TOML document without throwing of the exceptions
	// from the top of this file. Errors are reported to std::cerr.
	root_node parse(std::string_view toml, detail::no_throw_t);
	root_node parse(const std::string& toml, detail::no_throw_t);
	root_node parse(const char* toml, detail::no_throw_t);
	root_node parse(std::istream&, detail::no_throw_t);
	root_node parse(const std::filesystem::path& filename, detail::no_throw_t);

	//global writer options
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
			std::enable_if_t<detail::is_range_v<Container> &&
			std::is_convertible_v<typename Container::value_type, std::string_view>, int> = 0>
		void write(std::string_view key, Container&& value, literal_string_t);
		// Allowing passing an integral base when using the above template method.
		template<typename Integral, 
			std::enable_if_t<detail::is_integral_v<Integral>, int> = 0>
		void write(std::string_view key, Integral value, int_base);
		// Array write for integrals
		template<typename Container,
			std::enable_if_t<detail::is_range_v<Container> &&
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

namespace std
{
	template<>
	struct iterator_traits<another_toml::node_iterator>
	{
		using difference_type = std::ptrdiff_t;
		using value_type = another_toml::basic_node<>;
		using pointer = value_type*;
		using reference = const value_type&;
		using iterator_category = std::forward_iterator_tag;
	};
}

#include "another_toml.inl"

#endif // !ANOTHER_TOML_HPP
