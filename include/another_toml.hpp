#ifndef ANOTHER_TOML_HPP
#define ANOTHER_TOML_HPP

#include <cassert>
#include <filesystem>
#include <istream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

// library for parsing toml1.0 documents

namespace another_toml
{
	//thown by any of the parse functions
	class parser_error :public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	//thrown if eof is encountered in an unexpected location(inside a quote or table name, etc.)
	class unexpected_eof : public parser_error
	{
	public:
		using parser_error::parser_error;
	};

	//thrown when encountering an unexpected character
	class unexpected_character :public parser_error
	{
	public:
		using parser_error::parser_error;
	};

	//thrown if the toml file contains duplicate table or key declarations
	class duplicate_element :public parser_error
	{
	public:
		using parser_error::parser_error;
	};

	class wrong_type : public parser_error
	{
	public:
		using parser_error::parser_error;
	};

	// thrown if an invalid raw unicode or escaped unicode char was found
	class invalid_unicode_char : public parser_error
	{
	public:
		using parser_error::parser_error;
	};

	namespace detail
	{
		using index_t = std::size_t;
		constexpr auto bad_index = std::numeric_limits<detail::index_t>::max();

		struct toml_internal_data;

		struct toml_data_deleter
		{
		public:
			void operator()(toml_internal_data*) noexcept;
		};

		index_t get_next(const toml_internal_data&, index_t) noexcept;

		struct no_throw_t {};
	}

	enum class value_type {
		string,
		integer,
		floating_point,
		boolean,
		date_time,
		local_date_time,
		local_date,
		local_time,
		unknown, // couldn't detect, can still be read as string
		// The following values are internal only
		bad, // detected as invalid
		out_of_range
	};

	enum class node_type
	{
		table,
		array, 
		array_tables,
		key,
		value,
		inline_table,
		comment, // note, we don't provide access to comments or preserve them
		bad_type
	};

	struct date
	{
		std::uint16_t year;
		std::uint8_t month, day;
	};

	struct time
	{
		std::int8_t hours, minutes, seconds;
		float seconds_frac;
	};

	struct local_date_time
	{
		date date;
		time time;
	};

	struct date_time
	{
		local_date_time datetime;
		bool offset_positive;
		uint8_t offset_hours;
		uint8_t offset_minutes;
	};

	class node_iterator;

	template<bool RootNode = false>
	class node 
	{
	public:
		using data_type = std::conditional_t<RootNode,
			std::unique_ptr<detail::toml_internal_data, detail::toml_data_deleter>,
			const detail::toml_internal_data*>;

		explicit node(data_type shared_data = data_type{},
			detail::index_t i = detail::bad_index)
			: _data{ std::move(shared_data) }, _index{ i } {}

		//test if this is a valid node: calling any function other than good()
		//								on an invalid node is undefined behaviour
		// this will return false for nodes returned by the fucntions:
		//	get_child() for a node that has no children
		// if you use iterator/ranges to access child nodes then you don't have to worry about this.
		bool good() const noexcept;

		// test node type
		bool table() const noexcept;
		bool array() const noexcept;
		bool array_table() const noexcept;
		bool key() const noexcept;
		bool value() const noexcept;
		bool inline_table() const noexcept;

		value_type type() const noexcept;

		// get the nodes children
		// children are any nodes deeper in the heirarchy than the current node
		// eg. for a table: subtables, keys, arrays(of subtables)
		//		for an array: arrays, values, tables
		//		for a value: none
		//		for a key: anonymous table, value, array(of arrays of values)
		bool has_children() const noexcept;
		std::vector<node<>> get_children() const;
		node<> get_child() const;

		// iterator based interface
		node_iterator begin() const noexcept;
		node_iterator end() const noexcept;
		std::size_t size() const noexcept;

		const std::string& as_string() const noexcept;
		std::int64_t as_int() const;
		double as_floating() const;
		bool as_boolean() const;
		date_time as_date_time() const;
		local_date_time as_date_time_local() const;
		date as_date_local() const;
		time as_time_local() const;

		operator bool() const noexcept
		{
			return good();
		}

	private:
		data_type _data;
		detail::index_t _index;
	};

	using root_node = node<true>;
	extern template class node<true>;
	extern template class node<>;

	class node_iterator
	{
	public:
		using iterator_concept = std::forward_iterator_tag;

		node_iterator(const detail::toml_internal_data* sh = {},
			detail::index_t i = detail::bad_index)
			: _data{ sh }, _index{ i }
		{}

		node<> operator*() const noexcept
		{
			assert(_index != detail::bad_index &&
				_data);
			return node{ _data, _index };
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

	constexpr auto no_throw = detail::no_throw_t{};

	root_node parse(std::string_view toml);
	root_node parse(std::istream&);
	//node parse(const std::filesystem::path& filename);

	root_node parse(std::string_view toml, detail::no_throw_t) noexcept;
	root_node parse(std::istream&, detail::no_throw_t) noexcept;
	//node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept;

	struct writer_options
	{
		// if true, avoids unrequired whitespace eg: name = value -> name=value
		bool compact_spacing = false;
		// how many characters before splitting next array element to new line
		// set to 0 to never split
		int array_line_length = 80;
	};

	class writer
	{
	public:
		// note their is an implicit root table
		// you cannot end_table to end it
		// you can write values and arrays into it, before adding
		// other tables

		// [tables]
		// use end table to control nesting
		void begin_table(std::string_view);
		void end_table();

		// arrays:
		// name = [ elements ]
		// use write_element() to add elements
		// or begin_inline_table to add a table as an element
		void begin_array(std::string_view name);
		void end_array();

		// begins an inline table
		// name will be ignored if being added as an array member
		void begin_inline_table(std::string_view name);
		void end_inline_table();

		// begin an array of tables
		// [[array]]
		// keep calling begin_array_tables with the same name
		// to add new tables to the array
		void begin_array_tables(std::string_view);
		void end_array_tables();

		// key: values
		void write(std::string_view name, std::string value);
		void write(std::string_view name, std::int64_t value);
		void write(std::string_view name, double value);
		void write(std::string_view name, bool value);
		void write(std::string_view name, date_time value);
		void write(std::string_view name, local_date_time value);
		void write(std::string_view name, date value);
		void write(std::string_view name, time value);

		// write values on their own, for arrays
		void write_element(std::string value);
		void write_element(std::int64_t value);
		void write_element(double value);
		void write_element(bool value);
		void write_element(date_time value);
		void write_element(local_date_time value);
		void write_element(date value);
		void write_element(time value);

		void set_options(writer_options o)
		{
			_opts = o;
		}

		std::ostream& operator<<(std::ostream& lhs) const;

	private:
		writer_options _opts;
		std::unique_ptr<detail::toml_internal_data, detail::toml_data_deleter> _data;
	};

	writer make_writer();
}

namespace std
{
	template<>
	struct iterator_traits<another_toml::node_iterator>
	{
		using difference_type = std::ptrdiff_t;
		using value_type = another_toml::node<>;
		using pointer = value_type*;
		using reference = const value_type&;
		using iterator_category = std::forward_iterator_tag;
	};
}

#include "another_toml.inl"

#endif // !ANOTHER_TOML_HPP
