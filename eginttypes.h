/*
 * eginttypes.h
 *
 *  Created on: 03/02/2013
 *      Author: volodya
 */

#ifndef EGINTTYPES_H_
#define EGINTTYPES_H_

#ifndef _MSC_VER
#include <inttypes.h>
#endif

#if __cplusplus >= 201103L
#include <cstdint>
#else
// fallback for C++0x non-compliant compilers
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;
typedef long long int64_t;
typedef unsigned long long uint64_t;

#endif

// printf specificators for size_t - from http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml
#ifdef _LP64
#define __PRIS_PREFIX "z"
#else
#define __PRIS_PREFIX
#endif

// Use these macros after a % in a printf format string
// to get correct 32/64 bit behavior, like this:
// size_t size = records.size();
// printf("%"PRIuS"\n", size);

#define PRIdS __PRIS_PREFIX "d"
#define PRIxS __PRIS_PREFIX "x"
#define PRIuS __PRIS_PREFIX "u"
#define PRIXS __PRIS_PREFIX "X"
#define PRIoS __PRIS_PREFIX "o"

#endif /* EGINTTYPES_H_ */
