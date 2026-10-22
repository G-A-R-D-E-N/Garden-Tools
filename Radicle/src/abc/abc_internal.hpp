#pragma once

#include <fo4/abc/abc.hpp>

#include <vector>

namespace fo4::abc::detail {

AbcStatus parse_constant_pool(ByteReader& reader, AbcConstantPool& out);
AbcStatus parse_metadata(ByteReader& reader, std::vector<AbcMetadata>& out);
AbcStatus check_pool_indices(const AbcFile& file);

}
