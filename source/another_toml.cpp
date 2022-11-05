#include "another_toml.hpp"

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

#include "another_toml_string_util.hpp"

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
			writer::int_base base;
		};

		struct floating
		{
			double value;
			writer::float_rep rep;
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

		enum class table_def : std::int8_t
		{
			header,
			dot,
			not_table,
			err
		};

		struct internal_node
		{
			std::string name;
			node_type type = node_type::bad_type;
			value_type v_type = value_type::unknown;
			variant_t value;
			table_def table_type = table_def::not_table;
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

	constexpr auto root_table = index_t{};

	index_t find_table(const std::string_view n, const index_t p, const detail::toml_internal_data& d) noexcept
	{
		assert(p != bad_index);
		
		const auto &parent = d.nodes[p];
		auto table = parent.child;
		while (table != bad_index)
		{
			auto& t = d.nodes[table];
			if (t.name == n)
				return table;
			table = t.next;
		}

		return bad_index;
	}

	// method defs for nodes
	template<bool R>
	bool basic_node<R>::good() const noexcept
	{
		return _data && !(_index == bad_index ||
			node_type::bad_type == _data->nodes[_index].type);
	}

	// test node type
	template<bool R>
	bool basic_node<R>::table() const noexcept
	{
		return node_type::table == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::array() const noexcept
	{
		return node_type::array == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::array_table() const noexcept
	{
		return node_type::array_tables == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::key() const noexcept
	{
		return node_type::key == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::value() const noexcept
	{
		return node_type::value == _data->nodes[_index].type;
	}

	template<bool R>
	bool basic_node<R>::inline_table() const noexcept
	{
		return node_type::inline_table == _data->nodes[_index].type;
	}

	template<bool R>
	value_type basic_node<R>::type() const noexcept
	{
		return _data->nodes[_index].v_type;
	}

	template<bool R>
	bool basic_node<R>::has_children() const noexcept
	{
		return _data->nodes[_index].child != bad_index;
	}

	template<bool R>
	std::vector<basic_node<>> basic_node<R>::get_children() const
	{
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
		return basic_node<>{ _data.get(), _data->nodes[_index].child};
	}

	template<>
	basic_node<> basic_node<>::get_first_child() const
	{
		return basic_node<>{ _data, _data->nodes[_index].child};
	}

	template<bool R>
	bool basic_node<R>::has_sibling() const noexcept
	{
		return _data->nodes[_index].next != bad_index;
	}

	template<>
	basic_node<> basic_node<true>::get_next_sibling() const
	{
		return basic_node<>{ _data.get(), _data->nodes[_index].next};
	}

	template<>
	basic_node<> basic_node<>::get_next_sibling() const
	{
		return basic_node<>{ _data, _data->nodes[_index].next};
	}

	template<bool R>
	basic_node<> basic_node<R>::find_child(std::string_view name) const
	{
		if (!table() && !inline_table())
			throw wrong_node_type{ "Cannot call find_child on this type of node"s };

		auto child = get_first_child();
		while (child.good() && child.as_string() != name)
			child = child.get_next_sibling();

		return child;
	}

	template<bool R>
	basic_node<> basic_node<R>::find_table(std::string_view name) const
	{
		const auto table = find_child(name);
		if (!table.good())
			throw key_not_found{ "No table found with that name"s };

		//const auto table = table_key.get_first_child();
		if (table.table() || table.inline_table())
			return table;
		else if (table.key())
		{
			const auto t_value = table.get_first_child();
			if (t_value.table() || t_value.inline_table())
				return t_value;
		}

		throw wrong_type{ "Name doesnt reference a table"s };
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

	template<bool R>
	std::size_t basic_node<R>::size() const noexcept
	{
		auto size = std::size_t{};
		auto child = _data->nodes[_index].child;
		while (child != bad_index)
		{
			++size;
			child = _data->nodes[child].next;
		}
		return size;
	}

	struct to_string_visitor
	{
	public:
		const writer_options& options;

		std::string operator()(std::monostate)
		{
			return "Error";
		}

		std::string operator()(string_t)
		{
			return "Error";
		}

		std::string operator()(detail::integral i)
		{
			auto out = std::stringstream{};
			using base = writer::int_base;
			if (options.simple_numerical_output)
				i.base = base::dec;

			if (i.base == base::bin)
			{
				// negative values are forbidden
				assert(i.value >= 0);
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

				if (i.base != base::dec)
				{
					assert(i.value >= 0);
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
			if (d.rep == writer::float_rep::scientific &&
				!options.simple_numerical_output)
				strm << std::scientific;
			else if (d.rep == writer::float_rep::fixed &&
				!options.simple_numerical_output)
				strm << std::fixed;

			if (d.precision > writer::auto_precision)
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
			// TODO: make this configurable
			out.push_back('T');
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
		if(_data->nodes[_index].type == node_type::value &&
			_data->nodes[_index].v_type != value_type::string)
		{
			auto opts = writer_options{};
			opts.simple_numerical_output = true;
			return std::visit(to_string_visitor{ opts }, _data->nodes[_index].value);
		}

		return _data->nodes[_index].name;
	}

	template<bool R>
	std::int64_t basic_node<R>::as_integer() const
	{
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
	static index_t insert_child_table(const index_t parent, std::string name, detail::toml_internal_data& d, table_def t);
	template<bool NoThrow>
	static index_t insert_child_table_array(index_t parent, std::string name, detail::toml_internal_data& d);

	//method defs for writer
	writer::writer()
		: _data{ new toml_internal_data{} }
	{}

	void writer::begin_table(std::string_view table_name)
	{
		auto i = _stack.back();
		const auto t = _data->nodes[i].type;
		assert(t == node_type::table ||
			t == node_type::inline_table ||
			t == node_type::array ||
			t == node_type::array_tables);

		auto new_table = insert_child_table<false>(i, std::string{ table_name }, *_data, table_def::header);
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

	void writer::write_value(std::string&& value, literal_t)
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

	void writer::write_value(std::string_view value, literal_t)
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
		auto child = d.nodes[i].child;
		while (child != bad_index)
		{
			const auto& c_ref = d.nodes[child];
			if (node_type::key == c_ref.type ||
				(node_type::value != c_ref.type &&
					!empty(c_ref.name)))
			{
				return true;
			}

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

	bool skip_table_header(index_t table, index_t child, const toml_internal_data& d)
	{
		if (child == bad_index)
			return false;

		auto tables = true;
		const auto func = [&tables](const internal_node& n) noexcept {
			if (n.type != node_type::table &&
				n.type != node_type::array_tables)
				tables = false;
			return;
		};

		for_each_child(table, d, func);
		return tables;
	}

	using char_count_t = std::int16_t;
	
	void append_line_length(char_count_t& line, std::size_t length, const writer_options& o) noexcept
	{
		if (length > o.array_line_length)
			line = o.array_line_length + 1;
		else
			line += static_cast<char_count_t>(length);

		if (line > o.array_line_length)
			line = o.array_line_length + 1;
		return;
	}

	bool optional_newline(std::ostream& strm, char_count_t& last_newline, const writer_options& o)
	{
		if (last_newline > 0 && last_newline > o.array_line_length)
		{
			strm << '\n';
			last_newline = {};
			return true;
		}
		return false;
	}

	using indent_level_t = std::int32_t;

	void optional_indentation(std::ostream& strm, indent_level_t indent, const writer_options& o,
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

	template<bool WriteOne>
	static void write_children(std::ostream& strm, const toml_internal_data& d,
		const writer_options& o, std::vector<index_t> stack, char_count_t& last_newline_dist,
		indent_level_t indent_level)
	{
		assert(!empty(stack));
		const auto parent = stack.back();
		const auto parent_type = d.nodes[parent].type;

		auto children = get_children(parent, d);

		// make sure root keys are written before child tables
		std::partition(begin(children), end(children), [&d](const index_t i) {
			return !(d.nodes[i].type == node_type::table ||
				d.nodes[i].type == node_type::array_tables);
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

				const auto indent = indent_level + 1;
				if (parent_type != node_type::array_tables && 
					(is_headered_table(*beg, d) || c_ref.child == bad_index))
				{
					// skip if all their children are also tables
					// skip writing empty tables unless they are leafs
					if (o.skip_empty_tables && skip_table_header(*beg, c_ref.child, d))
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

					optional_indentation(strm, indent, o, last_newline_dist);

					strm << '[' << make_table_name(name_stack, d, o) << "]\n"s;
					last_newline_dist = {};
				}

				write_children<false>(strm, d, o, std::move(name_stack), last_newline_dist, indent);
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

				//key_name
				const auto key_name = escape_toml_name(c_ref.name, o.ascii_output);
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
					if (!string_extra.literal || 
						// if we want ascii output, all unicode chars 
						// must be escaped in doublequoted strings
						(o.ascii_output && contains_unicode(c_ref.name)))
					{
						strm << '\"';
						const auto str = o.ascii_output ? to_escaped_string2(c_ref.name) : to_escaped_string(c_ref.name);
						strm.write(data(str), size(str));
						strm << '\"';
						append_line_length(last_newline_dist, 2 + size(str), o);
					}
					else
					{
						strm << '\'';
						strm.write(data(c_ref.name), size(c_ref.name));
						strm << '\'';
						append_line_length(last_newline_dist, 2 + size(c_ref.name), o);
					}
				}
				else if (c_ref.v_type == value_type::bad ||
					c_ref.v_type == value_type::unknown)
					assert(false);//bad
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
		d.nodes.emplace_back(internal_node{ {}, node_type::bad_type });
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
				if (child_ref.name == n.name && !allow_duplicates)
				{
					if (child_ref.type == node_type::table && 
						n.type == node_type::table &&
						!child_ref.closed &&
						p.table_type == n.table_type)
						return child;

					if constexpr (NoThrow)
						return bad_index;
					else
						throw duplicate_element{ "Tried to insert duplicate element: "s + n.name + ", into: " + p.name };
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
			if (c->name == s)
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
					return { {}, true };
				else
					throw unexpected_eof{ "Parser encountered an unexcpected eof."};
			}

			#ifndef NDEBUG
			toml_file.push_back(static_cast<char>(val));
			#endif

			++col;
			return { static_cast<char>(val), {} };
		}

		void nextline() noexcept
		{
			col = {};
			++line;
			return;
		}

		void ignore()
		{
			#ifndef NDEBUG
			toml_file.push_back(strm.peek());
			#endif

			strm.ignore();
			return;
		}

		void putback(const char ch) noexcept
		{
			--col;
			strm.putback(ch);

			#ifndef NDEBUG
			toml_file.pop_back();
			#endif
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
		#ifndef NDEBUG
		std::string toml_file;
		#endif
	};

	//new line(we check both, since file may have been opened in binary mode)
	static bool newline(parser_state& strm, char ch) noexcept
	{
		if (ch == '\r' && strm.strm.peek() == '\n')
		{
			strm.ignore();
			return true;
		}

		return ch == '\n';
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
	static index_t insert_child_table(const index_t parent, std::string name, detail::toml_internal_data& d, table_def t)
	{
		auto table = detail::internal_node{ std::move(name) , node_type::table };
		table.closed = false;
		table.table_type = t;
		return insert_child<NoThrow>(d, parent, std::move(table));
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
				if (node->name == name)
				{
					if (node->type == node_type::array_tables)
					{
						parent = child;
						break;
					}
					else
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Tried to create table array with already used name: " << name << '\n';
							return bad_index;
						}
						else
							throw parser_error{ "Tried to create table array with already used name: "s + name };
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
				n.table_type = table_def::header;
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
			n.table_type = table_def::header;
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
		auto ret = insert_child_table<NoThrow>(parent, {}, d, table_def::header);		
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

	// access a template func from string_util.cpp
	template<bool NoThrow>
	std::optional<std::string> to_u8_str(char32_t ch);

	extern template std::optional<std::string> to_u8_str<true>(char32_t ch);
	extern template std::optional<std::string> to_u8_str<false>(char32_t ch);

	// variant of unicode_u8_to_u32 that reads from parser_state
	static char32_t parse_unicode_char(char c, parser_state& strm) noexcept
	{
		if (!is_unicode_start(c))
			return unicode_error_char;

		int bytes = 1;
		if (c & 0b01000000)
			++bytes;
		if (c & 0b00100000)
			++bytes;
		if (c & 0b00010000)
			++bytes;

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

		if (valid_u32_char(out))
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
		while (strm.strm.good())
		{
			const auto [ch, eof] = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
				{
					std::cerr << "Unexpected end of file in quoted string\n"s;
					return {};
				}
			}

			if (newline(strm, ch))
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal newline in quoted string\n"s;
					return {};
				}
				else
					throw unexpected_character{ "Illigal newline in quoted string"s };
			}

			if (ch == delim)
			{
				if constexpr (DoubleQuoted)
				{
					if (size(out) > 0)
					{
						//don't break on excaped quote: '\"'
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
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal control character in string\n"s;
					return {};
				}
				else
					throw unexpected_character{ "Illigal control character in string"s };
			}
			else if (is_unicode_byte(ch))
			{
				const auto unicode = parse_unicode_char(ch, strm);
				if (unicode == unicode_error_char)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Invalid unicode character in string\n"s;
						return {};
					}
					else
						throw parser_error{ "Invalid unicode character in string"s };
				}
				else
				{
					const auto u8 = to_u8_str<NoThrow>(unicode);
					if constexpr (NoThrow)
					{
						if (!u8)
						{
							std::cerr << "Invalid unicode character in string\n"s;
							return {};
						}
					}

					assert(u8);
					out.append(*u8);
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
			return {};
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
				std::cerr << "Illigal character found in table/key name: " << ch << '\n';
				strm.putback(ch);
				return {};
			}

			out.push_back(ch);
		}

		return out;
	}

	template<bool NoThrow>
	std::optional<std::string> replace_escape_chars(std::string_view);

	extern template std::optional<std::string> replace_escape_chars<true>(std::string_view);
	extern template std::optional<std::string> replace_escape_chars<false>(std::string_view);

	template<bool NoThrow, bool Table = false>
	static key_name parse_key_name(parser_state& strm, detail::toml_internal_data& d)
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
			auto [ch, eof] = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return {};
			}

			if (whitespace(ch, strm))
				continue;

			if (ch == '=' || ch == ']')
			{
				if (!name)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Illigal name\n"s;
						return {};
					}
					else
						throw parser_error{ "Illigal name"s };
				}

				strm.putback(ch);
				break;
			}

			if (ch == '\"')
			{
				if (name)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Illigal character in name: \"\n";
						return {};
					}
					else
						throw unexpected_character{ "Illigal character in name: \"" };
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
						std::cerr << "Unexpected end of literal string: "s << name.value_or("\"\""s) << '\n';
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
					if constexpr (NoThrow)
					{
						std::cerr << "Missing name\n";
						insert_bad(d);
						return {};
					}
					else
						throw parser_error{ "Missing name" };
				}
				else
				{
					auto child = find_child(d, parent, *name);
					
					if (child == bad_index)
						child = insert_child_table<NoThrow>(parent, std::move(*name), d, table_def::dot);
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
								throw parser_error{ "Name heirarchy for keys shouldn't include table arrays"s };
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
					else if (c.closed && c.table_type == table_def::header)
					{
						if constexpr(!Table)
						{
							if constexpr (NoThrow)
							{
								std::cerr << "Using dotted keys to add to a previously defined table is not allowed\n"s;
								insert_bad(d);
								return {};
							}
							else
								throw parser_error{ "Using dotted keys to add to a previously defined table is not allowed"s };
						}
						// fall out of if
					}
					else if (!(c.type == node_type::table || c.type == node_type::array_tables))
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Using dotted names to treat a non-table as a table\n"s;
							insert_bad(d);
							return {};
						}
						else
							throw parser_error{ "Using dotted names to treat a non-table as a table"s };
					}

					parent = child;
					name = {};
					continue;
				}
			}

			if (!name)
			{
				auto str = get_unquoted_name(strm, ch);
				if (str)
					name = str;
				else
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Error reading name\n"s;
						insert_bad(d);
						return {};
					}
					else
						throw parser_error{ "Error reading name"s };
				}
			}
			else
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Unexpected character in name\n"s;
					insert_bad(d);
					return {};
				}
				else
					throw parser_error{ "Unexpected character in name"s };
			}
		}

		return { parent, name };
	}

	using get_value_type_ret = std::tuple<value_type, variant_t, std::string>;

	// access func from string_util.cpp
	std::string remove_underscores(std::string_view sv);

	template<bool NoThrow>
	static get_value_type_ret get_value_type(std::string_view str) noexcept(NoThrow)
	{
		if (empty(str))
			return { value_type::bad, {}, {} };

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
			auto base_en = writer::int_base::dec;
			if (size(string) > 1)
			{
				const auto base_chars = std::string_view{ &*begin(string), 2 };
				if (base_chars == "0x"sv)
				{
					base = 16;
					base_en = writer::int_base::hex;
				}
				else if (base_chars == "0b"sv)
				{
					base = 2;
					base_en = writer::int_base::bin;
				}
				else if (base_chars == "0o"sv)
				{
					base = 8;
					base_en = writer::int_base::oct;
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
			else if(ret.ec == std::errc::invalid_argument)
				return { value_type::bad, {}, {} };
			else if (ret.ec == std::errc::result_out_of_range)
				return { value_type::out_of_range, {}, {} };
		}
		
		using error_t = parse_float_string_return::error_t;
		const auto float_ret = parse_float_string(str);
		if (float_ret.error == error_t::none)
			return { value_type::floating_point, floating{ float_ret.value, float_ret.representation }, std::string{str} };
		else if(float_ret.error == error_t::out_of_range)
			return { value_type::out_of_range, {}, {} };

		const auto ret = parse_date_time(str);
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
					if constexpr (NoThrow)
					{
						if constexpr (array)
							std::cerr << "Unexpected newline in array element\n"s;
						else
							std::cerr << "Unexpected newline in inline table value\n"s;
						return false;
					}
					else
					{
						if constexpr(array)
							throw parser_error{ "Unexpected newline in array element"s };
						else
							throw parser_error{ "Unexpected newline in inline table value"s };
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
			if constexpr (NoThrow)
			{
				std::cerr << "Expected value\n"s;
				return false;
			}
			else
				throw parser_error{ "Expected value"s };
		}

		auto [type, value, string] = get_value_type<NoThrow>(out);
		if (type == value_type::bad)
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Failed to parse value\n"s;
				return false;
			}
			else
				throw parser_error{ "Error parsing value"s };
		}

		if (type == value_type::out_of_range)
		{
			if constexpr (NoThrow)
			{
				std::cerr << "parsed value out of range\n"s;
				return false;
			}
			else
				throw parser_error{ "Parsed value was out of range"s };
		}

		strm.token_stream.push_back(token_type::value);
		return insert_child<NoThrow>(toml_data, parent, 
			internal_node{
				std::move(string), node_type::value,
				type, std::move(value)
			}) != bad_index;
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
							if constexpr (NoThrow)
							{
								std::cerr << "Illigal character following '\\' line break\n"s;
								return {};
							}
							else
								throw unexpected_character{ "Illigal character following '\\' line break"s };
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
					if constexpr (NoThrow)
					{
						std::cerr << "Invalid sequence at end of multiline string\n"s;
						return {};
					}
					else
						throw unexpected_character{ "Invalid sequence at end of multiline string"s };
				}
			}

			if (invalid_string_chars(ch) && ch != '\n')
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal control character in multiline string\n"s;
					return {};
				}
				else
					throw unexpected_character{ "Illigal control character in multiline string"s };
			}

			str.push_back(ch);
		}

		if constexpr (NoThrow)
		{
			return {};
		}
		else
			throw parser_error{ "Error parsing multiline string"s };
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
				strm.ignore();
				str = multiline_string<NoThrow, DoubleQuote>(strm);
				if (!str)
					return false;

				if constexpr (DoubleQuote)
				{
					str = replace_escape_chars<NoThrow>(*str);
					if (!str)
						return false;
				}
			}
			else
				str = std::string{};
		}
		else
		{
			//start normal quote str
			str = get_quoted_str<NoThrow, DoubleQuote>(strm);
			if constexpr (NoThrow)
			{
				if (!str)
				{
					std::cerr << "Illigal character in string"s;
					return false;
				}
			}

			if constexpr (DoubleQuote)
			{
				str = replace_escape_chars<NoThrow>(*str);
				if (!str)
					return false;
			}

			if (strm.strm.peek() == quote_char)
				strm.ignore();
			else
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal character in quoted string"s;
					return false;
				}
				else
					throw unexpected_character{ "Illigal character in quoted string"s };
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
				if constexpr (NoThrow)
				{
					std::cerr << "Forbidden character in comment: " << ch << '(' << static_cast<int>(ch) << ")\n";
					return false;
				}
				else
					throw unexpected_character{ "Forbidden character in comment: "s + ch +
					'(' + std::to_string(static_cast<int>(ch)) + ')' };
			}

			if (is_unicode_byte(ch))
			{
				auto u_ch = parse_unicode_char(ch, strm);
				if (u_ch == unicode_error_char)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Invalid unicode character(s) in stream\n";
						return false;
					}
					else
						throw unexpected_character{ "Invalid unicode character(s) in stream"s };
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
					return false;
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
				assert(strm.token_stream.back() == token_type::value);
				strm.token_stream.emplace_back(token_type::comma);
				continue;
			}

			if (ch == '#')
			{
				if (!parse_comment<NoThrow>(strm))
				{
					if constexpr (NoThrow)
						return false;
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
			throw parser_error{ "Stream error while parsing array"s };
	}

	template<bool NoThrow>
	static bool parse_inline_table(parser_state& strm, toml_internal_data& toml_data)
	{
		// push back an inline table
		assert(!empty(strm.stack));
		const auto parent = strm.stack.back();
		const auto& p = toml_data.nodes[parent];
		assert(p.type == node_type::key || p.type == node_type::array);
		const auto table = insert_child<NoThrow>(toml_data, parent, internal_node{ {}, node_type::inline_table });
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
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal newline in inline table\n";
					return false;
				}
				else
					throw unexpected_character{ "Illigal newline in inline table"s };
			}

			if (ch == '}')
			{
				if (strm.token_stream.back() == token_type::comma)
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Trailing comma is forbidden in inline tables\n"s;
						insert_bad(toml_data);
						return false;
					}
					else
						throw unexpected_character{ "Trailing comma is forbidden in inline tables"s };
				}

				toml_data.nodes[table].closed = true;
				assert(table == strm.stack.back());
				strm.stack.pop_back();
				return true;
			}

			if (const auto back = strm.token_stream.back();
				!(back == token_type::inline_table || back == token_type::comma))
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Values must be seperated by commas in inline tables\n"s;
					insert_bad(toml_data);
					return false;
				}
				else
					throw unexpected_character{ "Values must be seperated by commas in inline tables"s };
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
		auto key_str = parse_key_name<NoThrow>(strm, toml_data);

		if constexpr (NoThrow)
		{
			if (!key_str.name)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
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
		const auto key_index = insert_child<NoThrow>(toml_data, key_str.parent, std::move(key));

		if constexpr (NoThrow)
		{
			if (key_index == bad_index)
				return false;
		}
		
		strm.stack.emplace_back(key_index);
		strm.token_stream.emplace_back(token_type::key);
		
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

		strm.token_stream.emplace_back(token_type::value);
		const auto parent_type = toml_data.nodes[strm.stack.back()].type;
		if (parent_type == node_type::key)
			strm.stack.pop_back();
		return ret;
	}

	template<bool NoThrow, bool Array>
	static index_t parse_table_header(parser_state& strm, toml_internal_data& toml_data)
	{
		assert(toml_data.nodes[strm.stack.back()].type == node_type::table ||
			toml_data.nodes[strm.stack.back()].type == node_type::array_tables);
		strm.stack.pop_back();
		strm.close_tables(toml_data);

		const auto name = parse_key_name<NoThrow, true>(strm, toml_data);
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
			if constexpr (NoThrow)
			{
				std::cerr << "Unexpected character, was expecting ']'; found: " << ch << '\n';
				return bad_index;
			}
			else
				throw unexpected_character{ "Unexpected character, was expecting ']'" };
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
				if constexpr (NoThrow)
				{
					insert_bad(toml_data);
					std::cerr << "Unexpected character while parsing array table name; expected: ']'\n"s;
					return bad_index;
				}
				else
					throw unexpected_character{ "Unexpected character while parsing array table name; expected: ']'"s };
			}
		}

		if (const auto table = find_table(*name.name, name.parent, toml_data);
			table != bad_index && toml_data.nodes[table].closed == true)
		{
			if constexpr (!Array)
			{
				if constexpr (NoThrow)
				{
					std::cerr << "This table: " << *name.name << "; has already been defined\n";
					return bad_index;
				}
				else
					throw parser_error{ "table redefinition" };
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
				throw parser_error{ "Error getting table name" };
		}

		auto& parent = name.parent;
		auto table = bad_index;
		if constexpr (Array)
		{
			table = insert_child_table_array<NoThrow>(name.parent, std::move(*name.name), toml_data);
			strm.token_stream.emplace_back(token_type::array_table);
		}
		else
		{
			table = find_table(*name.name, name.parent, toml_data);
			if (table == bad_index)
				table = insert_child_table<NoThrow>(name.parent, std::move(*name.name), toml_data, table_def::header);
			strm.token_stream.emplace_back(token_type::table);
		}

		if constexpr (NoThrow)
		{
			if (table == bad_index)
				return bad_index;
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
					return bad_index;
			}
		}
		else if (!newline(strm, ch))
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Unexpected character after table header\n"s;
				return bad_index;
			}
			else
				throw unexpected_character{ "Unexpected character after table header"s };
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
					if constexpr (NoThrow)
					{
						std::cerr << "Key/Value pairs must be followed by a newline\n"s;
						insert_bad(*toml_data);
						break;
					}
					else
						throw unexpected_character{ "Key/Value pairs must be followed by a newline"s };
				}
			}
			else
			{
				insert_bad(*toml_data);
				break;
			}
		}

		if (toml_data->nodes.back().type == node_type::bad_type)
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

	root_node parse(std::string_view toml, detail::no_throw_t)
	{
		return parse<true>(toml);
	}

	root_node parse(const std::string& toml, detail::no_throw_t)
	{
		return parse<true>(std::string_view{ toml });
	}

	root_node parse(const char* toml, detail::no_throw_t)
	{
		return parse<true>(std::string_view{ toml });
	}

	root_node parse(std::istream& strm, detail::no_throw_t)
	{
		return parse<true>(strm);
	}

	root_node parse(const std::filesystem::path& filename, detail::no_throw_t)
	{
		return parse<true>(filename);
	}
}
