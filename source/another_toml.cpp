// Deprecated but not marked for removal
// This is the easiest way for us to convert unicode escape codes
// We dont use the functionallity that was considered a vulnerability AFAIK
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include "another_toml.hpp"

#include <array>
#include <cassert>
#include <codecvt>
#include <fstream>
#include <iostream>
//#include <locale>
#include <ranges>
#include <regex>
#include <sstream>
#include <stack>
#include <string_view>
#include <variant>

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
		using variant_t = std::variant<std::monostate, std::int64_t, double, bool,
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
			// includes the table names
			std::string name;
			node_type type = node_type::bad_type;
			value_type v_type = value_type::unknown;
			variant_t value;
			index_t next = bad_index;
			index_t child = bad_index;
			// a closed table can still have child tables added, but not child keys
			table_def table_type = table_def::not_table;
			bool closed = true; 
		};

		struct toml_internal_data
		{
			// not tables: this is all nodes
			std::vector<internal_node> tables;
		};

		void toml_data_deleter::operator()(toml_internal_data* ptr) noexcept
		{
			delete ptr;
		}

		index_t get_next(const toml_internal_data& d, const index_t i) noexcept
		{
			assert(size(d.tables) > i);
			return d.tables[i].next;
		}
	}

	using namespace detail;

	constexpr auto root_table = index_t{};

	index_t find_table(const std::string_view n, const index_t p, const detail::toml_internal_data& d) noexcept
	{
		assert(p != bad_index);
		
		const auto &parent = d.tables[p];
		auto table = parent.child;
		while (table != bad_index)
		{
			auto& t = d.tables[table];
			if (t.name == n)
				return table;
			table = t.next;
		}

		return bad_index;
	}

	bool node::good() const noexcept
	{
		return _data && (_index != bad_index ||
			node_type::bad_type == _data->tables[_index].type);
	}

	// test node type
	bool node::table() const noexcept
	{
		return node_type::table == _data->tables[_index].type;
	}

	bool node::array() const noexcept
	{
		return node_type::array == _data->tables[_index].type;
	}

	bool node::array_table() const noexcept
	{
		return node_type::array_tables == _data->tables[_index].type;
	}

	bool node::key() const noexcept
	{
		return node_type::key == _data->tables[_index].type;
	}

	bool node::value() const noexcept
	{
		return node_type::value == _data->tables[_index].type;
	}

	bool node::inline_table() const noexcept
	{
		return node_type::inline_table == _data->tables[_index].type;
	}

	value_type node::type() const noexcept
	{
		return _data->tables[_index].v_type;
	}

	bool node::has_children() const noexcept
	{
		return _data->tables[_index].child != bad_index;
	}

	std::vector<node> node::get_children() const
	{
		auto out = std::vector<node>{};
		auto child = _data->tables[_index].child;
		while (child != bad_index)
		{
			out.emplace_back(node{ _data, child });
			child = _data->tables[child].next;
		}
		return out;
	}

	node node::get_child() const
	{
		return { _data, _data->tables[_index].child };
	}

	node_iterator node::begin() const noexcept
	{
		const auto child = _data->tables[_index].child;
		if (child != bad_index)
			return node_iterator{ _data, child };
		else
			return end();
	}

	node_iterator node::end() const noexcept
	{
		return node_iterator{};
	}

	std::size_t node::size() const noexcept
	{
		auto size = std::size_t{};
		auto child = _data->tables[_index].child;
		while (child != bad_index)
		{
			++size;
			child = _data->tables[child].next;
		}
		return size;
	}

	const std::string& node::as_string() const noexcept
	{
		return _data->tables[_index].name;
	}

	std::int64_t node::as_int() const
	{
		try
		{
			return std::get<std::int64_t>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	double node::as_floating() const
	{
		try
		{
			return std::get<double>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	bool node::as_boolean() const
	{
		try
		{
			return std::get<bool>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	date_time node::as_date_time() const
	{
		try
		{
			return std::get<date_time>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	local_date_time node::as_date_time_local() const
	{
		try 
		{
			return std::get<local_date_time>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	date node::as_date_local() const
	{
		try
		{
			return std::get<date>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	time node::as_time_local() const
	{
		try
		{
			return std::get<time>(_data->tables[_index].value);
		}
		catch (const std::bad_variant_access& e)
		{
			throw wrong_type{ e.what() };
		}
	}

	node node::get_root_table() const noexcept
	{
		return node{ _data, {} };
	}

	void insert_bad(detail::toml_internal_data& d)
	{
		d.tables.emplace_back(internal_node{ {}, node_type::bad_type });
		return;
	}

	template<bool NoThrow>
	index_t insert_child(detail::toml_internal_data& d, const index_t parent, detail::internal_node n, table_def table_type = table_def::not_table)
	{
		assert(parent != bad_index);
		const auto new_index = size(d.tables);
		auto& p = d.tables[parent];
		auto allow_duplicates = p.type == node_type::array || p.type == node_type::array_tables;
		if (p.child != bad_index)
		{
			auto child = p.child;
			while (true)
			{
				auto& child_ref = d.tables[child];
				if (child_ref.name == n.name)
				{
					if (child_ref.type == node_type::table && 
						n.type == node_type::table &&
						!child_ref.closed &&
						p.table_type == n.table_type)
						return child;

					if (!allow_duplicates)
					{
						if constexpr (NoThrow)
							return bad_index;
						else
							throw duplicate_element{ "Tried to insert duplicate element: "s + n.name + ", into: " + p.name };
					}
				}

				if (child_ref.next == bad_index)
					break;

				child = child_ref.next;
			}

			d.tables[child].next = new_index;
		}
		else
			p.child = new_index;

		d.tables.emplace_back(std::move(n));
		return new_index;
	}

	index_t find_child(const detail::toml_internal_data& d, const index_t parent, const std::string_view s) noexcept
	{
		auto& p = d.tables[parent];
		if (p.child == bad_index)
			return bad_index;

		auto next = p.child;
		while (next != bad_index)
		{
			auto c = &d.tables[next];
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
				assert(toml_data.tables[t].type == node_type::table);
				toml_data.tables[t].closed = true;
			}
			open_tables.clear();
		}

		std::istream& strm;
		// stack is never empty, but may contain table->key->inline table->key->array->etc.
		std::vector<index_t> stack;
		// tables that need to be closed when encountering the next table header
		std::vector<index_t> open_tables;
		std::vector<token_type> token_stream;
		//index_t parent = bad_index;
		//index_t prev_sibling = bad_index;
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
			strm.strm.ignore();
			return true;
		}

		return ch == '\n';
	}

	// TOML says that \n and \r are forbidden in comments
	// in reality \n and \r\n marks the end of a comment
	constexpr auto comment_forbidden_chars = std::array<char, 32>{
		//U+0000 to U+0008
		'\0', 1, 2, 3, 4, 5, 6, '\a', '\b',
		//U+000A to U+001F
		'\n', '\v', '\f', '\r', 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		//U+007F
		127
	};

	//test ch against the list of forbidden chars above
	constexpr bool comment_forbidden_char(char ch) noexcept
	{
		return ch >= 0 && ch < 9 ||
			ch > 9 && ch < 32 ||
			ch == 127;
	} 

	constexpr auto utf8_error_char = 0xD7FF17;
	constexpr bool valid_utf8_char(int val) noexcept
	{
		return (val >= 0 && val < 0xD800) ||
			(val > 0xDFFF && val <= 0xD7FF16) ||
			(val >= 0xE00016 && val <= 0x10FFFF);
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
	static index_t insert_child_table(index_t parent, std::string name, detail::toml_internal_data& d, table_def t)
	{
		auto table = detail::internal_node{ std::move(name) , node_type::table };
		table.closed = false;
		return insert_child<NoThrow>(d, parent, std::move(table), t);
	}

	template<bool NoThrow>
	static index_t insert_child_table_array(index_t parent, std::string name, detail::toml_internal_data& d)
	{
		if (auto* node = &d.tables[parent];
			node->child != bad_index)
		{
			auto child = node->child;
			node = &d.tables[child];

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
				node = &d.tables[child];
			}

			//create array
			if (node->name != name)
			{
				parent = insert_child<NoThrow>(d, parent, internal_node{ std::move(name), node_type::array_tables}, table_def::header);
				if constexpr (NoThrow)
				{
					if (parent == bad_index)
						return parent;
				}
				node = &d.tables[parent];
			}
		}
		else
		{
			//create array as child
			parent = insert_child<NoThrow>(d, parent, internal_node{ std::move(name), node_type::array_tables});
			if constexpr (NoThrow)
			{
				if (parent == bad_index)
					return parent;
			}
			node = &d.tables[parent];
		}

		return insert_child<NoThrow>(d, parent, 
			internal_node{ {} , node_type::table });
	}

	template<bool NoThrow>
	static index_t insert_child_key(index_t parent, std::string name, detail::toml_internal_data& d)
	{
		auto key = detail::internal_node{ std::move(name), node_type::key};
		return insert_child<NoThrow>(d, parent, std::move(key));
	}

	constexpr auto escape_codes_raw = std::array{
		"\\b"sv, "\\t"sv, "\\n"sv, "\\f"sv, "\\r"sv, "\\\""sv, "\\\\"sv // "\\uXXXX" "\\UXXXXXXXX"
	};

	constexpr auto escape_codes_char = std::array{
		'\b', '\t', '\n', '\f', '\r', '\"', '\\' // "\\uXXXX" "\\UXXXXXXXX"
	};

	const auto unicode_bad_conversion = "conversion_error"s;

	// replace string chars with proper escape codes
	template<bool NoThrow>
	static bool replace_escape_chars(std::string& s) noexcept(NoThrow)
	{
		static_assert(size(escape_codes_raw) == size(escape_codes_char));
		constexpr auto codes_size = size(escape_codes_raw);

		auto pos = std::size_t{};
		while (pos < size(s))
		{
			const auto code_beg = s.find("\\", pos);
			if (code_beg == std::string::npos)
				break; //we're done

			const auto code_mid = code_beg + 1;
			if (code_mid >= size(s))
				break;// TODO: unmatched escape char

			auto code_end = code_mid + 1;
			auto found_code = false;
			for (auto i = std::size_t{}; i < codes_size; ++i)
			{
				if (escape_codes_raw[i] == std::string_view{ &s[code_beg], &s[code_end] })
				{
					s.replace(code_beg, code_beg + 2,
						1, escape_codes_char[i]);
					found_code = true;
					pos = code_beg + 1;
					break;
				}
			}

			if (!found_code)
			{
				// Unicode chars are 1-4 bytes, the escape code is at least 6 chars
				// so the resulting string will always be shorter, string won't have
				// to alloc.
				if (const auto unicode_char = s[code_mid]; 
					unicode_char == 'u' || unicode_char == 'U')
				{
					if (s[code_mid] == 'u') // \uXXXX
						code_end = code_mid + 5;
					else if (s[code_mid] == 'U') // \UXXXXXXXX
						code_end = code_mid + 9;

					if (size(s) < code_end)
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Invalid unicode escape code: "s << std::string_view{ &s[code_beg], &s[size(s) - 1] } << '\n';
							return false;
						}
						else
							throw unexpected_character{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], &s[size(s) - 1] } };
					}

					auto int_val = int{};
					const auto result = std::from_chars(&s[code_mid + 1], &s[code_end], int_val, 16);
					if (result.ptr != &s[code_end] ||
						result.ec == std::errc::result_out_of_range ||
						!valid_utf8_char(int_val))
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Invalid unicode escape code: "s << std::string_view{ &s[code_beg], &s[code_end] } << '\n';
							return false;
						}
						else
							throw unexpected_character{ "Invalid unicode escape code: "s + std::string{ &s[code_beg], &s[code_end + 1] } };
					}

					auto cvt = std::wstring_convert<std::codecvt_utf8<char32_t>, char32_t>{ unicode_bad_conversion };
					const auto u8 = cvt.to_bytes(int_val);
					if (u8 == unicode_bad_conversion)
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Failed to convert unicode escape code to utf-8: "s << std::string_view{ &s[code_beg], &s[code_end] } << '\n';
							return false;
						}
						else
							throw parser_error{ "Failed to convert unicode escape code to utf-8: "s + std::string{ &s[code_beg], &s[code_end] } };
					}

					s.replace(code_beg, code_end - code_beg, u8);
					pos += size(u8);
					continue;
				}

				if constexpr (NoThrow)
					std::cerr << "Illigal escape code in quoted string: \"\\"s << s[code_mid] << "\"\n"s;
				else
					throw parser_error{ "Illigal escape code in quoted string: \"\\"s + s[code_mid] + "\""s };
			}

			++pos;
		}

		return true;
	}

	template<bool DoubleQuoted>
	static std::optional<std::string> get_quoted_str(parser_state& strm)
	{
		constexpr auto delim = DoubleQuoted ? '\"' : '\'';
		auto out = std::string{};
		while (strm.strm.good())
		{
			const auto [ch, eof] = strm.get_char<true>();
			if (eof)
				return {}; // illigal character in string
			if (newline(strm, ch))
			{
				strm.nextline();
				return {}; // illigal character in string
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
				name = get_quoted_str<true>(strm);
				if (!name || !replace_escape_chars<NoThrow>(*name))
					return {};

				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if (eof || ch != '\"')
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Unexpected end of quoted string: "s << name.value_or("\"\""s) << '\n';
						return {};
					}
					else
						throw unexpected_character{ "Unexpected end of quoted string"s };
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
						return {};
					}
					else
						throw unexpected_character{ "Illigal character in name: \'" };
				}
				name = get_quoted_str<false>(strm);
				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if (eof || ch != '\'')
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Unexpected end of literal string: "s << name.value_or("\"\""s) << '\n';
						return {};
					}
					else
						throw unexpected_character{ "Unexpected end of literal string"s };
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
						return {};
					}
					else
						throw parser_error{ "Missing name" };
				}
				else
				{
					auto child = find_child(d, parent, *name);
					if (child == bad_index)
						child = insert_child_table<NoThrow>(parent, *name, d, table_def::dot);
					else if(const auto& c = d.tables[child];
						c.type == node_type::array_tables)
					{
						if constexpr (!Table)
						{
							if constexpr (NoThrow)
								std::cerr << "Name heirarchy for keys shouldn't include table arrays"s;
							else
								throw parser_error{ "Name heirarchy for keys shouldn't include table arrays"s };
						}

						child = c.child;
						while (true)
						{
							auto* node = &d.tables[child];
							if (node->next == bad_index)
								break;
							child = node->next;
						}
					}

					auto& c = d.tables[child];
					if (c.closed && c.table_type == table_def::header)
					{
						if constexpr (NoThrow)
						{
							std::cerr << "Using dotted keys to add to a previously defined table is not allowed\n"s;
							return {};
						}
						else
							throw parser_error{ "Using dotted keys to add to a previously defined table is not allowed"s };
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
					return {};
				}
				else
					throw parser_error{ "Unexpected character in name"s };
			}
		}

		return { parent, name };
	}

	enum class match_index 
	{
		date = 1,
		year,
		month,
		day,
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

	using reg_matches = std::match_results<std::string_view::iterator>;

	static std::optional<date> fill_date(const reg_matches& matches) noexcept
	{
		auto out = date{};
		assert(matches[static_cast<std::size_t>(match_index::date)].matched);
		const auto& years = matches[static_cast<std::size_t>(match_index::year)];
		assert(years.matched);
		auto ret = std::from_chars(&*years.first, &*years.second, out.year);
		if (ret.ptr == &*years.second)
		{
			const auto& months = matches[static_cast<std::size_t>(match_index::month)];
			assert(months.matched);
			ret = std::from_chars(&*months.first, &*months.second, out.month);
			if (ret.ptr == &*months.second)
			{
				const auto& days = matches[static_cast<std::size_t>(match_index::day)];
				assert(days.matched);
				ret = std::from_chars(&*days.first, &*days.first + days.length(), out.day);
				if (ret.ptr == &*days.first + days.length())
					return out;
			}
		}

		return {};
	}

	static std::optional<time> fill_time(const reg_matches& matches) noexcept
	{
		auto out = time{};
		assert(matches[static_cast<std::size_t>(match_index::time)].matched);
		const auto& hours = matches[static_cast<std::size_t>(match_index::hours)];
		assert(hours.matched);
		auto ret = std::from_chars(&*hours.first, &*hours.second, out.hours);
		if (ret.ptr == &*hours.second)
		{
			const auto& minutes = matches[static_cast<std::size_t>(match_index::minues)];
			assert(minutes.matched);
			ret = std::from_chars(&*minutes.first, &*minutes.second, out.minutes);
			if (ret.ptr == &*minutes.second)
			{
				const auto& seconds = matches[static_cast<std::size_t>(match_index::seconds)];
				assert(seconds.matched);
				ret = std::from_chars(&*seconds.first, &*seconds.first + seconds.length(), out.seconds);
				if (ret.ptr == &*seconds.first + seconds.length())
				{
					const auto& seconds_frac = matches[static_cast<std::size_t>(match_index::seconds_frac)];
					if (seconds_frac.matched)
					{
						ret = std::from_chars(&*seconds_frac.first, &*seconds_frac.first + seconds_frac.length(), out.seconds_frac);
						if (ret.ptr == &*seconds_frac.first + seconds_frac.length())
							return out;
					}
					else
						return out;
				}
			}
		}

		return {};
	}

	static std::optional<local_date_time> fill_date_time(const reg_matches& matches) noexcept
	{
		const auto date = fill_date(matches);
		const auto time = fill_time(matches);
		if (date && time)
			return local_date_time{ *date, *time };
	
		return {};
	}

	static std::pair<value_type, variant_t> get_value_type(std::string_view str) noexcept
	{
		if (empty(str))
			return { value_type::bad, {} };

		//keywords
		//	true, false
		if (str == "true"sv)
			return { value_type::boolean, true };
		if (str == "false"sv)
			return { value_type::boolean, false };
		//	inf, +inf, -inf
		if (str == "inf"sv || str == "+inf"sv)
			return { value_type::floating_point, std::numeric_limits<double>::infinity() };
		if (str == "-inf"sv)
			return { value_type::floating_point, -std::numeric_limits<double>::infinity()};
		//	nan, +nan, -nan
		if(str == "nan"sv || str == "+nan"sv || str == "-nan"sv)
			return { value_type::floating_point, std::numeric_limits<double>::quiet_NaN() };
		
		const auto beg = begin(str);
		const auto end = std::end(str);

		// also removes leading '+'
		const auto remove_underscores = [](std::string_view sv) noexcept {
			auto str = std::string{ sv };
			for (auto iter = begin(str); iter != std::end(str); ++iter)
			{
				if (*iter == '_')
					iter = str.erase(iter);
			}
			if (!empty(str) && str.front() == '+')
				str.erase(begin(str));
			return str;
		};

		// matches all valid integers with optional underscores after the first digit
		//  -normal digits(with no leading 0)
		//	-hex
		//	-binary
		//	-oct
		// doesn't check for min/max value
		constexpr auto int_reg =
			R"(^[\+\-]?[1-9]+(_?([\d])+)*$|^0x[\dA-Fa-f]+(_?[\dA-Fa-f]*)*|0b[01]+(_?[01]*)*|0o[0-7]+(_?([0-7])+)*|^[\+\-]?0$)";

		if (std::regex_match(beg, end, std::regex{ int_reg }))
		{
			auto string = std::invoke(remove_underscores, str);
			
			auto base = 10;
			if (size(string) > 1)
			{
				const auto base_chars = std::string_view(begin(string), begin(string) + 2);
				if (base_chars == "0x"sv)
					base = 16;
				else if (base_chars == "0b"sv)
					base = 2;
				else if (base_chars == "0o"sv)
					base = 8;

				if (base != 10)
					string.erase(0, 2);
			}
			
			//convert to int
			auto int_val = std::int64_t{};
			const auto string_end = &string[0] + size(string);
			auto ret = std::from_chars(&string[0], string_end, int_val, base);

			if (ret.ptr == string_end)
				return { value_type::integer, {int_val} };
			else if(ret.ec == std::errc::invalid_argument)
				return { value_type::bad, {} };
			else if (ret.ec == std::errc::result_out_of_range)
				return { value_type::out_of_range, {} };
		}
		
		//floating point(double)
		constexpr auto float_reg = R"(^[\+\-]?([1-9]+(_([\d])+)*|0)(\.[\d]+(_[\d])*)?([eE][\+\-]?[\d]+(_[\d]+)?)?$)";
		if (std::regex_match(beg, end, std::regex{ float_reg }))
		{
			const auto string = std::invoke(remove_underscores, str);
			auto floating_val = double{};
			const auto string_end = &string[0] + size(string);
			const auto ret = std::from_chars(&string[0], string_end, floating_val);
			if (ret.ptr == string_end)
				return { value_type::floating_point, {floating_val} };
			else if (ret.ec == std::errc::invalid_argument)
				return { value_type::bad, {} };
			else if (ret.ec == std::errc::result_out_of_range)
				return { value_type::out_of_range, {} };
		}

		constexpr auto date_time_reg =
			R"(^((\d{4})-(\d{2})-(\d{2}))?[T t]?((\d{2}):(\d{2}):(\d{2})(\.\d+)?)?(([zZ])|(([\+\-])(\d{2}):(\d{2})))?)";
		if (auto matches = std::match_results<std::string_view::iterator>{};
			std::regex_match(beg, end, matches, std::regex{ date_time_reg }))
		{
			//date
			const auto& date = matches[static_cast<std::size_t>(match_index::date)];
			const auto& time = matches[static_cast<std::size_t>(match_index::time)];
			const auto& offset = matches[static_cast<std::size_t>(match_index::offset)];
		
			const auto offset_date_time = date.matched &&
				time.matched &&
				offset.matched;
			const auto local_date_time = date.matched &&
				time.matched;
			const auto local_date = date.matched;
			const auto local_time = time.matched;

			if (offset_date_time)
			{
				const auto dt = fill_date_time(matches);
				if (dt)
				{
					auto odt = date_time{ *dt, false};
					const auto& off_z = matches[static_cast<std::size_t>(match_index::off_z)];
					if (off_z.matched)
					{
						// default offset
						odt.offset_hours = {};
						odt.offset_minutes = {};
						odt.offset_positive = true;
						return { value_type::date_time, odt };
					}

					const auto& off_sign = matches[static_cast<std::size_t>(match_index::off_sign)];
					assert(off_sign.matched);
					if (*off_sign.first == '+')
						odt.offset_positive = true;
					else if (*off_sign.first != '-')
						return { value_type::bad, {} };

					const auto& hours = matches[static_cast<std::size_t>(match_index::off_hours)];
					assert(hours.matched);
					auto ret = std::from_chars(&*hours.first, &*hours.second, odt.offset_hours);
					if (ret.ptr == &*hours.second)
					{
						const auto& minutes = matches[static_cast<std::size_t>(match_index::off_minues)];
						assert(minutes.matched);
						ret = std::from_chars(&*minutes.first, &*minutes.first + minutes.length(), odt.offset_minutes);
						if (ret.ptr == &*minutes.first + minutes.length())
							return { value_type::date_time, odt };
					}
				}
			}
			else if (local_date_time)
			{
				auto dt = fill_date_time(matches);
				if (dt)
					return { value_type::local_date_time, *dt };
			}
			else if (local_date)
			{
				auto d = fill_date(matches);
				if (d)
					return { value_type::local_date, *d };
			}
			else if (local_time)
			{
				auto t = fill_time(matches);
				if (t)
					return { value_type::local_time, *t };
			}
		}

		return { value_type::bad, {} };
	}

	// for parsing keywords, dates or numerical values
	template<bool NoThrow>
	static bool parse_unquoted_value(parser_state& strm, detail::toml_internal_data& toml_data)
	{
		auto out = std::string{};
		auto ch = char{};
		auto eof = bool{};
		const auto parent = strm.stack.back();
		const auto parent_type = toml_data.tables[parent].type;
		const auto array_elm = parent_type == node_type::array;
		auto type = value_type::unknown;
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

			if (array_elm)
			{
				if (ch == ',' || ch == ']')
				{
					strm.putback(ch);
					break;
				}

				if (newline(strm, ch))
				{
					if constexpr (NoThrow)
					{
						std::cerr << "Unexpected newline in array element\n"s;
						return false;
					}
					else
						throw parser_error{ "Unexpected newline in array element"s };
				}
			}
			else if (newline(strm, ch))
			{
				strm.nextline();
				break;
			}

			out.push_back(ch);
		}

		while (!empty(out) && (out.back() == '\t' || out.back() == ' '))
			out.pop_back();

		if (!out.empty())
		{
			auto [type, value] = get_value_type(out);
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
					throw parser_error{ "Error parsing value"s };
			}

			strm.token_stream.push_back(token_type::value);
			return insert_child<NoThrow>(toml_data, parent, 
				internal_node{
					std::move(out), node_type::value,
					type, std::move(value)
				}) != bad_index;
		}

		return false;
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

			if (size(str) > 2)
			{
				auto rb = rbegin(str);
				const auto re = rend(str);
				while (*rb == quote_char && rb != re)
					++rb;

				//
				const auto dist = distance(rbegin(str), rb) + (ch == quote_char ? 1 : 0);
				const auto end = dist > 2 && dist < 6;
				if (end && ch != quote_char)
				{
					// implements '\' behaviour for double quoted strings
					if constexpr (DoubleQuote)
					{
						const auto last = str.find_last_of('\\');
						if (last != std::string::npos)
						{
							auto prev = last;
							auto count = 1;
							while (prev > 0 && str[--prev] == '\\')
								++count;

							if (count % 2 != 0)
							{
								str.erase(last, size(str) - last);

								std::tie(ch, eof) = strm.get_char<NoThrow>();
								if constexpr (NoThrow)
								{
									if (eof)
										return {};
								}

								if (whitespace(ch, strm))
									continue;
							}
						}
					} // !doublequote

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
			strm.strm.ignore();
			if (strm.strm.peek() == quote_char)
			{
				strm.strm.ignore();
				str = multiline_string<NoThrow, DoubleQuote>(strm);
				if (!str)
					return false;

				if constexpr (DoubleQuote)
					replace_escape_chars<NoThrow>(*str);
			}
			else
				str = std::string{};
		}
		else
		{
			//start normal quote str
			str = get_quoted_str<DoubleQuote>(strm);
			if (!str)
			{
				if constexpr (NoThrow)
				{
					std::cerr << "Illigal character in quoted string"s;
					return false;
				}
				else
					throw unexpected_character{ "Illigal character in quoted string"s };
			}

			if constexpr (DoubleQuote)
			{
				if (!replace_escape_chars<NoThrow>(*str))
					return false;
			}

			if (strm.strm.peek() == quote_char)
				strm.strm.ignore();
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
		insert_child<NoThrow>(toml_data, strm.stack.back(), internal_node{ std::move(*str), node_type::value, value_type::string });
		strm.token_stream.emplace_back(token_type::value);

		return true;
	}

	std::int32_t parse_unicode_char(char c, parser_state& strm)
	{
		int bytes = 1;
		if (c | 0b10000000)
			++bytes;
		else
		{
			//not unicode
			return utf8_error_char;
		}

		if (c | 0b01000000)
			++bytes;
		if (c | 0b00100000)
			++bytes;

		assert(bytes >= 1);
		auto out = std::int32_t{};
		switch (bytes)
		{
		case 2:
			out = c & 0b00011111;
			out = out << 5;
			break;
		case 3:
			out = c & 0b00001111;
			out = out << 4;
			break;
		case 4:
			out = c & 0b00000111;
			out = out << 3;
			break;
		}

		while (bytes > 1)
		{
			auto [ch, eof] = strm.get_char<true>();
			if (eof)
				return utf8_error_char;
			out |= ch & 0b00111111;
			out = out << 6;
			--bytes;
		}

		if (valid_utf8_char(out))
			return out;

		return utf8_error_char;
	}

	constexpr bool is_unicode(char c) noexcept
	{
		return c & 0b10000000;
	}

	static bool parse_comment(parser_state& strm) noexcept
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
				std::cerr << "Forbidden character in comment: " << ch << '(' << static_cast<int>(ch) << ")\n";
				return false;
			}

			if (is_unicode(ch))
			{
				auto u_ch = parse_unicode_char(ch, strm);
				if (u_ch == utf8_error_char)
				{
					std::cerr << "Invalid unicode character(s) in stream\n"s;
					return false;
				}
			}
		}

		return true;
	}

	template<bool NoThrow>
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

			if (ch == '#')
			{
				parse_comment(strm);
				continue;
			}

			//get value
			strm.putback(ch);
			if (!parse_value<NoThrow>(strm, toml_data))
				return false;
		
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return false;
			}

			if (whitespace(ch, strm))
			{
				std::tie(ch, eof) = strm.get_char<NoThrow>();
				if constexpr (NoThrow)
				{
					if (eof)
						return false;
				}
			}

			if (ch == ',')
			{
				strm.token_stream.emplace_back(token_type::comma);
				continue;
			}

			if (ch == ']')
			{
				strm.token_stream.emplace_back(token_type::array_end);
				strm.stack.pop_back(); 
				return true;
			}

			if (newline(strm, ch))
			{
				strm.nextline();
				continue;
			}

			return false;
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
		const auto& p = toml_data.tables[parent];
		assert(p.type == node_type::key || p.type == node_type::array);
		const auto table = insert_child<NoThrow>(toml_data, parent, internal_node{ {}, node_type::inline_table });
		strm.stack.emplace_back(table);
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

				toml_data.tables[table].closed = true;
				assert(table == strm.stack.back());
				strm.stack.pop_back();
				return true;
			}

			strm.putback(ch);
			if (!parse_key_value<NoThrow>(strm, toml_data))
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

	template<bool NoThrow>
	static bool parse_key_value(parser_state& strm, toml_internal_data& toml_data)
	{
		auto key_str = parse_key_name<NoThrow>(strm, toml_data);

		if (!key_str.name)
		{
			if constexpr (NoThrow)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
			else
				throw parser_error{ "Error parsing key name"s };
		}

		auto [ch, eof] = strm.get_char<NoThrow>();

		if constexpr (NoThrow)
		{
			if (eof || !key_str.name)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
		}

		if (toml_data.tables[key_str.parent].closed == false)
			strm.open_tables.emplace_back(key_str.parent);

		const auto key_index = insert_child_key<NoThrow>(key_str.parent, std::move(*key_str.name), toml_data);
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

		//look for '='
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
			if (eof || !key_str.name)
			{
				insert_bad(toml_data);
				std::cerr << "Error getting key name\n"s;
				return false;
			}
		}

		if (!whitespace(ch, strm))
			strm.putback(ch);

		const auto ret = parse_value<NoThrow>(strm, toml_data);
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

	template<bool NoThrow>
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
			ret = parse_unquoted_value<NoThrow>(strm, toml_data);
		}

		const auto parent_type = toml_data.tables[strm.stack.back()].type;
		if (parent_type == node_type::key)
			strm.stack.pop_back();
		return ret;
	}

	template<bool NoThrow>
	static key_name parse_table_header(parser_state& strm, toml_internal_data& toml_data)
	{
		const auto name = parse_key_name<NoThrow, true>(strm, toml_data);
		auto [ch, eof] = strm.get_char<NoThrow>();
		if constexpr (NoThrow)
		{
			if (eof)
				return {};
		}

		if (whitespace(ch, strm))
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					return{};
			}
		}

		if (ch != ']')
		{
			if constexpr (NoThrow)
			{
				std::cerr << "Unexpected character, was expecting ']'; found: " << ch << '\n';
				return {};
			}
			else
				throw unexpected_character{ "Unexpected character, was expecting ']'" };
		}

		if (const auto table = find_table(*name.name, name.parent, toml_data);
			table != bad_index && toml_data.tables[table].closed == true)
		{
			if constexpr (NoThrow)
			{
				std::cerr << "This table: " << *name.name << "; has already been defined\n";
				return {};
			}
			else
				throw parser_error{ "table redefinition" };
		}

		return name;
	}

	template<bool NoThrow>
	static node parse_toml(std::istream& strm)
	{
		auto toml_data = std::make_shared<detail::toml_internal_data>();
		auto& t = toml_data->tables;
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
		t.emplace_back(std::string{}, node_type::table);
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
				if (p_state.strm.peek() == '[')//array of tables
				{
					p_state.strm.ignore();
					p_state.stack.pop_back();
					p_state.close_tables(*toml_data);
					auto header = parse_table_header<NoThrow>(p_state, *toml_data);
					auto& name = header.name;
					if (!name)
					{
						if constexpr (NoThrow)
						{
							insert_bad(*toml_data);
							std::cerr << "Error getting table name\n";
							break;
						}
						else
							throw parser_error{ "Error getting table name" };
					}

					auto& parent = header.parent;
					auto table = insert_child_table_array<NoThrow>(header.parent, std::move(*name), *toml_data);
					p_state.stack.emplace_back(table);
					p_state.token_stream.push_back(token_type::array_table);
					p_state.open_tables.emplace_back(table);

					std::tie(ch, eof) = p_state.get_char<NoThrow>();

					if constexpr (NoThrow)
					{
						if (eof)
						{
							insert_bad(*toml_data);
							std::cerr << "Unexpected eof in table array name\n"s;
							break;
						}
					}

					if (ch != ']')
					{
						if constexpr (NoThrow)
						{
							insert_bad(*toml_data);
							std::cerr << "Unexpected character; expected ']'\n"s;
							break;
						}
						else
							throw unexpected_character{ "Unexpected character while parsing table name; expected: ']'"s };
					}

					continue;
				}
				else
				{
					p_state.stack.pop_back();
					p_state.close_tables(*toml_data);

					auto table_header = parse_table_header<NoThrow>(p_state, *toml_data);
					auto& name = table_header.name;
					
					if (!name)
					{
						if constexpr (NoThrow)
						{
							insert_bad(*toml_data);
							std::cerr << "Error getting table name\n";
							break;
						}
						else
							throw parser_error{ "Error getting table name" };
					}

					auto table = find_table(*name, table_header.parent, *toml_data);

					if(table == bad_index)
						table = insert_child_table<NoThrow>(table_header.parent, std::move(*name), *toml_data, table_def::header);
					p_state.stack.emplace_back(table);
					p_state.open_tables.emplace_back(table);
					p_state.token_stream.emplace_back(token_type::table);
					continue;
				}
			}

			if (ch == '#')
			{
				if (parse_comment(p_state))
					continue;
				else
				{
					if constexpr (NoThrow)
					{
						insert_bad(*toml_data);
						break;
					}
					else
						throw unexpected_character{ "Illigal character in comment"s };
				}
			}

			p_state.putback(ch);
			if (parse_key_value<NoThrow>(p_state, *toml_data))
			{
				std::tie(ch, eof) = p_state.get_char<true>();
				if (eof)
					break;

				// FIX: for key=vallue key=value on same line
				if (whitespace(ch, p_state))
				{
					std::tie(ch, eof) = p_state.get_char<true>();
					if (eof)
						break;
				}

				if (!newline(p_state, ch))
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
				//ENDFIX
			}
			else
			{
				insert_bad(*toml_data);
				break;
			}
		}

		if (toml_data->tables.back().type == node_type::bad_type)
			return {};

		return { std::move(toml_data), {} };
	}

	template<bool NoThrow>
	node parse(std::istream& strm)
	{
		if (!strm.good())
			return {};

		return parse_toml<NoThrow>(strm);
	}

	template<bool NoThrow>
	node parse(std::string_view toml)
	{
		auto strstream = std::stringstream{ std::string{ toml }, std::ios_base::in };
		return parse<NoThrow>(strstream);
	}

	/*template<bool NoThrow>
	node parse(const std::filesystem::path& path)
	{
		if (!std::filesystem::exists(path))
			return {};

		if (std::filesystem::is_directory(path))
			return {};

		auto strm = std::ifstream{ path };
		return parse<NoThrow>(strm);
	}*/

	node parse(std::string_view toml)
	{
		return parse<false>(toml);
	}

	node parse(std::istream& strm)
	{
		return parse<false>(strm);
	}

	/*node parse(const std::filesystem::path& path)
	{
		return parse<false>(path);
	}*/

	node parse(std::string_view toml, detail::no_throw_t) noexcept
	{
		return parse<true>(toml);
	}

	node parse(std::istream& strm, detail::no_throw_t) noexcept
	{
		return parse<true>(strm);
	}

	/*node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept
	{
		return parse<true>(filename);
	}*/
}
