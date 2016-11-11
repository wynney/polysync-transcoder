#include <polysync/plog/description.hpp>
#include <polysync/plog/detector.hpp>
#include <polysync/exception.hpp>
#include <polysync/logging.hpp>
#include <polysync/io.hpp>

#include <regex>

// Instantiate the static console format; this is used inside of mettle to
// print failure messages through operator<<'s defined in io.hpp.
namespace polysync { namespace console { codes format = color(); }}

namespace polysync { namespace plog { 

using polysync::logging::logger;
using polysync::logging::severity;

namespace detector {

catalog_type catalog; 

template <typename T>
inline T hex_stoul(const std::string& value) {
    std::regex is_hex("0x.+");
    if (std::regex_match(value, is_hex))
        return static_cast<T>(std::stoul(value, 0, 16));
    else
        return static_cast<T>(std::stoul(value));
}

// Description strings have type information, but the type comes out as a
// string (because TOML does not have a very powerful type system).  Define
// factory functions to look up a type by string name and convert to a strong type.
static variant convert(const std::string value, const std::type_index& type) {
    static const std::map<std::type_index, std::function<variant (const std::string&)>> factory = {
        { typeid(std::uint8_t), [](const std::string& value) { return hex_stoul<std::uint8_t>(value); } },
        { typeid(std::uint16_t), [](const std::string& value) { return hex_stoul<std::uint16_t>(value); } },
        { typeid(std::uint32_t), [](const std::string& value) { return hex_stoul<std::uint32_t>(value); } },
        { typeid(std::uint64_t), [](const std::string& value) { return hex_stoul<std::uint64_t>(value); } },
        { typeid(float), [](const std::string& value) { return static_cast<float>(stof(value)); } },
        { typeid(double), [](const std::string& value) { return static_cast<double>(stod(value)); } },
    };

    if (!factory.count(type))
        throw polysync::error("no string converter") << exception::type(descriptor::typemap.at(type).name);

    return factory.at(type)(value);
}

// Load the global type detector dictionary detector::catalog with an entry from a TOML table.
void load(const std::string& name, std::shared_ptr<cpptoml::table> table, catalog_type& catalog) {
    logger log("detector[" + name + "]");

    // Recurse nested tables
    if (!table->contains("description")) {
        for (const auto& tp: *table) 
            load(name + "." + tp.first, tp.second->as_table(), catalog);
        return;
    }

    if (!table->contains("detector")) {
        BOOST_LOG_SEV(log, severity::debug1) << "no sequel";
        return;
    }

    auto det = table->get("detector");
    if (!det->is_table())
        throw polysync::error("detector must be a table");

    for (const auto& branch: *det->as_table()) {

        if (!branch.second->is_table())
            throw polysync::error("detector must be a TOML table") << exception::type(branch.first);

        if (!plog::descriptor::catalog.count(name))
            throw polysync::error("no type description") << exception::type(name);

        decltype(std::declval<detector::type>().match) match;
        const plog::descriptor::type& desc = plog::descriptor::catalog.at(name);
        for (auto pair: *branch.second->as_table()) {

            // Dig through the type description to get the type of the matching field
            auto it = std::find_if(desc.begin(), desc.end(), 
                    [pair](auto f) { return f.name == pair.first; });
            
            // The field name did not match at all; throw to get out of here.
            if (it == desc.end())
                throw polysync::error("unknown field") 
                    << exception::type(pair.first) 
                    << exception::field(it->name)
                    << status::description_error;

            // Disallow branching on any non-native field type.  Branching on
            // arrays or nested types is not supported (and hopefully never
            // will need to be).
            const std::type_index* idx = it->type.target<std::type_index>();
            if (!idx)
                throw polysync::error("illegal branch on compound type")
                    << exception::type(pair.first)
                    << exception::field(it->name)
                    << status::description_error; 

            // For this purpose, TOML numbers must be strings because TOML is
            // not very type flexible (and does not know about hex notation).
            // Here is where we convert that string into a strong type.
            // const std::type_info& info = it->type.target_type();
            std::string value = pair.second->as<std::string>()->get();
            match.emplace(pair.first, convert(value, *idx));
        }
        detector::catalog.emplace_back(detector::type { name, match, branch.first });

        BOOST_LOG_SEV(log, severity::debug1) <<  "installed sequel \"" 
            << detector::catalog.back().parent << "\" -> \"" 
            << detector::catalog.back().child << "\"";
    }
}

} // namespace detector

std::string detect(const node& parent) {
    logging::logger log { "detector" };

    plog::tree tree = *parent.target<plog::tree>();
    if (tree->empty())
        throw polysync::error("parent tree is empty");

    // std::streampos rem = endpos - stream.tellg();
    // BOOST_LOG_SEV(log, severity::debug2) 
    //     << "decoding " << (size_t)rem << " byte payload from \"" << parent.name << "\"";

    // Iterate each detector in the catalog and check for a match.  Store the
    // resulting type name in tpname.
    std::string tpname;
    for (const detector::type& det: detector::catalog) {

        // Parent is not even the right type, so short circuit and fail this test early.
        if (det.parent != parent.name) {
            BOOST_LOG_SEV(log, severity::debug2) << det.child << " not matched: parent \"" 
                << parent.name << "\" != \"" << det.parent << "\"";
            continue;
        }

        // Iterate each field in the detector looking for mismatches.
        std::vector<std::string> mismatch;
        for (auto field: det.match) {
            auto it = std::find_if(tree->begin(), tree->end(), 
                    [field](const node& n) { 
                    return n.name == field.first; 
                    });
            if (it == tree->end()) {
                BOOST_LOG_SEV(log, severity::debug2) << det.child << " not matched: parent \"" 
                    << det.parent << "\" missing field \"" << field.first << "\"";
                break;
            }
            if (*it != field.second)
                mismatch.emplace_back(field.first);
        }
        
        // Too many matches. Catalog is not orthogonal and needs tweaking.
        if (mismatch.empty() && !tpname.empty())
            throw polysync::error("non-unique detectors: " + tpname + " and " + det.child);

        // Exactly one match. We have detected the sequel type.
        if (mismatch.empty()) {
            tpname = det.child;
            continue;
        }

        //  The detector failed, print a fancy message to help developer fix catalog.
        BOOST_LOG_SEV(log, severity::debug2) << det.child << ": mismatched" 
            << std::accumulate(mismatch.begin(), mismatch.end(), std::string(), 
                    [&](const std::string& str, auto field) { 
                        return str + " { " + field + ": " + 
                        // lex(*std::find_if(tree->begin(), tree->end(), [field](auto f){ return field == f.name; })) + 
                        // lex(tree->at(field)) + 
                        // (tree->at(field) == det.match.at(field) ? " == " : " != ")
                        // lex(det.match.at(field)) 
                        + " }"; 
                    });
    }

    // Absent a detection, return raw bytes.
    if (tpname.empty()) {
        BOOST_LOG_SEV(log, severity::debug1) << "type not detected, returning raw sequence";
        return "raw";
    }

    BOOST_LOG_SEV(log, severity::debug1) << tpname << " matched from parent \"" << parent.name << "\"";

    return tpname;
}


}} // namespace polysync::plog


