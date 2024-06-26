# Another TOML
Another TOML is a cpp17 parser and writer for TOML v1.0.

Another TOML can be used to parse toml files without exceptions, as long as you call
the functions to confirm that nodes are valid and the expected kind of toml element or value type.

## Unicode
Another TOML performs unicode NFC normalisation when searching for tables and keys.
Functions for performing normalised comparisons use algorithms from [uni-algo](https://github.com/uni-algo/uni-algo) and are provided for end users as well.

```cpp
// both "my_table" and the names of the child nodes will be normalised for comparison
auto table = root_node.find_child("my_table");

#include "another_toml/string_util.hpp"

// compare utf8 strings for normalised equality
return another_toml::unicode_string_equal("string one", "string two");
```

## Installing
* Clone this repository.
* Include it in your project as a submodule.
* Invoke: `git submodule update` to update Another TOMLs dependencies.
* Configure it using cmake.
* List `another-toml-cpp` as a dependency in `target_link_libraries`

## Tests
Another TOML passes the tests at `BurntSushi/toml-test` (as of v1.5.0).
Repo at: https://github.com/cyanskies/another-toml-test is used for testing.

## Usage
The examples in this section are used to read this example toml file

```toml
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
```

Another TOML is designed to read ASCII and UTF-8 encoded files, it will ignore the UTF-8 BOM if encountered
but will not protect against being given files in an unexpected encoding (at least not until they are interpreted as invalid TOML).

### Parsing Example
The following code can be used to read the above toml file and extract each of the values stored in it.
The library is in the namespace `another_toml`; however we will add the following namespace declaration
so that we can access everything using `toml::`

```cpp
#include "another_toml.hpp"
namespace toml = another_toml;
```

Parse a toml file by passing the toml content as a string, cstring, or string_view.

```cpp
auto toml_str = std::string{ "...toml content..." };
auto toml_c_str = "...toml content...";
auto toml_sv = std::string_view{ toml_str };
auto root_table = toml::parse(toml_str);
auto root_table = toml::parse(toml_c_str);
auto root_table = toml::parse(toml_sv);
```
	
Otherwise you can pass a stream that contains the toml document or a path to the file on the filesystem.

```cpp
auto path = std::filesystem::path{ "./path/to/content.toml" };
auto root_table = toml::parse(std::cin);
auto root_table = toml::parse(path);
```

Parser functions will throw `another_toml::toml_error`
You can also catch the more specific exception sub-types defined in another_toml/except.hpp

You can pass `another_toml::no_throw` to request the parser not to throw exceptions.
The parser may still throw standard library exceptions related to memory alloc or 
stream/filesystem access(for the overloads that use these).

Use `good()` to test if the returned node can be read from. If a node returns `false` after calling `good()`
then it is known as a bad node.

```cpp
auto root_table = toml::parse(toml_str, toml::no_throw);
auto success = root_table.good();
```

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

```cpp
auto first_child = root_table.get_first_child();
```

You can then call `get_next_sibling()` to access each of the other child nodes in a chain.

```cpp
auto second_child = first_child.get_next_sibling();
auto third_child = second_child.get_next_sibling();
```

Use `has_children` and `has_sibling()` to test if a node has any children, or if a child node has any remaining sibling nodes

```cpp
if (root_table.has_chilren())
{
	auto first_child = root_table.get_first_child();
	if(first_child.has_sibling())
	{
		auto second_child = first_child.get_next_sibling();
	}
}
```

You can also check if a call to `get_first_child()` or `get_next_sibling()` was successful by calling `good()`
on the returned node.

```cpp
auto first_child = root_node.get_first_child();
auto success = first_child.good();
```

#### Examine Node Type
Examine the type of the node using the following functions: `table()`, `key()`, `array()`, `array_table()`, `value()`, `inline_table()`
They correspond to the TOML element types of the same name.

```cpp
auto success = first_child.key();
success = second_child.table();
```

If you don't check that a node is `good()`, or that it is of the expected type then exceptions may be thrown by later functions.

#### Extract Values
In order to find a specific node you will also need to examine its name. All node types can be converted into strings using `as_string`.

```cpp
if(first_child.key())
	auto key_name = first_child.as_string();
if(second_child.table())
	auto table_name = second_child.as_string();
```

Some nodes don't have names, such as arrays or inline tables that are nested within arrays, these nodes
will return an empty string. 

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

```cpp
std::string as_string()
std::int64_t as_integer()
double as_floating()
bool as_boolean()
toml::date_time as_date_time()
toml::local_date_time as_date_time_local()
toml::date as_date_local()
toml::time as_time_local()
```

Other than `as_string()`, the above functions will throw `another_toml::wrong_type` if the node
can't be converted to that type.

To extract `title` from the example toml source we can use the following code
to get the value node.

```cpp
auto title_key = root_node.get_first_child();
auto title_value = title_key.get_first_child();
```

We can confirm its type using `type()`.

```cpp
auto correct_type = (title_value.type() == toml::value_type::string);
```

And we can extract the message using `as_string()`.

```cpp
auto title_str = title_value.as_string();
```

#### Extracting a Table and Value
Now we'll extract `[owner]` and its key `dob`.

```cpp
auto owner_table = title_key.get_next_sibling();
auto is_table = owner_table.table();
	
auto owner_name = owner_table.get_first_child();
auto owner_dob = owner_name.get_next_sibling();
auto dob_value = owner_dob.get_first_child();
```

Since we expect dob to be a date and time value we will use `as_date_time()` to extract it.
We can also convert it(and any other type) into a string using `as_string()`.

```cpp
auto dob = dob_value.as_date_time();
auto dob_str = dob_value.as_string();
```

#### Finding Child Nodes
Accessing nodes as above if useful for reading TOML documents with unknown data in them;
but most of the time we are expecting a specific structure for our document.
We can use `find_child(std::string_view)` to find a specific node by name.

```cpp
auto database = root_node.find_child("database");
```

`find_child` will skip key nodes, the code below will return the inline table node,
rather than the key node named `temp_targets`. This makes it easier to write code that
accesses inline tables, values and arrays.

```cpp
auto inline_table = database.find_child("temp_targets");
```

`operator[]` is overriden for nodes so that the following code can be written:

```cpp
auto case_temp_value = root_node["database"]["temp_targets"]["case"];
```

`find_child(std::string_view)` throws `node_not_found` if the child node cannot be found,
this enables calls to `find_child` to report errors while they are chained together as above.

You can pass `toml::no_throw` to instead return a bad node on failure.

```cpp
auto database = root_node.find_child("database", toml::no_throw);
auto success = database.good();
```
	
#### Templated Extraction Functions
You can also convert nodes to values using the templated helper function `as_type<Type>()`.

```cpp
auto enabled_value = database.find_child("enabled");
auto is_enabled = enabled_value.as_type<bool>();
```

There is also a templated `get_value<Type>(std::string_view)` function to
cut out the boilerplate code needed to extract keys and their values.

```cpp
auto is_enabled = database.get_value<bool>("enabled");
```

`get_value` will throw exceptions if the key is missing or if the value
cannot be converted into the desired type.

We can iterate over a nodes children to extract arrays.

```cpp
auto ports_array = database.find_child("ports");
auto ports = std::vector<std::int64_t>{};
for (auto elm : port_array)
	ports.push_back(elm.as_integer());
```

`get_value` can also be used to extract homogeneous arrays (an array with all elements of the same type) directly into a container.

```cpp
auto ports = database.get_value<std::vector<std::int64_t>>("ports");
```

We can also extract all the child nodes as a `std::vector` using `get_children()`.

```cpp
auto data_array = database.find_child("data");
auto elements = data_array.get_children();
auto data_strings = elements[0].as_type<std::vector<std::string>>();
auto data_floats = elements[1].as_type<std::vector<double>>();
```

#### Extracting Tables
We can also extract inline tables using `find_child`.

```cpp
auto temp_targets = database.find_child("temp_targets");
auto temp_cpu = temp_targets.get_value<double>("cpu");
auto temp_case = temp_targets.get_value<double>("case");
```

Code for reading data from a table will typically look like this:

```cpp
auto alpha = root_table["servers"]["alpha"];
auto alpha_ip = alpha.get_value<std::string>("ip");
auto alpha_role = alpha.get_value<std::string>("role");

auto beta = root_table["servers"]["beta"];
auto beta_ip = alpha.get_value<std::string>("ip");
auto beta_role = alpha.get_value<std::string>("role");
```

#### Arrays of Tables
Arrays of tables work just like a normal array, except the child nodes are all tables.
The tables in the array don't have names, when you iterate through them `as_string()` will
return an empty string.

```cpp
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
	new_product.name = name.as_string();
	auto sku = table_elm.find_child("sku");
	new_product.sky = sku.as_integer();
	auto color = table_elm.find_child("color");
	new_product.color = color.as_string();
	product_vect.emplace_back(new_product);			
}
```

### Generating a TOML Document
Another TOML can also output TOML documents, we'll generate the example document near the top
of this file. We use `another_toml::writer` to describe our document and then write it out.

```cpp	
auto w = toml::writer{};
```

#### Writing Keys and Values
The writer automatically creates the root level table for you, so we can add keys to it
straight away with `write_key(std::string_view)`.

```cpp
w.write_key("title");
```

After `write_key` we need to submit a value to the writer with `write_value`. 
Writer is expecting a value to follow the key, calling a different function
at this point is an error.
This function accepts any of the TOML types listed earlier in this article.

```cpp
w.write_value("TOML Example");
```

#### Writing Tables
We can add a table with `begin_table(std::string_view)` everything added after
`begin_table()` will be nested within the table. The table must be eventually ended
with `end_table()`.

```cpp
w.begin_table("owner");
```

A shorthand is provided for adding a key and value together. Call `write` with
both a key and a value.

```cpp
w.write("name", "Tom Preston-Werner");
w.write("dob", toml::date_time{
	toml::local_date_time{
		toml::date{	1979, 5, 27	},
		toml::time{ 7, 32 }
	}, false, 8 });
```

Now we can end the 'owner' table.

```cpp
w.end_table();
```

Next we'll write the 'database' table.

```cpp
w.begin_table("database");
w.write("enabled", true);
```

#### Writing Dotted Keys
In TOML a key can have a 'dotted' name:

```toml
name = "Orange"
physical.color = "orange"
physical.shape = "round"
site."google.com" = true
```

These structures are expressed in Another TOML by creating dotted tables.
The above document is created using the following code:

```cpp
w.write("name", "Orange");

w.begin_table("physical", toml::table_def_type::dotted);
	w.write("color", "orange");
	w.write("shape", "round");
w.end_table();

w.begin_table("site", toml::table_def_type::dotted);
	w.write("google.com", true);
w.end_table();
```

#### Writing Arrays
We can write arrays by starting and ending the array with
`begin_array(std::string_view)` and `end_array()`.
Inbetween the begining and end of the array we can add values and
also nest additional arrays and inline tables.

```cpp
w.begin_array("ports");
w.write_value(8000);
w.write_value(8001);
w.write_value(8002);
w.end_array();
```

You can also write homogeneous arrays by passing a container
to the `write` function.

```cpp
const auto ports = std::vector{
	8000, 8001, 8002
};
w.write("ports", ports);
```

When nesting arrays and inline tables within arrays you still use
the same interface as before, however when nested within an array
these elements don't have a name, you can leave the name parameter blank
by passing `{}`.

We can use the shorthand to nest this array of strings.

```cpp
w.begin_array("data");
w.write({}, { "delta", "phi" });
```

Or use the 'begin'/'end array' functions.

```cpp
w.begin_array({});
w.write_value(3.14f);
w.end_array();
w.end_array(); // data
```

#### Writing Inline Tables
Inline tables work the same way as normal tables, except they are started
and finished with `begin_inline_table(std::string_view)` and `end_inline_table()`.

```cpp
w.begin_inline_table("temp_targets");
w.write("cpu",79.5);
w.write("case", 72.f);
w.end_inline_table();
```

We'll end the 'database' table, and start a 'servers' table.

```cpp
w.end_table(); // [database]

w.begin_table("servers");
```

We can leave this table empty and add two nested tables using what
we've already learned and also end the 'servers' table.

```cpp
w.begin_table("alpha");
w.write("ip", "10.0.0.1");
w.write("role", "frontend");
w.end_table();

w.begin_table("beta");
w.write("ip", "10.0.0.2");
w.write("role", "backend");
w.end_table();

w.end_table(); // [servers]
```

#### Writing Arrays of Tables
Arrays of tables use a similar syntax to tables, however we begin and end the tables
with `begin_array_tables(std::string_view)` and `end_array_tables()`.

```cpp
w.begin_array_tables("products");
w.write("name", "Hammer");
w.write("sku", 738594937);
w.end_array_table();
```

Any of the TOML elements that use begin/end functions can be left empty
by calling their end function without adding anything between them.

```cpp
w.begin_array_tables("products");
w.end_array_table();
```

Everything added to a table within the table array is nested
solely within that table.

```cpp
w.begin_array_tables("products");
w.write("name", "Nail");
w.write("sku", 284758393);
w.write("color", "grey");
w.end_array_table();
```

#### Outputting the Completed Document
You must end any open tables/arrays or inline arrays before generating the
TOML document (The root table is an exception). You can use `to_string()` to generate a `std::string` containing
the TOML document or you can stream it to any standard output stream.

```cpp
auto toml_str = w.to_string(); // make a string variable
std::cout << w; // stream to standard output
```

### Writer Output Options
The output formating can be controlled as explained below.

#### String Output Options
When writing string values you can specify that they should be written as literal strings
by adding `literal_string_tag` to the function call.

```cpp
w.write_value("literal\n \nstring", toml::writer::literal_string_tag);
```

#### Integral Output Options
You can control the base representation of an integral type with the
`int_base` parameter.

```cpp
w.write_value(50, toml::writer::int_base::bin);
```

The supported bases are:
* **bin**: binary
* **oct**: octal
* **dec**: decimal
* **hex**: hexadecimal

#### Floating Point Output Options
You can control the base representation of a floating point type with the
`float_rep` and `precision` parameter.

```cpp
w.write_value(50, toml::writer::float_rep::scientific, 6);
```

The supported representations are:
* **default**: notation will be chosen based on the floats value
* **fixed**: fractional notation (eg. 3.14)
* **scientific**: scientific notation (eg. 10e-4)

Precision controls how many decimal places of precision will be written. Pass
`writer::auto_precision` to have it chosen dynamically.

#### Key/Value Options
The above output settings can also be used with the `write` shorthand, this even works
when writing out arrays.

```cpp
w.write("color", "grey", toml::writer::literal_string_tag);
w.write("floats", { 1.2f, 1.2f, 1.2f }, toml::writer::float_rep::scientific);
```

#### Global Writer Options
More settings for controlling writer output can be controlled by passing a `writer_options`
struct to your writer. `writer_options` is default contructed with the settings that
writer uses by default. A description of each setting is below:

##### Max Line Length
Set `writer_options::max_line_length`.
Set to 80 characters by default.

How many characters an output line should have before splitting. The lines are only
split at valid split points, such as after the first '[' of an array or after each
array elements ','. Lines can also be split for long strings, using '\' to preserve
the strings value.

Set to `writer_options::dont_split_lines` to not split lines.
NOTE: Length calculations are simple approximations, not a guarrantee to split the 
line at a specific column. Notably splits will only be added after complete values (eg. a whole datetime) 
or inbetween words in a non-literal string type.

```toml
line_length_short = [
1, 2, 3, 4, 5, 6,
7, 8, 9, 10 ]
```

##### Compact Spacing
Set `writer_options::compact_spacing`.
Enabled by default.

If enabled the writer will skip any optional whitespace, can be used
to reduce document size at the cost of readability(doesn't effect array line splitting).

```toml
compact_spacing_off = [ 1, 2, 3 ]
compact_spacing_on=[1,2,3]
```

##### Indent String
Set `writer_options::indent_string`.
Set to a single tab by default.

This string is used to indent lines, you can replace it to control how much
indentation is used. Most users will leave it as a tab character or replace it
with the desired number of spaces. The indentation string must be made up of
whitespace characters, otherwise the resulting toml files will be invalid.
Indentation is used by **Indent Child Tables** and **Indent After Line Split**.

```cpp
auto opts = toml::writer_options{};
opts.indent_string = " ";
opts.indent_child_tables = true; // See 'Indent Child Tables' below 
auto w = toml::writer{};
w.set_options(opts);
```
```toml
[a]
key = "value"

 [a.b]
 key = "value"

  [a.b.c]
```

##### Indent Child Tables
Set `writer_options::indent_child_tables`.
Disabled by default.

If enabled then an indentation will be added for each layer of child table.

```toml
[a]
name = "a"

	[a.b]
	key = "value"

		[a.b.c]

#indentation isn't added for skipped tables (see skip empty tables)
[x.y.z]
```

##### Indent After Line Split
Set `writer_options::indent_after_line_split`
Enabled by default.

While this is enabled there will be an indent added when a line split is inserted
during a long value.
```toml
line_length_short = [
	1, 2, 3, 4, 5, 6,
	7, 8, 9, 10 ]
```

##### Ascii Output
Set `writer_options::ascii_output`.
Disabled by default.

Output only ascii characters, all unicode characters will be replaced by
escape sequences.

##### Skip Empty Tables
Set `writer_options::skip_empty_tables`.
Enabled by default.

Don't output empty tables, unless they are leaf tables.

```toml
[a] # leaf table

[x.y.z] # leaf table

[q]
count = 5
	
	[q.w.e] # leaf table
```

##### Date Time Separator
Set `writer_options::date_time_separator`.

Choose which character to use to separate the date and time portions of 
datetime and local datetime types:

* `writer_options::date_time_separator_t::big_t` (default)
* `writer_options::date_time_separator_t::whitespace` 

```toml
# big_t
date = 1979-05-27T07:32:00
# whitespace
date = 1979-05-27 07:32:00
```

##### Simple Numerical Output
Set `writer_options::simple_numerical_output`.
Disabled by default.

If set, overrides any output options set when calling `write` or `write_value`
with integrals and floats, instead writes them out in base 10 and fixed notation.

##### UTF-8 Byte Order Mark
Set `writer_options::utf8_bom`.
Disabled by default.

If set, the output will include a UTF-8 BOM at the beginning. Enable this
only if you expect the output to be read by a program that uses the BOM to detect 
encoding, or refuses to accept UTF-8 encoded files without a BOM.
