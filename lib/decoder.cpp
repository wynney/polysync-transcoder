#include <bitset>

#include <polysync/detector.hpp>
#include <polysync/descriptor/formatter.hpp>
#include <polysync/print_tree.hpp>
#include <polysync/print_hana.hpp>
#include <polysync/exception.hpp>
#include <polysync/hana.hpp>
#include <polysync/decoder/decoder.hpp>

namespace polysync {

using logging::severity;

// The TOML descriptions know the type names as strings, and metadata like
// endianess that also affect how the numbers are decoded.  The decoder,
// however, needs the C++ type, not a string representation of the type.  Here,
// we have a grand old mapping of the string type names (from TOML) to the type
// specific decode function.
std::map<std::string, Decoder::Parser> Decoder::parseMap =
{
   // Native integers
   { "uint8", [](Decoder& r) { return r.decode<std::uint8_t>(); } },
   { "uint16", [](Decoder& r) { return r.decode<std::uint16_t>(); } },
   { "uint32", [](Decoder& r) { return r.decode<std::uint32_t>(); } },
   { "uint64", [](Decoder& r) { return r.decode<std::uint64_t>(); } },
   { "int8", [](Decoder& r) { return r.decode<std::int8_t>(); } },
   { "int16", [](Decoder& r) { return r.decode<std::int16_t>(); } },
   { "int32", [](Decoder& r) { return r.decode<std::int32_t>(); } },
   { "int64", [](Decoder& r) { return r.decode<std::int64_t>(); } },

   // Bigendian integers
   { "uint16.be", [](Decoder& r){ return r.decode<boost::endian::big_uint16_t>(); } },
   { "uint32.be", [](Decoder& r){ return r.decode<boost::endian::big_uint32_t>(); } },
   { "uint64.be", [](Decoder& r){ return r.decode<boost::endian::big_uint64_t>(); } },
   { "int16.be", [](Decoder& r){ return r.decode<boost::endian::big_int16_t>(); } },
   { "int32.be", [](Decoder& r){ return r.decode<boost::endian::big_int32_t>(); } },
   { "int64.be", [](Decoder& r){ return r.decode<boost::endian::big_int64_t>(); } },

   // Floating point types and aliases
   { "float", [](Decoder& r) { return r.decode<float>(); } },
   { "float32", [](Decoder& r) { return r.decode<float>(); } },
   { "double", [](Decoder& r) { return r.decode<double>(); } },
   { "float64", [](Decoder& r) { return r.decode<double>(); } },

   // Bigendian floats: first byteswap as uint, then emplacement new to float.
   { "float.be", [](Decoder& r) {
           std::uint32_t swap = r.decode<boost::endian::big_uint32_t>().value();
           return *(new ((void *)&swap) float);
       } },
   { "float32.be", [](Decoder& r) {
           std::uint32_t swap = r.decode<boost::endian::big_uint32_t>().value();
           return *(new ((void *)&swap) float);
       } },
   { "double.be", [](Decoder& r) {
           std::uint64_t swap = r.decode<boost::endian::big_uint64_t>().value();
           return *(new ((void *)&swap) float);
       } },
   { "float64.be", [](Decoder& r) {
           std::uint64_t swap = r.decode<boost::endian::big_uint64_t>().value();
           return *(new ((void *)&swap) float);
       } },

   // Fallback bytes buffer
   { "raw", [](Decoder& r)
       {
           std::streampos rem = r.record_endpos - r.stream.tellg();
           polysync::Bytes raw(rem);
           r.stream.read((char *)raw.data(), rem);
           return Node("raw", raw);
       }},
};


Decoder::Decoder( std::istream& st ) : stream( st )
{
    // Enable exceptions for all reads
    stream.exceptions( std::ifstream::failbit );
}

// Read a field, described by looking up thef type by string.  The type strings can
// be compound types described in the TOML description, primitive types known
// by parse_map, or strings generated by boost::hana from compile time structs.
Variant Decoder::decode( const std::string& type )
{
    auto parse = parseMap.find( type );

    if ( parse != parseMap.end() )
    {
        return parse->second( *this );
    }

    if ( !descriptor::catalog.count( type ) )
    {
        throw polysync::error( "no decoder" )
           << exception::type( type )
           << exception::module( "decoder" )
           << status::description_error;
    }

    BOOST_LOG_SEV( log, severity::debug2 )
        << "decoding \"" << type << "\" at offset " << stream.tellg();

    const descriptor::Type& desc = descriptor::catalog.at( type );
    return decode( desc );
}

struct branch_builder
{
    polysync::Tree branch;
    const descriptor::Field& fieldDescriptor;
    Decoder* d;

    // Terminal types
    void operator()( const std::type_index& idx ) const
    {
        auto term = descriptor::terminalTypeMap.find(idx);
        if ( term == descriptor::terminalTypeMap.end() )
        {
            throw polysync::error("no typemap") << exception::field(fieldDescriptor.name);
        }

        std::string tname;

        switch ( fieldDescriptor.byteorder )
        {
            case descriptor::ByteOrder::LittleEndian:
                tname = term->second.name;
                break;
            case descriptor::ByteOrder::BigEndian:
                tname = term->second.name + ".be";
                break;
        }

        Variant a = d->decode(tname);
        branch->emplace_back(fieldDescriptor.name, a);
        branch->back().format = fieldDescriptor.format;
        BOOST_LOG_SEV(d->log, severity::debug2) << fieldDescriptor.name << " = "
            << branch->back() << " (" << term->second.name
            << (fieldDescriptor.byteorder == descriptor::ByteOrder::BigEndian ? ", bigendian" : "")
            << ")";
    }

    void operator()(const descriptor::BitField& idx) const
    {
        size_t size = idx.size();
        BOOST_LOG_SEV( d->log, severity::debug1 )
            << "buffering " << size << " bits for bitfield partition";
        if ( size % 8 )
        {
            throw polysync::error( "bitfield must have total size an integral number of bytes" );
        }

        namespace mp = boost::multiprecision;
        mp::cpp_int blob;
        Bytes buf( size/8 );
        d->stream.read( (char*)buf.data(), buf.size() );
        mp::import_bits( blob, buf.begin(), buf.end(), 8 );
        for ( auto field: idx.fields )
        {
            std::uint8_t size = eggs::variants::apply( []( auto var ) { return var.size; }, field );
            const std::string& name = eggs::variants::apply( []( auto var ) { return var.name; }, field );
            std::uint8_t value = (blob & ((1 << size) - 1)).convert_to<std::uint8_t>();
            blob >>= size;
            branch->emplace_back( name, value );
            branch->back().format = descriptor::formatFunction.at( "hex" );
        }
    }

    // Nested described type
    void operator()(const descriptor::Nested& nest) const {
        // Type aliases sometimes appear as nested types because the alias was
        // defined after the type that uses it.  This is why we check typemap
        // here; if we could know when parsing the TOML that the type was
        // actually an alias not a nested type, then this would not be necessary.
        if (descriptor::terminalNameMap.count(nest.name))
            return operator()(descriptor::terminalNameMap.at(nest.name));

        if (!descriptor::catalog.count(nest.name))
            throw polysync::error("no nested descriptor for \"" + nest.name + "\"");
        const descriptor::Type& desc = descriptor::catalog.at(nest.name);
        Node a(fieldDescriptor.name, d->decode(desc));
        branch->emplace_back(fieldDescriptor.name, a);
        BOOST_LOG_SEV(d->log, severity::debug2) << fieldDescriptor.name << " = " << a
            << " (nested \"" << nest.name << "\")";
    }

    // Burn off unused or reserved space
    void operator()(const descriptor::Skip& skip) const
    {
        polysync::Bytes raw(skip.size);
        d->stream.read((char *)raw.data(), skip.size);
        std::string name = "skip-" + std::to_string(skip.order);
        branch->emplace_back(name, raw);
        BOOST_LOG_SEV(d->log, severity::debug2) << name << " " << raw;
    }

    void operator()(const descriptor::Array& desc) const
    {
        // Not sure yet if the array size is fixed, or read from a field in the parent node.
        auto sizefield = desc.size.target<std::string>();
        auto fixedsize = desc.size.target<size_t>();
        size_t size;

        if (sizefield)
        {
            // The branch should have a previous element with name sizefield
            auto node_iter = std::find_if(branch->begin(), branch->end(),
                    [sizefield](const Node& n) { return n.name == *sizefield; });

            if (node_iter == branch->end())
                throw polysync::error("array size indicator field not found")
                    << exception::field(*sizefield)
                    << status::description_error;

	    // Compute the size, regardless of the integer type, by just lexing
	    // to a string and converting the string back to an int.
	    // Otherwise, we would have to fuss with the exact integer type
	    // which is not very interesting here.
            std::stringstream os;
            os << *node_iter;
            try {
                size = std::stoll( os.str() );
            } catch ( std::invalid_argument ) {
                throw polysync::error("cannot parse array size value \"" + os.str() +
                        "\", string of size " + std::to_string(os.str().size()))
                    << exception::field(*sizefield)
                    << status::description_error;
            }
        }
        else
        {
            size = *fixedsize;
        }

        auto nesttype = desc.type.target<std::string>();

        if (nesttype)
        {
            if (!descriptor::catalog.count(*nesttype))
                throw polysync::error("unknown nested type");

            const descriptor::Type& nest = descriptor::catalog.at(*nesttype);
            std::vector<polysync::Tree> array;
            for (size_t i = 0; i < size; ++i)
            {
                BOOST_LOG_SEV(d->log, severity::debug2) << "decoding " << nest.name
                    <<  " #" << i + 1 << " of " << size;
                array.push_back(*(d->decode(nest).target<polysync::Tree>()));
            }
            branch->emplace_back(fieldDescriptor.name, array);
            BOOST_LOG_SEV(d->log, severity::debug2) << fieldDescriptor.name << " = " << array;
        }
        else
        {
            std::type_index idx = *desc.type.target<std::type_index>();
            std::vector<std::uint8_t> array(size);
            for(std::uint8_t& val: array)
            {
                d->decode(val);
            }
            branch->emplace_back(fieldDescriptor.name, array);
            BOOST_LOG_SEV(d->log, severity::debug2) << fieldDescriptor.name << " = " << array;
        }
    }

};

polysync::Variant Decoder::decode(const descriptor::Type& desc)
{
    polysync::Tree child(desc.name);

    try
    {
        std::for_each(desc.begin(), desc.end(), [&]( const descriptor::Field& field ) {
                eggs::variants::apply(branch_builder { child, field, this }, field.type);
                });
    }
    catch (polysync::error& e)
    {
        e << exception::module("decoder");
        e << exception::type(desc.name);
        e << exception::tree(child);
        throw;
    }
    catch ( std::ios_base::failure )
    {
        throw polysync::error("read error")
            << exception::module("decoder")
            << exception::type(desc.name);
    }

    return std::move(Node(desc.name, child));
}

void Decoder::decode( boost::multiprecision::cpp_int& value )
{
    // 16 is correct for ps_hash_type, but this needs to be flexible if other
    // types come along that are not 128 bits.  I have not been able to
    // determine how to compute how many bits a particular value cpp_int
    // actually needs.
    Bytes buf( 16 );
    stream.read( (char*)buf.data(), buf.size() );
    boost::multiprecision::import_bits( value, buf.begin(), buf.end(), 8 );
}

void Decoder::decode( Bytes& raw )
{
    stream.read( (char*)raw.data(), raw.size() );
}

Variant Decoder::operator()( const descriptor::Type& type )
{
    return decode( type );
}

}  // polysync
