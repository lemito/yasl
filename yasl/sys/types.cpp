#include "types.h"

static_assert(sizeof(uint8) == 1, "size of uint8 must be 1 bytes");
static_assert(sizeof(uint16) == 2, "size of uint16 must be 2 bytes");
static_assert(sizeof(uint32) == 4, "size of uint32 must be 4 bytes");
static_assert(sizeof(uint64) == 8, "size of uint64 must be 8 bytes");

static_assert(sizeof(int8) == 1, "size of int8 must be 1 bytes");
static_assert(sizeof(int16) == 2, "size of int16 must be 2 bytes");
static_assert(sizeof(int32) == 4, "size of int32 must be 4 bytes");
static_assert(sizeof(int64) == 8, "size of int64 must be 8 bytes");
