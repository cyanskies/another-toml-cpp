// MIT License
//
// Copyright (c) 2022 Steven Pilkington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANOTHER_TOML_NODE_HPP
#define ANOTHER_TOML_NODE_HPP

#include <cassert>
#include <memory>
#include <string_view>
#include <vector>

#include "another_toml/internal.hpp"
#include "another_toml/types.hpp"

namespace another_toml
{
	// FWD def
	class node_iterator;

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
		// Throws: node_not_found if has_sibling() == false
		basic_node<> get_next_sibling() const;

		// get child with the provided name
		// test the return value using .good()
		// NOTE: Only searches immediate children.
		//		 Doesn't support dotted keynames.
		// Throws : bad_node and node_not_found
		basic_node<> find_child(std::string_view) const;

		// As above, but returns a bad node on error instead of throwing
		basic_node<> find_child(std::string_view, no_throw_t) const noexcept;

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
		// `T` can be a container if this node is a homogeneous array
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

#include "another_toml/node.inl"

#endif
