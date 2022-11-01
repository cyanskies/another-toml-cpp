# Another TOML
Another TOML is a cpp parser and writer for TOML v1.0.
Another TOML passes the tests at `BurntSushi/toml-test`.

Another TOML can be used to parse toml files without exceptions, as long as you call
the functions to confirm that nodes are valid and the expected kind of toml element or value type.

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
The following code can be used to read the above toml file and extract each of the values stored in it.
The library is in the namespace `another_toml`; however we will add the following namespace declaration
so that we can access everything using `toml::`

	#include "another_toml.hpp"
	namespace toml = another_toml;
	
Parse a toml file by passing the toml content as a string, cstring, or string_view.

	auto toml_str = std::string{ "...toml content..." };
	auto toml_c_str = "...toml content...";
	auto toml_sv = std::string_view{ toml_str };
	auto root_table = toml::parse(toml_str);
	auto root_table = toml::parse(toml_c_str);
	auto root_table = toml::parse(toml_sv);
	
Otherwise you can pass a stream that contains the toml document or a path to the file on the filesystem.

	auto path = std::filesystem::path{ "./path/to/content.toml" };
	auto root_table = toml::parse(std::cin);
	auto root_table = toml::parse(path);
	
Parser functions will throw `another_toml::parser_error`
You can also catch the more specific exception sub-types defined in another_toml.hpp

You can pass `another_toml::no_throw` to request the parser not to throw exceptions.
The parser may still throw standard library exceptions related to memory alloc or 
stream/filesystem access(for the overloads that use these).

Use `good()` to test if the returned node can be read from.

	auto root_table = toml::parse(toml_str, toml::no_throw);
	auto success = root_table.good();
	
The node returned by `another_toml::parse` is the **root node**. It stores all the parsed data and
must remain in memory until you are finished reading the document.
All the other nodes created while reading the document are lightweight references into the **root node**.
If the root node is destroyed then calling any function on any of the other nodes will lead to undefined behaviour.
	
#### TOML Structure
When a toml source is parsed, it is converted into a node tree structure and a root table node is returned. 
- Table nodes can contain child tables, keys, and array tables.
- Inline table nodes contain the same kind of nodes as tables.
- Key nodes usually contain a single value node, but may also contain an inline table node, or an array node.
- Array nodes contain value nodes, but can also contain arrays or inline tables.
- Array table nodes contain table nodes(these tables don't have names, as their name in a toml file is the name of the array table)

#### Access Child Nodes
Call `get_first_child()` to access a nodes child node.

	auto first_child = root_table.get_first_child();
	
You can then call `get_next_sibling()` to access each of the other child nodes in a chain.

	auto second_child = first_child.get_next_sibling();
	auto third_child = second_child.get_next_sibling();

Use `has_children` and `has_sibling()` to test if a node has any children, or if a child node has any remaining sibling nodes

	if (root_table.has_chilren())
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

#### Examine Node Type
Examine the type of the node using the following functions: `table()`, `key()`, `array()`, `array_table()`, `value()`, `inline_table()`
They correspond to the TOML element types of the same name.

	auto success = first_child.key();
	success = second_child.table();
	
If you don't check that a node is `good()`, or that it is of the expected type then exceptions may be thrown by later functions.

#### Extract Values
In order to find a specific node you will also need to examine its name. All node types can be converted into strings using `as_string`.

	if(first_child.key())
		auto key_name = first_child.as_string();
	if(second_child.table())
		auto table_name = second_child.as_string();
		
Some nodes don't have names, such as arrays or inline tables that are nested within arrays.

Once you have a value node you can convert it into a usable c++ type.
TOML types correspond to the following c++ types:
- String: `std::string`
- Integer: `std::int64_t`, int64 is used as it can hold the entire range required by the TOML standard
- Float: `double`, double is used to preserve the precision level required by the TOML standard
- Boolean: `bool`

Another TOML provides types for storing TOML dates and times:
- Offset Date Time: `another_toml::date_time`
- Local Date Time: `another_toml::local_date_time`
- Local Date: `another_toml::date`
- Local Time: `another_toml::time`

A value node can be converted to one of the types above using the following functions:

	std::string as_string()
	std::int64_t as_integer()
	double as_floating()
	bool as_boolean()
	toml::date_time as_date_time()
	toml::local_date_time as_date_time_local()
	toml::date as_date_local()
	toml::time as_time_local()
	
Other than `as_string()`, the above functions will throw `another_toml::wrong_type` if the node
can't be converted to that type.

To extract `title` from the example toml source we can use the following code
to get the value node.

	auto title_key = root_node.get_first_child();
	auto title_value = title_key.get_first_child();
	
We can confirm its type using `type()`.

	auto correct_type = (title_value.type() == toml::value_type::string);

And we can extract the message using `as_string()`.

	auto title_str = title_value.as_string();
	
#### Extracting a Table and Value
Now we'll extract `[owner]` and its key `dob`.

	auto owner_table = title_key.get_next_sibling();
	auto is_table = owner_table.table();
	
	auto owner_name = owner_table.get_first_child();
	auto owner_dob = owner_name.get_next_sibling();
	auto dob_value = owner_dob.get_first_child();
	
Since we expect dob to be a date and time value we will use `as_date_time()` to extract it.
We can also convert it(and any other type) into a string using `as_string()`.

	auto dob = dob_value.as_date_time();
	auto dob_str = dob_value.as_string();
	
#### Finding Child Nodes
Access nodes as above if useful for reading TOML documents with unknown data in them;
but most of the time we are expecting a specific structure for our document.
We can use `find_child(std::string_view)` to find a specific node by name.

	auto database = root_node.find_child("database");
	auto success = database.good();
	
#### Templated Extraction Functions
You can also convert nodes to values using the templated helper function `as_t<Type>()`.

	auto enabled_key = database.find_child("enabled");
	auto enabled_value = enabled_key.get_first_child();
	auto is_enabled = enabled_value.as_t<bool>();
	
There is also a templated `get_value<Type>(std::string_view)` function to
cut out the boilerplate code needed to extract keys and their values.

	auto is_enabled = database.get_value<bool>("enabled);
	
`get_value` will throw exceptions if the key is missing or if the value
cannot be converted into the desired type.

We can iterate over a nodes children to extract arrays.

	auto ports_key = database.find_child("ports");
	auto ports_array = port_key.get_first_child();
	auto ports = std::vector<std::int64_t>{};
	for (auto elm : port_array)
		ports.push_back(elm.as_integer());

`get_value` can also be used to extract heterogeneous arrays directly into a container.

	auto ports = database.get_value<std::vector<std::int64_t>>("ports");

We can also extract all the child nodes as a `std::vector` using `get_children()`.

	auto data_key = database.find_child("data");
	auto data_array = data_key.get_first_child();
	auto elements = data_array.get_children();
	auto data_strings = elements[0].as_t<std::vector<std::string>>();
	auto data_floats = elements[1].as_t<std::vector<double>>();

#### Extracting Tables
We can also extract inline tables using the helper function `find_table(std::string_view)`.

		auto temp_targets = database.find_table("temp_targets");
		auto temp_cpu = temp_targets.get_value<double>("cpu");
		auto temp_case = temp_targets.get_value<double>("case");

`find_table` can throw `another_toml::key_not_found` or `another_toml::wrong_type`.

`find_table` works with normal tables too!

	auto servers = root_table.find_table("servers");

	auto alpha = servers.find_table("alpha");
	auto alpha_ip = alpha.get_value<std::string>("ip");
	auto alpha_role = alpha.get_value<std::string>("role");

	auto beta = servers.find_table("beta");
	auto beta_ip = alpha.get_value<std::string>("ip");
	auto beta_role = alpha.get_value<std::string>("role");

#### Arrays of Tables
Arrays of tables work just like a normal array, except the child nodes are all tables.
The tables in the array don't have names, when you iterate through them `as_string()` will
return an empty string.

		auto products_array = root_table.find_child("products");
		struct product
		{
			std::string name;
			std::int64_t sku;
			std::string color;
		}
		
		auto product_vect = std::vector<product>{};
		
		for (auto table_elm : products_array)
		{
			auto new_poduct = product{};
			auto name = table_elm.find_child("name");
			if(name.good())
				new_product.name = name.get_first_child().as_string();
			auto sku = table_elm.find_child("sku");
			if(sku.good())
				new_product.sky = sku.get_first_child().as_integer();
			auto color = table_elm.find_child("color");
			if(color.good())
				new_product.color = sku.get_first_child().as_string();
			product_vect.emplace_back(new_product);			
		}

## Installing
Include Another TOML as a git submodule and configure it using cmake.
