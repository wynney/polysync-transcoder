#pragma once

#include <stdexcept>
#include <boost/exception/exception.hpp>
#include <boost/exception/all.hpp>

namespace polysync {

// Define a custom exception, and use boost::exception to incrementally add
// context as the stack unwinds during an exception.
struct error : virtual std::runtime_error, virtual boost::exception {
    error(const std::string& msg) : std::runtime_error(msg) {}
};

std::ostream& operator<<( std::ostream&, const error& );

// Mettle uses to_printable()
std::string to_printable( const error& );

// Define a sett of return values for shell programs that check.
enum status : int {
    ok = 0, // No error
    bad_argument = -1, // Malformed command line input
    bad_input = -2, // Error configuring the input dataset
    no_plugin = -3, // Error configuring the output plugin
    description_error = -4, // Problem with a TOML file
    bad_environment = -5, // Error configuring runtime environment
};

namespace exception {

using type = boost::error_info< struct type_type, std::string >;
using detector = boost::error_info< struct detector_type, std::string >;
using field = boost::error_info< struct field_type, std::string >;
using plugin = boost::error_info< struct plugin_type, std::string >;
using status = boost::error_info< struct status_type, polysync::status >;
using path = boost::error_info< struct path_type, std::string >;
using module = boost::error_info< struct module_type, std::string >;

} // namespace exception

inline error operator<<( error e, status s ) {
    e << exception::status(s);
    return e;
}


} // namespace polysync
