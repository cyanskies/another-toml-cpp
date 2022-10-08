#ifndef ANOTHER_TOML_HPP
#define ANOTHER_TOML_HPP

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
		using  parser_error::parser_error;
	};

	//thrown when encountering an unexpected character
	class unexpected_character :public parser_error
	{
	public:
		using  parser_error::parser_error;
	};

	namespace detail
	{
		using index_t = std::size_t;
		struct toml_internal_data;

		struct no_throw_t {};
	}

	enum class node_type
	{
		table,
		array, 
		key,
		value,
		comment, // note, we don't provide access to comments or preserve them
		bad_type
	};

	class node;

	class node_iterator
	{
	public:
		using iterator_concept = std::forward_iterator_tag;
	};

	class node 
	{
	public:
		node(std::shared_ptr<detail::toml_internal_data> shared_data = {},
			detail::index_t i = {})
			: _data{ shared_data }, _index{ i } {}

		//test if this is a valid node: calling any function other than good()
		//								on an invalid node is undefined behaviour
		// this will return false for nodes returned by the fucntions:
		//	get_child() for a node that has no children
		//	get_next() for a node that has no more siblings
		//  find()/find_child() if the node wasn't found
		//
		// if you use iterator/ranges to access child nodes then you don't have to worry about this.
		bool good() const noexcept;

		// test node type
		bool table() const noexcept;
		bool array() const noexcept;
		bool key() const noexcept;
		bool value() const noexcept;

		// get the nodes children
		// children are any nodes deeper in the heirarchy than the current node
		// eg. for a table: subtables, keys, arrays(of subtables)
		//		for an array: arrays, values, tables
		//		for a value: none
		//		for a key: anonymous table, value, array(of arrays of values)
		bool has_children() const noexcept;
		std::size_t child_count() const noexcept;
		node get_child() const noexcept;
		std::vector<node> get_children() const;
		node get_next_sibling() const noexcept;

		// search all children for the named node
		// can use dots to search for subchildren
		node find_child(std::string_view) const noexcept;


		// iterator based interface
		node_iterator begin() const noexcept;
		node_iterator end() const noexcept;
		std::size_t size() const noexcept;

		std::string_view as_string() const noexcept;
		// as_XXX

		//returns the root table at the base of the hierarchy
		node get_root_table() const noexcept;
		// works like find_child, but starts from the root
		node find(std::string_view name) const noexcept;
		
		operator bool() const noexcept
		{
			return good();
		}

	private:
		mutable std::shared_ptr<detail::toml_internal_data> _data;
		detail::index_t _index;
	};

	constexpr auto no_throw = detail::no_throw_t{};

	node parse(std::string_view toml);
	node parse(std::istream&); 
	node parse(const std::filesystem::path& filename);

	node parse(std::string_view toml, detail::no_throw_t) noexcept;
	node parse(std::istream&, detail::no_throw_t) noexcept;
	node parse(const std::filesystem::path& filename, detail::no_throw_t) noexcept;
	
}

namespace std
{
	template<>
	struct iterator_traits<another_toml::node_iterator>
	{
		using difference_type = std::ptrdiff_t;
		using value_type = another_toml::node;
		using pointer = value_type*;
		using reference = const value_type&;
		using iterator_category = std::forward_iterator_tag;
	};
}

#include "another_toml.inl"

#endif // !ANOTHER_TOML_HPP
