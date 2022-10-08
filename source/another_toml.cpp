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
#include <sstream>
#include <stack>
#include <string_view>

using namespace std::string_literals;
using namespace std::string_view_literals;

namespace another_toml
{
	constexpr auto utf8_bom = std::array{
		0xEF, 0xBB, 0xBF
	};

	constexpr auto bad_index = std::numeric_limits<detail::index_t>::max();

	namespace detail
	{	
		struct internal_node
		{
			// includes the table names
			std::string name;
			node_type type = node_type::bad_type;
			index_t next = bad_index;
			index_t child = bad_index;
			// a closed table can still have child tables added, but not child keys
			bool closed = false; 
		};

		struct toml_internal_data
		{
			static constexpr auto root_table = index_t{};
			std::vector<internal_node> tables;
		};
	}

	using detail::index_t;

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

	index_t insert_child(detail::toml_internal_data& d, index_t parent, detail::internal_node n)
	{
		assert(parent != bad_index);
		auto new_index = size(d.tables);
		d.tables.emplace_back(std::move(n));
		auto& p = d.tables[parent];
		if (p.child != bad_index)
		{
			detail::internal_node* child = &d.tables[p.child];
			while (child->next != bad_index)
				child = &d.tables[child->next];
			child->next = new_index;
		}
		else
			p.child = new_index;

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

	enum class mode
	{
		key_name,
		value,
		root,
	};

	enum class token_type
	{
		table_begin,
		table_end,
		array_table_begin,
		array_begin,
		array_end,
		key,
		value,
		comment,
		bad
	};

	struct key_name
	{
		index_t parent;
		std::optional<std::string> name;
	};

	struct token
	{
		index_t parent;
		token_type type;
		std::optional<std::string> value;
	};

	struct parser_state
	{
		template<bool NoThrow>
		std::pair<unsigned char, bool> get_char() noexcept(NoThrow)
		{
			auto val = strm.get();
			if (val == std::istream::traits_type::eof())
			{
				if constexpr (NoThrow)
					return { {}, true };
				else
					throw unexpected_eof{ "Parser encountered an unexcpected eof."};
			}

			++col;
			return { static_cast<unsigned char>(val), {} };
		}

		std::istream& strm;
		token prev;
		mode mode = mode::root;
		// stack is never empty, but may contain table->key->inline table->key->array->etc.
		std::vector<index_t> stack;
		// tables that need to be closed when encountering the next table header
		std::vector<index_t> open_tables;
		index_t parent = bad_index;
		index_t prev_sibling = bad_index;
		std::size_t line = {};
		std::size_t col = {};
	};

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
		return val >= 0 && val <= 0xD7FF16 || (val >= 0xE00016 && val <= 0x10FFFF);
	}

	// limit to a-z, A-Z, 0-9, '-' and ' '
	constexpr bool valid_key_name_char(char ch) noexcept
	{
		return ch == '-'			||
			ch >= '0' && ch <= '9'	||
			ch >= 'A' && ch <= 'Z'	||
			ch == '_'				||
			ch >= 'a' && ch <= 'z';
	}

	//skip through whitespace
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

		strm.strm.putback(ch);
		return true;
	}

	static index_t insert_child_table(index_t parent, std::string_view name, detail::toml_internal_data& d)
	{
		auto table = detail::internal_node{ std::string{ name }, node_type::table };
		return insert_child(d, parent, std::move(table));
	}

	constexpr auto escape_codes_raw = std::array{
		"\\b"sv, "\\t"sv, "\\n"sv, "\\f"sv, "\\r"sv, "\\\""sv, "\\\\"sv // "\\uXXXX" "\\UXXXX"
	};

	constexpr auto escape_codes_char = std::array{
		'\b', '\t', '\n', '\f', '\r', '\"', '\\' // "\\uXXXX" "\\UXXXX"
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
					if (result.ptr != &s[code_end])
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

	static std::string get_quoted_name(parser_state& strm, char delim)
	{
		auto out = std::string{};
		while (strm.strm.good())
		{
			const auto [ch, eof] = strm.get_char<true>();
			if (eof)
				break;			
			if (ch == delim)
				break;

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
				strm.strm.putback(ch);
				break;
			}

			if (!valid_key_name_char(ch))
			{
				std::cerr << "Illigal character found in table/key name: " << ch << '\n';
				strm.strm.putback(ch);
				return {};
			}

			++strm.col;
			out.push_back(ch);
		}

		return out;
	}

	template<bool NoThrow>
	static key_name parse_key_name(parser_state& strm, detail::toml_internal_data& d) noexcept(NoThrow)
	{
		auto name = std::optional<std::string>{};
		auto parent = strm.stack.back();
		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<NoThrow>();
			if (eof)
				return {};

			if (whitespace(ch, strm))
				continue;

			if (ch == '=' || ch == ']')
			{
				strm.strm.putback(ch);
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
				name = get_quoted_name(strm, '\"');
				if (!replace_escape_chars<NoThrow>(*name))
					return {};

				std::tie(ch, eof) = strm.get_char();
				if (eof || ch != '\"')
				{
					;//error
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
				name = get_quoted_name(strm, '\'');
				//literals dont fix escape codes
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
						child = insert_child_table(parent, *name, d);
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
					return {};
			}
		}

		return { parent, name };
	}

	template<bool NoThrow>
	static node parse_array() noexcept(NoThrow)
	{
	}

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

	static void parse_comment(parser_state& strm) noexcept
	{
		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<true>();
			if (eof)
				return;

			if (newline(strm, ch))
			{
				strm.col = {};
				++strm.line;
				return;
			}

			if (comment_forbidden_char(ch))
			{
				std::cerr << "Forbidden character in comment: " << ch << '(' << static_cast<int>(ch) << ")\n";
			}

			++strm.col;
		}
	}

	template<bool NoThrow>
	static token parse_table_header(parser_state& strm, detail::toml_internal_data& toml_data) noexcept(NoThrow)
	{
		const auto name = parse_key_name<NoThrow>(strm, toml_data);
		auto [ch, eof] = strm.get_char<NoThrow>();
		if (eof)
			return {};

		if (whitespace(ch, strm))
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if (eof)
				return {};
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

		return token{ name.parent, token_type::table_begin, std::move(name.name) };
	}

	template<bool NoThrow>
	static token parse_token(parser_state& strm, detail::toml_internal_data& toml_data) noexcept(NoThrow)
	{
		// find the next token
		// root tokens, like a table header or key name
		// or rhs tokens(anything that can follow a '=')

		const auto parent_type = toml_data.tables[strm.stack.back()].type;
		while (strm.strm.good())
		{
			auto [ch, eof] = strm.get_char<NoThrow>();
			if (eof)
				return {};

			if (whitespace(ch, strm))
				continue;

			if(newline(strm, ch))
			{
				strm.col = {};
				++strm.line;
				continue;
			}

			if (ch == '[') // start table or array
			{
				if (strm.strm.peek() == '[')//array of tables
				{
					strm.strm.ignore();
					;//parse_key_name(strm);
					// check for extra "]"
				}
				else
				{
					// if the current parent is key then this should be the start of an array
					return parse_table_header<NoThrow>(strm, toml_data);	
				}
			}

			if (ch == '#')
			{
				parse_comment(strm);
				continue;
			}

			strm.strm.putback(ch);
			auto key_str = parse_key_name<NoThrow>(strm, toml_data);
			if (!key_str.name)
				return {};
			return { token{ key_str.parent, token_type::key, std::move(key_str.name) } };
		}

		return {};
	}

	template<bool NoThrow>
	static node parse_toml(std::istream& strm) noexcept(NoThrow)
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
		
		while(strm.good())
		{
			token tok = parse_token<NoThrow>(p_state, *toml_data);
			const auto& name = tok.value;

			if (!name)
			{
				if constexpr (NoThrow)
					std::cerr << "Error getting token name\n";
				else
					throw parser_error{ "Error getting token name" };
			}

			switch (tok.type)
			{
			case token_type::table_begin:
				//close any open tables
				for (auto t : p_state.open_tables)
				{
					assert(toml_data->tables[t].type == node_type::table);
					toml_data->tables[t].closed = true;
				}

				p_state.open_tables.clear();
				// we must be in the root state
				const auto table = insert_child_table(tok.parent, *name , *toml_data);
				p_state.stack.emplace_back(table);
				continue;
			}
		}

		return { std::move(toml_data), {} };
	}

	template<bool NoThrow>
	node parse(std::istream& strm) noexcept(NoThrow)
	{
		if (!strm.good())
			return {};

		return parse_toml<NoThrow>(strm);
	}

	template<bool NoThrow>
	node parse(std::string_view toml) noexcept(NoThrow)
	{
		auto strstream = std::stringstream{ std::string{ toml }, std::ios_base::in };
		return parse<NoThrow>(strstream);
	}

	template<bool NoThrow>
	node parse(const std::filesystem::path& path) noexcept(NoThrow)
	{
		if (!std::filesystem::exists(path))
			return {};

		if (std::filesystem::is_directory(path))
			return {};

		auto strm = std::ifstream{ path };
		return parse<NoThrow>(strm);
	}

	node parse(std::string_view toml)
	{
		return parse<false>(toml);
	}

	node parse(std::istream& strm)
	{
		return parse<false>(strm);
	}

	node parse(const std::filesystem::path& path)
	{
		return parse<false>(path);
	}

	node parse(std::string_view toml, detail::no_throw_t) noexcept
	{
		return parse<true>(toml);
	}

	node parse(std::istream& strm, detail::no_throw_t) noexcept
	{
		return parse<true>(strm);
	}

	node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept
	{
		return parse<true>(filename);
	}
}
