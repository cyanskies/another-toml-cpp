#include "another_toml.hpp"

#include <array>
#include <cassert>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
#include <variant>

namespace another_toml
{
	namespace detail
	{	
		struct internal_node
		{
			// includes the table names
			std::string name;
			node_type type;
			index_t next;
			index_t child;
			bool closed = false; // a closed table can still have child tables added, but not child keys
		};

		const auto bad_index = std::numeric_limits<index_t>::max();

		struct toml_internal_data
		{
			static constexpr auto root_table = index_t{};
			std::vector<internal_node> tables;
		};
	}

	using detail::index_t;

	index_t find_table(std::string_view name) noexcept;
	void insert_child(detail::toml_internal_data&, index_t parent, detail::internal_node n);

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
		array_begin,
		array_end,
		key_value,
		value,
		comment,
		bad
	};

	struct token
	{
		index_t parent;
		token_type type;
		std::optional<std::string> value;
	};

	struct parser_state
	{
		std::istream& strm;
		token prev;
		mode mode = mode::root;
		std::vector<node_type> stack = { node_type::table };
		index_t parent = detail::bad_index;
		index_t prev_sibling = detail::bad_index;
		std::size_t line = {};
		std::size_t col = {};
	};

	//Why is '\n' a forbidden char? That makes it impossible to end a comment
	constexpr auto comment_forbidden_chars = std::array<char, 30>{
		//U+0000 to U+0008
		'\0', 1, 2, 3, 4, 5, 6, '\a', '\b',
		//U+000A to U+001F
		/*'\n',*/ '\v', '\f', /*'\r',*/ 14, 15, 16, 17, 18, 19, 20,
		21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		//U+007F
		127
	};

	constexpr auto comment_begin = '#';
	
	constexpr bool valid_utf8_char(char) noexcept; //??? how to implement

	// limit to a-z, A-Z, 0-9, '-' and ' '
	constexpr bool valid_key_name_char(char ch) noexcept
	{
		if (ch == '-')
			return true;

		if (ch >= '0' && ch <= '9')
			return true;

		if (ch >= 'A' && ch <= 'Z')
			return true;

		if (ch == '_')
			return true;

		if (ch >= 'a' && ch <= 'z')
			return true;

		return false;
	}

	struct key_name
	{
		index_t parent;
		std::optional<std::string> name;
	};

	static std::optional<std::string> get_quoted_name(parser_state& strm, char delim)
	{
		auto out = std::optional<std::string>{};
		auto ch = strm.strm.good();
		while (strm.strm.good() && ch != delim)
		{
			if (!out)
				out = std::string{};
			out->push_back(ch);
		}

		return out;
	}

	static key_name parse_key_name(parser_state& strm)
	{
		auto name = key_name{};
		auto ch = strm.strm.good();

		while (strm.strm.good())
		{
			if (ch == '\"')
			{
				name.name = get_quoted_name(strm, '\"');

			}
			

			if (!name.name)
			{

			}

			ch = strm.strm.get();
		}
	}

	template<bool NoThrow>
	static node parse_array() noexcept(NoThrow)
	{
	}

	static bool whitespace(char ch) noexcept
	{
		return ch == ' ' || ch == '\t';
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
			auto ch = strm.strm.get();
			if (newline(strm, ch))
			{
				strm.col = {};
				++strm.line;
				return;
			}

			if (std::ranges::any_of(comment_forbidden_chars, [ch](auto& badch) {
				return ch == badch;
				}))
			{
				std::cerr << "Forbidden character in comment: " << ch << '(' << static_cast<int>(ch) << ")\n";
			}

			++strm.col;
		}
	}

	template<bool NoThrow>
	static token parse_token(parser_state& strm, detail::toml_internal_data&) noexcept(NoThrow)
	{
		// parser candidates
		// table
		// array of table
		// key
		// comment
		while (strm.strm.good())
		{
			auto ch = strm.strm.get();
			++strm.col;

			auto equal_to_ch = [ch](auto&& other) noexcept {
				return ch == other;
			};

			// whitespace
			if (whitespace(ch))
			{
				continue;
			}

			if(newline(strm, ch))
			{
				strm.col = {};
				++strm.line;
				continue;
			}

			if (ch == '[') // start table
			{

				if (strm.strm.peek() == '[')//array of tables
				{
					strm.strm.ignore();
					;//parse_key_name(strm);
				}
				else
				{
					auto name = parse_key_name(strm);
				}

					continue;
			}

			if (ch == '#')
			{
				parse_comment(strm);
			}

			strm.strm.putback(ch);
			//auto key_str = parse_key_name(strm);
		}

		return {};
	}

	template<bool NoThrow>
	static node parse_toml(std::istream& strm) noexcept(NoThrow)
	{
		auto toml_data = std::make_shared<detail::toml_internal_data>();
		auto& t = toml_data->tables;

		// implicit global table
		// always stored at index 0
		t.emplace_back(std::string{}, node_type::table);
		auto p_state = parser_state{ strm };

		auto current_index = index_t{};

		while(!strm.eof())
		{
			auto token = parse_token<NoThrow>(p_state, *toml_data);

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
