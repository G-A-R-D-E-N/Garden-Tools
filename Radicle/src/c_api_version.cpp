#include "fo4/c_api.h"

extern "C" uint32_t radicle_abi_version(void) {
  return RADICLE_ABI_VERSION;
}

extern "C" uint32_t radicle_abi_major(void) {
  return RADICLE_ABI_MAJOR;
}

extern "C" uint32_t radicle_abi_minor(void) {
  return RADICLE_ABI_MINOR;
}
