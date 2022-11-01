# another-toml-cpp
Another TOML is a cpp parser and writer for TOML v1.0

## Usage
The examples in this section are used to read this example toml file
	title = "TOML Example"

	[owner]
	name = "Tom Preston-Werner"
	dob = 1979-05-27T07:32:00-08:00

	[database]
	enabled = true
	ports = [ 8000, 8001, 8002 ]
	data = [ ["delta", "phi"], [3.14] ]
	temp_targets = { cpu = 79.5, case = 72.0 }

	[servers]
	[servers.alpha]
	ip = "10.0.0.1"
	role = "frontend"
	[servers.beta]
	ip = "10.0.0.2"
	role = "backend"

	[[products]]
	name = "Hammer"
	sku = 738594937

	[[products]]  # empty table within the array

	[[products]]
	name = "Nail"
	sku = 284758393
	color = "gray"
### Parsing Example
The following code can be used to reads the above toml file and extracts each of the values stored in it.
The library is in the namespace another_toml. These examples operate as though the following namespace declaration
was present near the top of the file

	#include "another_toml.hpp"
	namespace toml = another_toml;
	
Parse a toml file by passing the toml content as a string, c string, or string_view.

	auto toml_str = std::string{ "...toml content..." };
	auto toml_c_str = "...toml content...";
	auto toml_sv = std::string_view{ toml_str };
	auto root_table = toml::parse(toml_str);
	auto root_table = toml::parse(toml_c_str);
	auto root_table = toml::parse(toml_sv);
	
Otherwise you can pass a stream that contains the file or a path to the file on the filesystem.

	auto path = std::filesystem::path{ "./path/to/content.toml" };
	auto root_table = toml::parse(std::cin);
	auto root_table = toml::parse(path);
	
Parser functions will throw `another_toml::parser_error`
You can also catch the more specific exception sub-types defined in another_toml.hpp

You can pass `another_toml::no_throw` to request the parser to not throw exceptions.
Parser may still throw standard library exceptions related to memory alloc or 
stream/filesystem access(for the overloads that use these).
Use `good()` to test if the returned node can be read from.

	auto root_table = toml::parse(toml_str, toml::no_throw);
	auto success = root_table.good();	
	
Call `get_first_child()` to access a nodes child node.

	auto first_child = root_table.get_first_child();
	
You can then call `get_next_sibling()` to access each of the other child nodes in a chain.

	auto second_child = first_child.get_next_sibling();
	auto third_child = second_child.get_next_sibling();

Use `has_children` and `has_sibling()` to test if a node has any children, or if a child node has any sibling nodes

	if(root_table.has_chilren())
	{
		auto first_child = root_table.get_first_child();
		if(first_child.has_sibling())
		{
			auto second_child = first_child.get_next_sibling();
		}
	}

You can also check if a call to `get_first_child()` or `get_next_sibling()` was successful by calling `good()`
on the returned node.

	auto first_child = root_node.get_first_child();
	auto success = first_child.good();

Examine the type of the node using the following functions: `table()`, `key()`, `array()`, `array_table()`, `value()`, `inline_table()`
They correspond to the TOML element types of the same name.

	auto success = first_child.key();
	success = second_child.table();
	
If you don't check that a node is good, or that it is of the expected type then exceptions may be thrown by later functions

Extract a specific node from the root table node using `find_child(string_view)`

	auto title_key_node = root_table.find_child("title");
		// extract a value
		auto title_node = root_table.get_value<std::string>("title");

		// extract a table node
		auto owner = root_table.find_table("owner");

		// extract keys from a table using the low level api
		auto owner_key = owner.find_child("name");
		// the value is srored by a key as its only child node
		auto owner_value = owner_key.get_first_child();
		// convert the value to string
		auto owner_name = owner_value.as_string();
		// get_value is a short-hand for the above, its finds the key
		// gets its first child, and converts it to the passed type
		auto owner_dob = owner.get_value<toml::date_time>("dob");

		auto database = root_table.find_table("database");
		// pass a default value to be used if "enabled" isn't found
		auto database_enabled = database.get_value<bool>("enabled", false);
		// extract a heterogeneous array directly into a container
		auto database_ports = database.get_value<std::vector<std::int64_t>>("ports");
	
		//we could also iterate over the ports array directly
		auto port_key = database.find_child("ports");
		auto port_array = port_key.get_first_child();
		std::cout << "ports = [ ";
		for (auto elm : port_array)
		{
			// we can test each element to see if it is a value, sub array or inline table
			if (elm.value() && elm.type() == toml::value_type::integer)
			{
				// all toml value types can be converted to string
				std::cout << elm.as_string() << ", ";
			}
		}
		std::cout << "]\n\n";

		// extract a non-heterogeneous array
		auto data_key = database.find_child("data");
		auto data_array = data_key.get_first_child();
		// extract all the array elements as a vector
		auto elements = data_array.get_children();
		auto data_strings = elements[0].as_t<std::vector<std::string>>();
		auto data_floats = elements[1].as_t<std::vector<double>>();

		// extract an inline table
		auto temp_targets = database.find_table("temp_targets");
		auto temp_cpu = temp_targets.get_value<double>("cpu");
		auto temp_case = temp_targets.get_value<double>("case");

		auto servers = root_table.find_table("servers");

		// extract a sub-table, this looks the same as extracting an inline table
		auto alpha = servers.find_table("alpha");
		auto alpha_ip = alpha.get_value<std::string>("ip");
		auto alpha_role = alpha.get_value<std::string>("role");

		auto beta = servers.find_table("beta");
		auto beta_ip = alpha.get_value<std::string>("ip");
		auto beta_role = alpha.get_value<std::string>("role");

		// arrays of tables look just like an array
		auto products_array = root_table.find_child("products");
		std::cout << "[[products]]\n";
		for (auto table_elm : products_array)
		{
			std::cout << "[ ";
			for (auto key : table_elm)
			{
				if (key.key())
				{
					std::cout << key.as_string() << " = ";
					auto val = key.get_first_child();
					std::cout << val.as_string() << "; ";
				}
			}
			std::cout << "]\n";
		}
	}
