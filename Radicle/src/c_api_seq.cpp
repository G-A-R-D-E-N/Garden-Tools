#include "fo4/c_api_seq.h"

#include "c_api_internal.hpp"
#include "fo4/seq/seq.hpp"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

struct radicle_seq {
  fo4::seq::SeqFile value;
};

namespace {

using namespace radicle_c_detail;

}

extern "C" {

radicle_status radicle_seq_parse(const uint8_t* bytes, size_t size, radicle_seq** out_seq) {
  clear_error();
  if ((!bytes && size != 0) || !out_seq) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "bytes and out_seq are required");
  }

  *out_seq = nullptr;
  try {
    std::vector<std::uint8_t> input;
    if (size != 0) {
      input.assign(bytes, bytes + size);
    }
    auto handle = std::make_unique<radicle_seq>();
    handle->value = fo4::seq::SeqFile::fromBytes(input);
    *out_seq = handle.release();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_PARSE_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while parsing .seq data");
  }
}

radicle_status radicle_seq_read_file(const char* path, radicle_seq** out_seq) {
  clear_error();
  if (!path || !out_seq) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "path and out_seq are required");
  }

  *out_seq = nullptr;
  try {
    std::ifstream stream(radicle_c_detail::utf8_path(path), std::ios::binary);
    if (!stream) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to open the .seq file");
    }

    const std::vector<char> raw((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) {
      return fail(RADICLE_STATUS_IO_ERROR, "failed to read the .seq file");
    }
    const std::vector<std::uint8_t> bytes(raw.begin(), raw.end());
    return radicle_seq_parse(bytes.data(), bytes.size(), out_seq);
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_IO_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while reading .seq data");
  }
}

void radicle_seq_destroy(radicle_seq* seq) {
  radicle_c_detail::clear_error();
  delete seq;
}

radicle_status radicle_seq_write_bytes(const radicle_seq* seq, uint8_t** out_bytes, size_t* out_size) {
  clear_error();
  if (!seq || !out_bytes || !out_size) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "seq, out_bytes and out_size are required");
  }

  *out_bytes = nullptr;
  *out_size = 0;
  try {
    const auto bytes = seq->value.toBytes();
    if (bytes.empty()) {
      return RADICLE_STATUS_OK;
    }
    auto* buffer = static_cast<uint8_t*>(std::malloc(bytes.size()));
    if (!buffer) {
      return fail(RADICLE_STATUS_OUT_OF_MEMORY, "failed to allocate serialized bytes");
    }
    std::memcpy(buffer, bytes.data(), bytes.size());
    *out_bytes = buffer;
    *out_size = bytes.size();
    return RADICLE_STATUS_OK;
  } catch (const std::bad_alloc& error) {
    return fail_exception(RADICLE_STATUS_OUT_OF_MEMORY, error);
  } catch (const std::exception& error) {
    return fail_exception(RADICLE_STATUS_INTERNAL_ERROR, error);
  } catch (...) {
    return fail(RADICLE_STATUS_INTERNAL_ERROR, "unknown exception while serializing .seq data");
  }
}

radicle_status radicle_seq_entry_count(const radicle_seq* seq, size_t* out_count) {
  clear_error();
  if (!seq || !out_count) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "seq and out_count are required");
  }
  *out_count = seq->value.entries.size();
  return RADICLE_STATUS_OK;
}

radicle_status radicle_seq_entry_form_id(const radicle_seq* seq, size_t index, uint32_t* out_form_id) {
  clear_error();
  if (!seq || !out_form_id) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "seq and out_form_id are required");
  }
  *out_form_id = 0;
  if (index >= seq->value.entries.size()) {
    return fail(RADICLE_STATUS_INVALID_ARGUMENT, "entry index is out of range");
  }
  *out_form_id = seq->value.entries[index].value();
  return RADICLE_STATUS_OK;
}

}
