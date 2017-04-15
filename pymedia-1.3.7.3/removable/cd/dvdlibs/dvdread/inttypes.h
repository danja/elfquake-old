
#if !defined( __INTTYPES_H__ )
#define __INTTYPES_H__

#if defined( _STDINT_H ) 

	typedef unsigned short UINT16;
	typedef signed short INT16;
	typedef unsigned char UINT8;
	typedef unsigned int UINT32;
	typedef signed char INT8;
	typedef signed int INT32;

#	ifdef WIN32
		typedef signed __int64 INT64;
		typedef unsigned __int64 UINT64;
#	else
		typedef signed long long INT64;
		typedef unsigned long long UINT64;
#	endif

#    ifdef WIN32
#        define int64_t_C(c)     (c ## i64)
#        define uint64_t_C(c)    (c ## i64)

#        define inline __inline
#        define snprintf _snprintf

#    else
#        define int64_t_C(c)     (c ## LL)
#        define uint64_t_C(c)    (c ## ULL)
#    endif /* __MINGW32__ */


#else /* _STDINT_H */

#if !defined( _SYS_TYPES_H_ ) 

	typedef unsigned short UINT16;
	typedef signed short INT16;
	typedef unsigned char UINT8;
	typedef unsigned int UINT32;
	typedef signed char INT8;
	typedef signed int INT32;

#	ifdef WIN32
		typedef signed __int64 INT64;
		typedef unsigned __int64 UINT64;
#	else
		typedef signed long long INT64;
		typedef unsigned long long UINT64;
#	endif

	typedef UINT8 uint8_t;
	typedef UINT16 uint16_t;
	typedef UINT32 uint32_t;
	typedef UINT64 uint64_t;

#    ifdef WIN32
#        define int64_t_C(c)     (c ## i64)
#        define uint64_t_C(c)    (c ## i64)

#        define inline __inline
#        define snprintf _snprintf

#    else
#        define int64_t_C(c)     (c ## LL)
#        define uint64_t_C(c)    (c ## ULL)
#    endif /* __MINGW32__ */

#endif /* _SYS_TYPES_H_ */

#endif /* _STDINT_H */

#ifndef _SYS_TYPES_H

typedef INT8 int8_t;
typedef INT16 int16_t;
typedef INT32 int32_t;
typedef INT64 int64_t;

typedef char *  caddr_t;
typedef unsigned char u_int8_t;
typedef unsigned short u_int16_t;
typedef unsigned int u_int32_t;
#ifdef _MSC_VER
typedef unsigned __int64 u_int64_t;
#else
typedef unsigned long long u_int64_t;
#endif
typedef int32_t register_t;

#endif /* _SYS_TYPES_H */


#endif /* __INTTYPES_H__ */
