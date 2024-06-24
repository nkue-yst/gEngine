#pragma once

#if __cplusplus < 201103L
    #error This header requires C++11 or higher
#endif

#include <cstdint>
#include <cstdbool>

#define GN_BOOL bool
#define GN_TRUE true
#define GN_FALSE false


typedef char GnChar8;
typedef char16_t GnChar16;
typedef char32_t GnChar32;

typedef std::int8_t GnSint8;
typedef std::int16_t GnSint16;
typedef std::int32_t GnSint32;
typedef std::int64_t GnSint64;

typedef std::uint8_t  GnUint8;
typedef std::uint16_t GnUint16;
typedef std::uint32_t GnUint32;
typedef std::uint64_t GnUint64;

typedef float  GnFloat32;
typedef double GnFloat64;
