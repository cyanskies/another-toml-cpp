#ifndef ANOTHER_TOML_HPP
#define ANOTHER_TOML_HPP

#include <filesystem>
#include <istream>
#include <memory>
#include <string_view>

namespace another_toml
{
	namespace detail
	{
		struct internal_node;
		struct toml_internal_data;

		struct no_throw_t {};
	}

	class toml_node 
	{
	public:
		toml_node(std::shared_ptr<detail::toml_internal_data> shared_data = {},
			detail::internal_node* node = {})
			: _data{ shared_data }, _node{ node } {}

		std::string_view name() const;

		std::vector<toml_node> children();
		//std::string_view full_name

		operator bool() const noexcept
		{
			return static_cast<bool>(_data);
		}

	private:
		mutable std::shared_ptr<detail::toml_internal_data> _data;
		detail::internal_node* _node = {};
	};

	constexpr auto no_throw = detail::no_throw_t{};

	toml_node parse(std::string_view toml);
	toml_node parse(std::istream&); 
	toml_node parse(const std::filesystem::path& filename);

	toml_node parse(std::string_view toml, detail::no_throw_t) noexcept;
	toml_node parse(std::istream&, detail::no_throw_t) noexcept;
	toml_node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept;
	
}

#include "another_toml.inl"

#endif // !ANOTHER_TOML_HPP
