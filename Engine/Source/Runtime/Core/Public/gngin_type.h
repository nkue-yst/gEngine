#pragma once

#if __cplusplus < 201103L
    #error This header requires C++11 or higher
#endif

#include <cstdint>
#include <cstdbool>

#define GNGIN_BOOL bool
#define GNGIN_TRUE true
#define GNGIN_FALSE false


typedef char GnginChar8;
typedef char16_t GnginChar16;
typedef char32_t GnginChar32;

typedef std::int8_t GnginSint8;
typedef std::int16_t GnginSint16;
typedef std::int32_t GnginSint32;
typedef std::int64_t GnginSint64;

typedef std::uint8_t GnginUint8;
typedef std::uint16_t GnginUint16;
typedef std::uint32_t GnginUint32;
typedef std::uint64_t GnginUint64;

typedef float GnginFloat32;
typedef double GnginFloat64;
