# another-toml-cpp
 
## Parsing Example

	# This is a TOML document
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

The following code can be used to read the above toml file:
	#include "another_toml.hpp"

	namespace toml = another_toml;
	void parse_toml_example(std::string_view toml
		std::filesystem::path file, std::istream& stream)
	{
		auto root_table = toml::root_node{};

		// toml::parse reads a toml file from strings, streams and files
		// toml::parse will read until it reaches the end of the string or
		// a end-of-file.
		try
		{
			// pass a utf-8 encoded std::string, const char*, std::string_view
			root_table = toml::parse(toml);
			// pass a file path to open and read from a file
			root_table = toml::parse(file);
			// pass a custom stream to read from
			root_table = toml::parse(stream);
		}
		catch (toml::parser_error& p)
		{
			// these parsers throw parser_error and its subtypes
		}

		// parse file without throwing exceptions by passing no_throw
		root_table = toml::parse(toml, toml::no_throw);
		// test if the toml source was parsed correclty with .good()
		assert(root_table.good());

		// Even with no_throw, parse can still throw a
		// number of standard library exceptions.
		// eg:
		//	filesystem errors
		//	bad_alloc
		//	std::ios_base::failure if you set istream::exceptions

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
