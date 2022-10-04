#include "another_toml.hpp"

#include <cassert>
#include <fstream>
#include <ranges>
#include <sstream>

namespace another_toml
{
	namespace detail
	{
		struct internal_node
		{
			// line that the table header starts at, 
			// also used as unique keys for tables
			std::streampos loc = {};
			// full name is name_prefix + name
			std::string name;
			//std::string name_prefix;
			std::vector<internal_node> child_nodes;
		};

		struct toml_internal_data
		{
			std::vector<internal_node> tables;
		};
	}

	template<bool NoThrow>
	static toml_node make_base_node(std::istream& strm) noexcept(NoThrow)
	{
		auto toml_data = std::make_shared<detail::toml_internal_data>();
		auto& t = toml_data->tables;





		auto root_map = std::ranges::find(t, std::streampos{}, &detail::internal_node::loc);
		assert(root_map != end(t));

		return { std::move(toml_data), &*root_map };
	}

	template<bool NoThrow>
	toml_node parse(std::string_view toml) noexcept(NoThrow)
	{
		auto strstream = std::stringstream{ std::string{ toml }, std::ios_base::in };
		return parse<NoThrow>(strstream);
	}

	template<bool NoThrow>
	toml_node parse(std::istream& strm) noexcept(NoThrow)
	{
		if (!strm.good())
			return {};

		return make_base_node<NoThrow>(strm);
	}

	template<bool NoThrow>
	toml_node parse(const std::filesystem::path& path) noexcept(NoThrow)
	{
		if (!std::filesystem::exists(path))
			return {};

		if (std::filesystem::is_directory(path))
			return {};

		auto strm = std::ifstream{ path };
		return parse<NoThrow>(strm);
	}

	toml_node parse(std::string_view toml)
	{
		return parse<false>(toml);
	}

	toml_node parse(std::istream& strm)
	{
		return parse<false>(strm);
	}

	toml_node parse(const std::filesystem::path& path)
	{
		return parse<false>(path);
	}

	toml_node parse(std::string_view toml, detail::no_throw_t) noexcept
	{
		return parse<true>(toml);
	}

	toml_node parse(std::istream& strm, detail::no_throw_t) noexcept
	{
		return parse<true>(strm);
	}

	toml_node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept
	{
		return parse<true>(filename);
	}
}
