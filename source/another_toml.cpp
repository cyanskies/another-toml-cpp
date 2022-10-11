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

	constexpr auto bad_index = std::numeric_limits<detail::index_t>::max();

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

	namespace detail
	{	
		using variant_t = std::variant<std::monostate, std::int64_t, double, bool,
			//dates,
			date_time, local_date_time, date, time>;

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
			bool closed = false; 
		};

		struct toml_internal_data
		{
			static constexpr auto root_table = index_t{};
			std::vector<internal_node> tables;
		};
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

	void insert_bad(detail::toml_internal_data& d)
	{
		d.tables.emplace_back(internal_node{ {}, node_type::bad_type });
		return;
	}

	template<bool NoThrow>
	index_t insert_child(detail::toml_internal_data& d, const index_t parent, detail::internal_node n)
	{
		assert(parent != bad_index);
		const auto new_index = size(d.tables);
		auto& p = d.tables[parent];
		if (p.child != bad_index)
		{
			detail::internal_node* child = &d.tables[p.child];
			while (child->next != bad_index)
			{
				if (child->type == n.type &&
					child->name == n.name)
				{
					if constexpr (NoThrow)
						return bad_index;
					else
						throw duplicate_element{ "Tried to insert duplicate element: "s + n.name + ", into: " + p.name };
				}

				child = &d.tables[child->next];
			}

			child->next = new_index;
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

	enum class mode
	{
		key_name,
		value,
		root,
	};

	// NOTE: tokens are only used at the highest level
	//		of the textual heirarchy,
	//		so tables, arrays of tables, keys
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

	struct token
	{
		index_t parent;
		token_type type;
		std::optional<std::string> value;
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

			++col;
			return { static_cast<char>(val), {} };
		}

		void nextline() noexcept
		{
			col = {};
			++line;
			return;
		}

		void putback(char ch) noexcept
		{
			--col;
			strm.putback(ch);
			return;
		}

		std::istream& strm;
		token prev;
		mode mode = mode::root;
		// stack is never empty, but may contain table->key->inline table->key->array->etc.
		std::vector<index_t> stack;
		// tables that need to be closed when encountering the next table header
		std::vector<index_t> open_tables;
		std::vector<token_type> token_stream;
		index_t parent = bad_index;
		index_t prev_sibling = bad_index;
		std::size_t line = {};
		std::size_t col = {};
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
		return val >= 0 && val <= 0xD7FF16 || (val >= 0xE00016 && val <= 0x10FFFF);
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
	static index_t insert_child_table(index_t parent, std::string name, detail::toml_internal_data& d)
	{
		auto table = detail::internal_node{ std::move(name) , node_type::table };
		return insert_child<NoThrow>(d, parent, std::move(table));
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
					//don't break on excaped quote: '\"'
					if (out.back() != '\\')
					{
						strm.putback(ch);
						break;
					}
					// but do break if escaped slash followed by quote: "\\""
					else if (out[size(out) - 2] == '\\')
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

	template<bool NoThrow>
	static key_name parse_key_name(parser_state& strm, detail::toml_internal_data& d)
	{
		auto name = std::optional<std::string>{};
		auto parent = strm.stack.back();
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
				if (!replace_escape_chars<NoThrow>(*name))
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
				// TODO: eat the trailing '\''
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
						child = insert_child_table<NoThrow>(parent, *name, d);
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

	static int chtoi(const char* first, const char* last) noexcept
	{
		auto out = int{};
		auto ret = std::from_chars(first, last, out);
		if (ret.ptr == last)
			return out;
		else if (ret.ec == std::errc::invalid_argument)
			return -1;
		else if (ret.ec == std::errc::result_out_of_range)
			return -2;
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
			R"(^[\+\-]?[1-9]+(_([\d])+)*$|^0x[\dA-Fa-f]+(_[\dA-Fa-f]*)*|0b[01]+(_[01]*)*|0o[0-7]+(_([0-7])+)*|^[\+\-]?0$)";

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
			R"(^((\d{4})-(\d{2})-(\d{2}))?[T t]?((\d{2}):(\d{2}):(\d{2})(\.\d+)?)?(([zZ]|[\+\-])(\d{2}):(\d{2}))?$)";
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
					const auto& off_sign = matches[static_cast<std::size_t>(match_index::off_sign)];
					assert(off_sign.matched);
					if (*off_sign.first == 'z' ||
						*off_sign.first == 'Z')
					{
						// default offset
						odt.offset_hours = {};
						odt.offset_minutes = {};
						odt.offset_positive = true;
						return { value_type::date_time, odt };
					}

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

			return { value_type::bad, {} };
		}

		return { value_type::unknown, {} };
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
		const auto end_on_comma = parent_type == node_type::array;
		auto type = value_type::unknown;
		while (strm.strm.good())
		{
			std::tie(ch, eof) = strm.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof)
					break;
			}

			if (newline(strm, ch))
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
					std::cerr << "Failed to parse value"s;
					return false;
				}
				else
					throw parser_error{ "Error parsing value"s };
			}

			if (type == value_type::out_of_range)
			{
				if constexpr (NoThrow)
				{
					std::cerr << "parsed value out of range"s;
					return false;
				}
				else
					throw parser_error{ "Error parsing value"s };
			}

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

			if (newline(strm, ch))
			{
				//look backwards for the end sequence
				const auto last_quote = str.find_last_of(quote_char);
				if (last_quote != std::string::npos		&&
					last_quote > 1						&&
					str[last_quote - 1] == quote_char	&&
					str[last_quote - 2] == quote_char)
				{
					str.erase(last_quote - 2, (size(str) - last_quote) + 2);
					assert(size(str) == last_quote - 2);
					return str;
				}

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
			} // !newline(ch)

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
		}

		assert(!strm.stack.empty());
		assert(str);
		insert_child<NoThrow>(toml_data, strm.stack.back(), internal_node{ std::move(*str), node_type::value, value_type::string });
		strm.token_stream.emplace_back(token_type::value);
		auto& parent_type = toml_data.tables[strm.stack.back()].type;
		if (parent_type == node_type::key)
			strm.stack.pop_back();

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
		
		if (ch == '[')
		{
			throw std::exception{ "unimpl" };
			//array start
		}

		if (ch == '{') 
		{
			throw std::exception{ "unimpl" };
			//inline table start
		}

		if (ch == '\"')
			return parse_str_value<NoThrow, true>(strm, toml_data);

		if (ch == '\'')
			return parse_str_value<NoThrow, false>(strm, toml_data);

		strm.putback(ch);
		return parse_unquoted_value<NoThrow>(strm, toml_data);
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
				strm.col = {};
				++strm.line;
				break;
			}

			if (comment_forbidden_char(ch))
			{
				std::cerr << "Forbidden character in comment: " << ch << '(' << static_cast<int>(ch) << ")\n";
				return false;
			}

			++strm.col;
		}

		return true;
	}

	template<bool NoThrow>
	static token parse_table_header(parser_state& strm, toml_internal_data& toml_data)
	{
		const auto name = parse_key_name<NoThrow>(strm, toml_data);
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

		return token{ name.parent, token_type::table, std::move(name.name) };
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
					;//parse_key_name(p_state);
					// check for extra "]"
					continue;
				}
				else
				{
					// if the current parent is key then this should be the start of an array
					auto table_header = parse_table_header<NoThrow>(p_state, *toml_data);
					auto& name = table_header.value;
					
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

					for (auto t : p_state.open_tables)
					{
						assert(toml_data->tables[t].type == node_type::table);
						toml_data->tables[t].closed = true;
					}

					p_state.open_tables.clear();
					// we must be in the root state
					const auto table = insert_child_table<NoThrow>(table_header.parent, std::move(*name), *toml_data);
					p_state.stack.emplace_back(table);
					continue;
				}
			}

			if (ch == '#')
			{
				if (parse_comment(p_state))
					continue;
				else
				{
					insert_bad(*toml_data);
					break;
				}
			}

			p_state.putback(ch);
			auto key_str = parse_key_name<NoThrow>(p_state, *toml_data);
			std::tie(ch, eof) = p_state.get_char<NoThrow>();

			if constexpr (NoThrow)
			{
				if (eof || !key_str.name)
				{
					insert_bad(*toml_data);
					std::cerr << "Error getting key name\n"s;
					break;
				}
			}

			const auto key_index = insert_child_key<NoThrow>(key_str.parent, std::move(*key_str.name), *toml_data);
			p_state.stack.emplace_back(key_index);

			if (whitespace(ch, p_state))
			{
				std::tie(ch, eof) = p_state.get_char<NoThrow>();
			}

			if constexpr (NoThrow)
			{
				if (eof)
				{
					insert_bad(*toml_data);
					std::cerr << "Error getting key name\n"s;
					break;
				}
			}

			//look for '='
			if (ch != '=')
			{
				if constexpr (NoThrow)
				{
					insert_bad(*toml_data);
					std::cerr << "key names must be followed by '='"s;
					break;;
				}
				else
					throw unexpected_character{ "key names must be followed by '='"s };
			}

			std::tie(ch, eof) = p_state.get_char<NoThrow>();
			if constexpr (NoThrow)
			{
				if (eof || !key_str.name)
				{
					insert_bad(*toml_data);
					std::cerr << "Error getting key name\n"s;
					break;
				}
			}

			if (!whitespace(ch, p_state))
				p_state.putback(ch);

			if (!parse_value<NoThrow>(p_state, *toml_data))
			{
				insert_bad(*toml_data);
				break;
			}
		}

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

	template<bool NoThrow>
	node parse(const std::filesystem::path& path)
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
