/*
 * vim:ts=8:sw=3:sts=8:noexpandtab:cino=>5n-3f0^-2{2
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#if HAVE___ATTRIBUTE__
#define __UNUSED__ __attribute__((unused))
#else
#define __UNUSED__
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <limits.h>

#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif

#if defined(_WIN32) && ! defined(__CEGCC__)
# include <winsock2.h>
#endif

#include <Eina.h>

#include "Eet.h"
#include "Eet_private.h"

/*
 * routines for doing data -> struct and struct -> data conversion
 *
 * types:
 *
 * basic types:
 *   a sequence of...
 *
 *   char
 *   short
 *   int
 *   long long
 *   float
 *   double
 *   unsigned char
 *   unsigned short
 *   unsigned int
 *   unsgined long long
 *   string
 *
 * groupings:
 *   multiple entries ordered as...
 *
 *   fixed size array    [ of basic types ]
 *   variable size array [ of basic types ]
 *   linked list         [ of basic types ]
 *   hash table          [ of basic types ]
 *
 * need to provide builder/accessor funcs for:
 *
 *   list_next
 *   list_append
 *
 *   hash_foreach
 *   hash_add
 *
 */

/*---*/

typedef struct _Eet_Data_Element            Eet_Data_Element;
typedef struct _Eet_Data_Basic_Type_Codec   Eet_Data_Basic_Type_Codec;
typedef struct _Eet_Data_Group_Type_Codec   Eet_Data_Group_Type_Codec;
typedef struct _Eet_Data_Chunk              Eet_Data_Chunk;
typedef struct _Eet_Data_Stream             Eet_Data_Stream;
typedef struct _Eet_Data_Descriptor_Hash    Eet_Data_Descriptor_Hash;
typedef struct _Eet_Data_Encode_Hash_Info   Eet_Data_Encode_Hash_Info;
typedef struct _Eet_Free		    Eet_Free;
typedef struct _Eet_Free_Context	    Eet_Free_Context;

/*---*/

/* TODO:
 * Eet_Data_Basic_Type_Codec (Coder, Decoder)
 * Eet_Data_Group_Type_Codec (Coder, Decoder)
 */
struct _Eet_Data_Basic_Type_Codec
{
   int         size;
   const char *name;
   int       (*get) (const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
   void     *(*put) (Eet_Dictionary *ed, const void *src, int *size_ret);
};

struct _Eet_Data_Group_Type_Codec
{
   int  (*get) (Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk, int type, int group_type, void *data_in, int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, char **p, int *size);
   void (*put) (Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in);
};

struct _Eet_Data_Chunk
{
   char          *name;
   int            len;
   int            size;
   int            hash;
   void          *data;
   unsigned char  type;
   unsigned char  group_type;
};

struct _Eet_Data_Stream
{
   void *data;
   int   size;
   int   pos;
};

struct _Eet_Data_Descriptor_Hash
{
   Eet_Data_Element         *element;
   Eet_Data_Descriptor_Hash *next;
};

struct _Eet_Data_Descriptor
{
   const char           *name;
   const Eet_Dictionary *ed;
   int                   size;
   struct {
      void *(*mem_alloc) (size_t size);
      void  (*mem_free) (void *mem);
      char *(*str_alloc) (const char *str);
      char *(*str_direct_alloc) (const char *str);
      void  (*str_free) (const char *str);
      void  (*str_direct_free) (const char *str);
      void *(*list_next) (void *l);
      void *(*list_append) (void *l, void *d);
      void *(*list_data) (void *l);
      void *(*list_free) (void *l);
      void  (*hash_foreach) (void *h, int (*func) (void *h, const char *k, void *dt, void *fdt), void *fdt);
      void *(*hash_add) (void *h, const char *k, void *d);
      void  (*hash_free) (void *h);
   } func;
   struct {
      int                num;
      Eet_Data_Element  *set;
      struct {
	 int                             size;
	 Eet_Data_Descriptor_Hash       *buckets;
      } hash;
   } elements;
//   char *strings;
//   int   strings_len;
};

struct _Eet_Data_Element
{
   const char          *name;
   const char          *counter_name;
   const char          *directory_name_ptr;
   Eet_Data_Descriptor *subtype;
   int                  offset;         /* offset in bytes from the base element */
   int                  count;          /* number of elements for a fixed array */
   int                  counter_offset; /* for a variable array we need the offset of the count variable */
   unsigned char        type;           /* EET_T_XXX */
   unsigned char        group_type;     /* EET_G_XXX */
};

struct _Eet_Data_Encode_Hash_Info
{
  Eet_Data_Stream       *ds;
  Eet_Data_Element      *ede;
  Eet_Dictionary        *ed;
};

struct _Eet_Free
{
  int     ref;
  int     len[256];
  int     num[256];
  void  **list[256];
};

struct _Eet_Free_Context
{
   Eet_Free freelist;
   Eet_Free freeleak;
   Eet_Free freelist_list;
   Eet_Free freelist_hash;
   Eet_Free freelist_str;
   Eet_Free freelist_direct_str;
};

/*---*/

static int   eet_data_get_char(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_char(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_short(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_short(Eet_Dictionary *ed, const void *src, int *size_ret);
static inline int   eet_data_get_int(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_int(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_long_long(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_long_long(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_float(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_float(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_double(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_double(Eet_Dictionary *ed, const void *src, int *size_ret);
static inline int   eet_data_get_string(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_string(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_istring(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_istring(Eet_Dictionary *ed, const void *src, int *size_ret);
static int   eet_data_get_null(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dest);
static void *eet_data_put_null(Eet_Dictionary *ed, const void *src, int *size_ret);

static int   eet_data_get_type(const Eet_Dictionary *ed, int type, const void *src, const void *src_end, void *dest);
static void *eet_data_put_type(Eet_Dictionary *ed, int type, const void *src, int *size_ret);

static void eet_data_dump_group_start(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, int group_type, const char *name);
static void eet_data_dump_group_end(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata);
static void eet_data_dump_level(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata);
static void eet_data_dump_simple_type(int type, const char *name, void *dd,
				      int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata);


static int  eet_data_get_unknown(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk, int type, int group_type, void *data_in, int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, char **p, int *size);
static void eet_data_put_unknown(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in);
static void eet_data_put_array(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in);
static int  eet_data_get_array(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk, int type, int group_type, void *data, int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, char **p, int *size);
static int  eet_data_get_list(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk, int type, int group_type, void *data_in, int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, char **p, int *size);
static void eet_data_put_list(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in);
static void eet_data_put_hash(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in);
static int  eet_data_get_hash(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk, int type, int group_type, void *data, int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata, char **p, int *size);

static void            eet_data_chunk_get(const Eet_Dictionary *ed, Eet_Data_Chunk *chnk, const void *src, int size);
static Eet_Data_Chunk *eet_data_chunk_new(void *data, int size, const char *name, int type, int group_type);
static void            eet_data_chunk_free(Eet_Data_Chunk *chnk);

static Eet_Data_Stream *eet_data_stream_new(void);
static void             eet_data_stream_write(Eet_Data_Stream *ds, const void *data, int size);
static void             eet_data_stream_free(Eet_Data_Stream *ds);

static void             eet_data_chunk_put(Eet_Dictionary *ed, Eet_Data_Chunk *chnk, Eet_Data_Stream *ds);

static int       eet_data_descriptor_encode_hash_cb(void *hash, const char *key, void *hdata, void *fdata);
static void     *_eet_data_descriptor_encode(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, const void *data_in, int *size_ret);
static void     *_eet_data_descriptor_decode(Eet_Free_Context *context,
					     const Eet_Dictionary *ed,
                                             Eet_Data_Descriptor *edd,
                                             const void *data_in,
                                             int size_in,
                                             int level,
                                             void (*dumpfunc) (void *data, const char *str),
                                             void *dumpdata);

/*---*/

static const Eet_Data_Basic_Type_Codec eet_basic_codec[] =
{
     {sizeof(char),      "char",       eet_data_get_char,      eet_data_put_char     },
     {sizeof(short),     "short",      eet_data_get_short,     eet_data_put_short    },
     {sizeof(int),       "int",        eet_data_get_int,       eet_data_put_int      },
     {sizeof(long long), "long_long",  eet_data_get_long_long, eet_data_put_long_long},
     {sizeof(float),     "float",      eet_data_get_float,     eet_data_put_float    },
     {sizeof(double),    "double",     eet_data_get_double,    eet_data_put_double   },
     {sizeof(char),      "uchar",      eet_data_get_char,      eet_data_put_char     },
     {sizeof(short),     "ushort",     eet_data_get_short,     eet_data_put_short    },
     {sizeof(int),       "uint",       eet_data_get_int,       eet_data_put_int      },
     {sizeof(long long), "ulong_long", eet_data_get_long_long, eet_data_put_long_long},
     {sizeof(char *),    "string",     eet_data_get_string,    eet_data_put_string   },
     {sizeof(char *),    "inlined",    eet_data_get_istring,   eet_data_put_istring  },
     {sizeof(void *),    "NULL",       eet_data_get_null,      eet_data_put_null     }
};

static const Eet_Data_Group_Type_Codec eet_group_codec[] =
{
     { eet_data_get_unknown,  eet_data_put_unknown },
     { eet_data_get_array,    eet_data_put_array },
     { eet_data_get_array,    eet_data_put_array },
     { eet_data_get_list,     eet_data_put_list },
     { eet_data_get_hash,     eet_data_put_hash }
};

static int words_bigendian = -1;

/*---*/

#define SWAP64(x) (x) = \
   ((((unsigned long long)(x) & 0x00000000000000ffULL ) << 56) |\
       (((unsigned long long)(x) & 0x000000000000ff00ULL ) << 40) |\
       (((unsigned long long)(x) & 0x0000000000ff0000ULL ) << 24) |\
       (((unsigned long long)(x) & 0x00000000ff000000ULL ) << 8) |\
       (((unsigned long long)(x) & 0x000000ff00000000ULL ) >> 8) |\
       (((unsigned long long)(x) & 0x0000ff0000000000ULL ) >> 24) |\
       (((unsigned long long)(x) & 0x00ff000000000000ULL ) >> 40) |\
       (((unsigned long long)(x) & 0xff00000000000000ULL ) >> 56))
#define SWAP32(x) (x) = \
   ((((int)(x) & 0x000000ff ) << 24) |\
       (((int)(x) & 0x0000ff00 ) << 8) |\
       (((int)(x) & 0x00ff0000 ) >> 8) |\
       (((int)(x) & 0xff000000 ) >> 24))
#define SWAP16(x) (x) = \
   ((((short)(x) & 0x00ff ) << 8) |\
       (((short)(x) & 0xff00 ) >> 8))

#define CONV8(x)
#define CONV16(x) {if (words_bigendian) SWAP16(x);}
#define CONV32(x) {if (words_bigendian) SWAP32(x);}
#define CONV64(x) {if (words_bigendian) SWAP64(x);}

#define IS_SIMPLE_TYPE(Type)    (Type > EET_T_UNKNOW && Type < EET_T_LAST)

/*---*/

/* CHAR TYPE */
static int
eet_data_get_char(const Eet_Dictionary *ed __UNUSED__, const void *src, const void *src_end, void *dst)
{
   char *s, *d;

   if (((char *)src + sizeof(char)) > (char *)src_end) return -1;
   s = (char *)src;
   d = (char *)dst;
   *d = *s;
   CONV8(*d);
   return sizeof(char);
}

static void *
eet_data_put_char(Eet_Dictionary *ed __UNUSED__, const void *src, int *size_ret)
{
   char *s, *d;

   d = (char *)malloc(sizeof(char));
   if (!d) return NULL;
   s = (char *)src;
   *d = *s;
   CONV8(*d);
   *size_ret = sizeof(char);
   return d;
}

/* SHORT TYPE */
static int
eet_data_get_short(const Eet_Dictionary *ed __UNUSED__, const void *src, const void *src_end, void *dst)
{
   short *d;

   if (((char *)src + sizeof(short)) > (char *)src_end) return -1;
   memcpy(dst, src, sizeof(short));
   d = (short *)dst;
   CONV16(*d);
   return sizeof(short);
}

static void *
eet_data_put_short(Eet_Dictionary *ed __UNUSED__, const void *src, int *size_ret)
{
   short *s, *d;

   d = (short *)malloc(sizeof(short));
   if (!d) return NULL;
   s = (short *)src;
   *d = *s;
   CONV16(*d);
   *size_ret = sizeof(short);
   return d;
}

/* INT TYPE */
static inline int
eet_data_get_int(const Eet_Dictionary *ed __UNUSED__, const void *src, const void *src_end, void *dst)
{
   int *d;

   if (((char *)src + sizeof(int)) > (char *)src_end) return -1;
   memcpy(dst, src, sizeof(int));
   d = (int *)dst;
   CONV32(*d);
   return sizeof(int);
}

static void *
eet_data_put_int(Eet_Dictionary *ed __UNUSED__, const void *src, int *size_ret)
{
   int *s, *d;

   d = (int *)malloc(sizeof(int));
   if (!d) return NULL;
   s = (int *)src;
   *d = *s;
   CONV32(*d);
   *size_ret = sizeof(int);
   return d;
}

/* LONG LONG TYPE */
static int
eet_data_get_long_long(const Eet_Dictionary *ed __UNUSED__, const void *src, const void *src_end, void *dst)
{
   unsigned long long *d;

   if (((char *)src + sizeof(unsigned long long)) > (char *)src_end) return -1;
   memcpy(dst, src, sizeof(unsigned long long));
   d = (unsigned long long *)dst;
   CONV64(*d);
   return sizeof(unsigned long long);
}

static void *
eet_data_put_long_long(Eet_Dictionary *ed __UNUSED__, const void *src, int *size_ret)
{
   unsigned long long *s, *d;

   d = (unsigned long long *)malloc(sizeof(unsigned long long));
   if (!d) return NULL;
   s = (unsigned long long *)src;
   *d = *s;
   CONV64(*d);
   *size_ret = sizeof(unsigned long long);
   return d;
}

/* STRING TYPE */
static inline int
eet_data_get_string_hash(const Eet_Dictionary *ed, const void *src, const void *src_end)
{
   if (ed)
     {
        int               index;

        if (eet_data_get_int(ed, src, src_end, &index) < 0) return -1;

        return eet_dictionary_string_get_hash(ed, index);
     }

   return -1;
}

static inline int
eet_data_get_string(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dst)
{
   char *s, **d;

   d = (char **)dst;

   if (ed)
     {
        const char       *str;
        int               index;

        if (eet_data_get_int(ed, src, src_end, &index) < 0) return -1;

        str = eet_dictionary_string_get_char(ed, index);
        if (str == NULL)
          return -1;

        *d = (char *) str;
        return eet_dictionary_string_get_size(ed, index);
     }

   s = (char *)src;
   if (s == NULL)
     {
        *d = NULL;
        return 0;
     }

   *d = s;
   return strlen(s) + 1;
}

static void *
eet_data_put_string(Eet_Dictionary *ed, const void *src, int *size_ret)
{
   char *s, *d;
   int len;

   if (ed)
     {
        const char      *str;
        int              index;

        str = *((const char **) src);
        if (!str) return NULL;

        index = eet_dictionary_string_add(ed, str);
        if (index == -1) return NULL;

        return eet_data_put_int(ed, &index, size_ret);
     }

   s = (char *)(*((char **)src));
   if (!s) return NULL;
   len = strlen(s);
   d = malloc(len + 1);
   if (!d) return NULL;
   memcpy(d, s, len + 1);
   *size_ret = len + 1;
   return d;
}

/* ALWAYS INLINED STRING TYPE */
static int
eet_data_get_istring(const Eet_Dictionary *ed __UNUSED__, const void *src, const void *src_end, void *dst)
{
   return eet_data_get_string(NULL, src, src_end, dst);
}

static void *
eet_data_put_istring(Eet_Dictionary *ed __UNUSED__, const void *src, int *size_ret)
{
   return eet_data_put_string(NULL, src, size_ret);
}

/* ALWAYS NULL TYPE */
static int
eet_data_get_null(const Eet_Dictionary *ed __UNUSED__, const void *src __UNUSED__, const void *src_end __UNUSED__, void *dst)
{
   char **d;

   d = (char**) dst;

   *d = NULL;
   return 0;
}

static void *
eet_data_put_null(Eet_Dictionary *ed __UNUSED__, const void *src __UNUSED__, int *size_ret)
{
   *size_ret = 0;
   return NULL;
}

/**
 * Fast lookups of simple doubles/floats.
 *
 * These aren't properly a cache because they don't store pre-calculated
 * values, but have a so simple math that is almost as fast.
 */
static inline int
_eet_data_float_cache_get(const char *s, int len, float *d)
{
   /* fast handle of simple case 0xMp+E*/
   if ((len == 6) && (s[0] == '0') && (s[1] == 'x') && (s[3] == 'p'))
     {
	int mantisse = (s[2] >= 'a') ? (s[2] - 'a' + 10) : (s[2] - '0');
	int exponent = (s[5] - '0');

	if (s[4] == '+') *d = (float)(mantisse << exponent);
	else             *d = (float)mantisse / (float)(1 << exponent);

	return 1;
     }
   return 0;
}

static inline int
_eet_data_double_cache_get(const char *s, int len, double *d)
{
   /* fast handle of simple case 0xMp+E*/
   if ((len == 6) && (s[0] == '0') && (s[1] == 'x') && (s[3] == 'p'))
     {
	int mantisse = (s[2] >= 'a') ? (s[2] - 'a' + 10) : (s[2] - '0');
	int exponent = (s[5] - '0');

	if (s[4] == '+') *d = (double)(mantisse << exponent);
	else             *d = (double)mantisse / (double)(1 << exponent);

	return 1;
     }
   return 0;
}

/* FLOAT TYPE */
static int
eet_data_get_float(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dst)
{
   float        *d;
   int           index;

   d = (float *) dst;
   if (!ed)
     {
        const char   *s, *p;
        long long     mantisse;
        long          exponent;
        int           len;

        s = (const char *)src;
        p = s;
        len = 0;
        while ((p < (const char *)src_end) && (*p != 0)) {len++; p++;}

        if (_eet_data_float_cache_get(s, len, d) != 0) return len + 1;

	if (eina_convert_atod(s, len, &mantisse, &exponent) == EINA_FALSE) return -1;
        *d = (float)ldexp((double)mantisse, exponent);

        return len + 1;
     }

   if (eet_data_get_int(ed, src, src_end, &index) < 0) return -1;

   if (!eet_dictionary_string_get_float(ed, index, d))
     return -1;
   return 1;
}

static void *
eet_data_put_float(Eet_Dictionary *ed, const void *src, int *size_ret)
{
   char  buf[128];
   int   index;

   eina_convert_dtoa((double)(*(float *)src), buf);

   if (!ed)
     {
        char    *d;
        int      len;

        len = strlen(buf);
        d = malloc(len + 1);
        if (!d) return NULL;
	memcpy(d, buf, len + 1);
        *size_ret = len + 1;
        return d;
     }

   index = eet_dictionary_string_add(ed, buf);
   if (index == -1) return NULL;

   return eet_data_put_int(ed, &index, size_ret);
}

/* DOUBLE TYPE */
static int
eet_data_get_double(const Eet_Dictionary *ed, const void *src, const void *src_end, void *dst)
{
   double       *d;
   int           index;

   d = (double *) dst;

   if (!ed)
     {
        const char     *s, *p;
        long long       mantisse = 0;
        long            exponent = 0;
        int             len;

        s = (const char *) src;
        p = s;
        len = 0;
        while ((p < (const char *)src_end) && (*p != 0)) {len++; p++;}

        if (_eet_data_double_cache_get(s, len, d) != 0) return len + 1;

	if (eina_convert_atod(s, len, &mantisse, &exponent) == EINA_FALSE) return -1;
        *d = ldexp((double) mantisse, exponent);

        return len + 1;
     }

   if (eet_data_get_int(ed, src, src_end, &index) < 0) return -1;

   if (!eet_dictionary_string_get_double(ed, index, d))
     return -1;
   return 1;
}

static void *
eet_data_put_double(Eet_Dictionary *ed, const void *src, int *size_ret)
{
   char  buf[128];
   int   index;

   eina_convert_dtoa((double)(*(double *)src), buf);

   if (!ed)
     {
        char   *d;
        int     len;

        len = strlen(buf);
        d = malloc(len + 1);
        if (!d) return NULL;
	memcpy(d, buf, len + 1);
        *size_ret = len + 1;

        return d;
     }

   index = eet_dictionary_string_add(ed, buf);
   if (index == -1) return NULL;

   return eet_data_put_int(ed, &index, size_ret);
}

static inline int
eet_data_get_type(const Eet_Dictionary *ed, int type, const void *src, const void *src_end, void *dest)
{
   int ret;

   ret = eet_basic_codec[type - 1].get(ed, src, src_end, dest);
   return ret;
}

static inline void *
eet_data_put_type(Eet_Dictionary *ed, int type, const void *src, int *size_ret)
{
   void *ret;

   ret = eet_basic_codec[type - 1].put(ed, src, size_ret);
   return ret;
}

/* chunk format...
 *
 * char[4] = "CHnK"; // untyped data ... or
 * char[4] = "CHKx"; // typed data - x == type
 *
 * int     = chunk size (including magic string);
 * char[]  = chunk magic/name string (0 byte terminated);
 * ... sub-chunks (a chunk can contain chuncks recusrively) ...
 * or
 * ... payload data ...
 *
 */

static inline void
eet_data_chunk_get(const Eet_Dictionary *ed, Eet_Data_Chunk *chnk,
		   const void *src, int size)
{
   const char *s;
   int ret1, ret2;

   if (!src) return;
   if (size <= 8) return;

   if (!chnk) return;

   s = src;
   if (s[2] == 'K')
     {
	if ((s[0] != 'C') || (s[1] != 'H') || (s[2] != 'K'))
	  return;

	chnk->type = (unsigned char)(s[3]);
	if (chnk->type > EET_T_LAST)
	  {
	     chnk->group_type = chnk->type;
	     chnk->type = EET_T_UNKNOW;
	  }
	else
	  chnk->group_type = EET_G_UNKNOWN;
	if ((chnk->type >= EET_T_LAST) ||
	    (chnk->group_type >= EET_G_LAST))
	  {
	     chnk->type = 0;
	     chnk->group_type = 0;
	  }
     }
   else
     {
	if ((s[0] != 'C') || (s[1] != 'H') || (s[2] != 'n') || (s[3] != 'K'))
	  return;
     }
   ret1 = eet_data_get_type(ed, EET_T_INT, (s + 4), (s + size), &(chnk->size));
   if (ret1 <= 0) return;
   if ((chnk->size < 0) || ((chnk->size + 8) > size)) return;
   ret2 = eet_data_get_type(ed, EET_T_STRING, (s + 8), (s + size), &(chnk->name));
   if (ret2 <= 0) return;

   chnk->len = ret2;

   /* Precalc hash */
   chnk->hash = eet_data_get_string_hash(ed, (s + 8), (s + size));

   if (ed)
     {
        chnk->data = (char *)src + 4 + ret1 + sizeof(int);
        chnk->size -= sizeof(int);
     }
   else
     {
        chnk->data = (char *)src + 4 + ret1 + chnk->len;
        chnk->size -= chnk->len;
     }

   return;
}

static inline Eet_Data_Chunk *
eet_data_chunk_new(void *data, int size, const char *name, int type, int group_type)
{
   Eet_Data_Chunk *chnk;

   if (!name) return NULL;
   chnk = calloc(1, sizeof(Eet_Data_Chunk));
   if (!chnk) return NULL;

   chnk->name = strdup(name);
   chnk->len = strlen(name) + 1;
   chnk->size = size;
   chnk->data = data;
   chnk->type = type;
   chnk->group_type = group_type;
   return chnk;
}

static inline void
eet_data_chunk_free(Eet_Data_Chunk *chnk)
{
   if (chnk->name) free(chnk->name);
   free(chnk);
}

static inline Eet_Data_Stream *
eet_data_stream_new(void)
{
   Eet_Data_Stream *ds;

   ds = calloc(1, sizeof(Eet_Data_Stream));
   if (!ds) return NULL;
   return ds;
}

static inline void
eet_data_stream_free(Eet_Data_Stream *ds)
{
   if (ds->data) free(ds->data);
   free(ds);
}

static inline void
eet_data_stream_write(Eet_Data_Stream *ds, const void *data, int size)
{
   char *p;

   if ((ds->pos + size) > ds->size)
     {
	ds->data = realloc(ds->data, ds->size + size + 512);
	if (!ds->data)
	  {
	     ds->pos = 0;
	     ds->size = 0;
	     return;
	  }
	ds->size = ds->size + size + 512;
     }
   p = ds->data;
   memcpy(p + ds->pos, data, size);
   ds->pos += size;
}

static void
eet_data_chunk_put(Eet_Dictionary *ed, Eet_Data_Chunk *chnk, Eet_Data_Stream *ds)
{
   int  *size;
   void *string;
   int   s;
   int   size_ret = 0;
   int   string_ret = 0;
   unsigned char buf[4] = "CHK";

   if (!chnk->data && chnk->type != EET_T_NULL) return;
   /* chunk head */

/*   eet_data_stream_write(ds, "CHnK", 4);*/
   if (chnk->type != EET_T_UNKNOW) buf[3] = chnk->type;
   else buf[3] = chnk->group_type;

   string = eet_data_put_string(ed, &chnk->name, &string_ret);
   if (!string)
     return ;

   /* size of chunk payload data + name */
   s = chnk->size + string_ret;
   size = eet_data_put_int(ed, &s, &size_ret);

   /* FIXME: If something goes wrong the resulting file will be corrupted. */
   if (!size)
     goto on_error;

   eet_data_stream_write(ds, buf, 4);

   /* write chunk length */
   eet_data_stream_write(ds, size, size_ret);

   /* write chunk name */
   eet_data_stream_write(ds, string, string_ret);

   /* write payload */
   eet_data_stream_write(ds, chnk->data, chnk->size);

   free(string);
 on_error:
   free(size);
}

/*---*/

static void
_eet_descriptor_hash_new(Eet_Data_Descriptor *edd)
{
   int i;

   edd->elements.hash.size = 1 << 6;
   edd->elements.hash.buckets = calloc(1, sizeof(Eet_Data_Descriptor_Hash) * edd->elements.hash.size);
   for (i = 0; i < edd->elements.num; i++)
     {
	Eet_Data_Element *ede;
	int hash;

	ede = &(edd->elements.set[i]);
	hash = _eet_hash_gen((char *) ede->name, 6);
	if (!edd->elements.hash.buckets[hash].element)
	  edd->elements.hash.buckets[hash].element = ede;
	else
	  {
	     Eet_Data_Descriptor_Hash *bucket;

	     bucket = calloc(1, sizeof(Eet_Data_Descriptor_Hash));
	     bucket->element = ede;
	     bucket->next = edd->elements.hash.buckets[hash].next;
	     edd->elements.hash.buckets[hash].next = bucket;
	  }
     }
}

static void
_eet_descriptor_hash_free(Eet_Data_Descriptor *edd)
{
   int i;

   for (i = 0; i < edd->elements.hash.size; i++)
     {
	Eet_Data_Descriptor_Hash *bucket, *pbucket;

	bucket = edd->elements.hash.buckets[i].next;
	while (bucket)
	  {
	     pbucket = bucket;
	     bucket = bucket->next;
	     free(pbucket);
	  }
     }
   if (edd->elements.hash.buckets) free(edd->elements.hash.buckets);
}

static Eet_Data_Element *
_eet_descriptor_hash_find(Eet_Data_Descriptor *edd, char *name, int hash)
{
   Eet_Data_Descriptor_Hash *bucket;

   if (hash < 0) hash = _eet_hash_gen(name, 6);
   else hash &= 0x3f;
   if (!edd->elements.hash.buckets[hash].element) return NULL;
   /*
     When we use the dictionnary as a source for chunk name, we will always
     have the same pointer in name. It's a good idea to just compare pointer
     instead of running strcmp on both string.
   */
   if (edd->elements.hash.buckets[hash].element->directory_name_ptr == name)
     return edd->elements.hash.buckets[hash].element;
   if (!strcmp(edd->elements.hash.buckets[hash].element->name, name))
     {
	edd->elements.hash.buckets[hash].element->directory_name_ptr = name;
	return edd->elements.hash.buckets[hash].element;
     }
   bucket = edd->elements.hash.buckets[hash].next;
   while (bucket)
     {
	if (bucket->element->directory_name_ptr == name) return bucket->element;
	if (!strcmp(bucket->element->name, name))
	  {
	     bucket->element->directory_name_ptr = name;
	     return bucket->element;
	  }
	bucket = bucket->next;
     }
   return NULL;
}

static void *
_eet_mem_alloc(size_t size)
{
   return calloc(1, size);
}

static void
_eet_mem_free(void *mem)
{
   free(mem);
}

static char *
_eet_str_alloc(const char *str)
{
   return strdup(str);
}

static void
_eet_str_free(const char *str)
{
   free((char *)str);
}

/*---*/

EAPI Eet_Data_Descriptor *
eet_data_descriptor_new(const char *name,
			int size,
			void *(*func_list_next) (void *l),
			void *(*func_list_append) (void *l, void *d),
			void *(*func_list_data) (void *l),
			void *(*func_list_free) (void *l),
			void  (*func_hash_foreach) (void *h, int (*func) (void *h, const char *k, void *dt, void *fdt), void *fdt),
			void *(*func_hash_add) (void *h, const char *k, void *d),
			void  (*func_hash_free) (void *h))
{
   Eet_Data_Descriptor *edd;

   if (!name) return NULL;
   edd = calloc(1, sizeof(Eet_Data_Descriptor));
   if (!edd) return NULL;

   edd->name = name;
   edd->ed = NULL;
   edd->size = size;
   edd->func.mem_alloc = _eet_mem_alloc;
   edd->func.mem_free = _eet_mem_free;
   edd->func.str_alloc = _eet_str_alloc;
   edd->func.str_direct_alloc = NULL;
   edd->func.str_direct_free = NULL;
   edd->func.str_free = _eet_str_free;
   edd->func.list_next = func_list_next;
   edd->func.list_append = func_list_append;
   edd->func.list_data = func_list_data;
   edd->func.list_free = func_list_free;
   edd->func.hash_foreach = func_hash_foreach;
   edd->func.hash_add = func_hash_add;
   edd->func.hash_free = func_hash_free;
   return edd;
}

/* new replcement */
EAPI Eet_Data_Descriptor *
eet_data_descriptor2_new(Eet_Data_Descriptor_Class *eddc)
{
   Eet_Data_Descriptor *edd;

   if (!eddc) return NULL;
   if (eddc->version < 1) return NULL;
   edd = calloc(1, sizeof(Eet_Data_Descriptor));
   if (!edd) return NULL;

   edd->name = eddc->name;
   edd->ed = NULL;
   edd->size = eddc->size;
   edd->func.mem_alloc = _eet_mem_alloc;
   edd->func.mem_free = _eet_mem_free;
   edd->func.str_alloc = _eet_str_alloc;
   edd->func.str_free = _eet_str_free;
   if (eddc->func.mem_alloc)
     edd->func.mem_alloc = eddc->func.mem_alloc;
   if (eddc->func.mem_free)
     edd->func.mem_free = eddc->func.mem_free;
   if (eddc->func.str_alloc)
     edd->func.str_alloc = eddc->func.str_alloc;
   if (eddc->func.str_free)
     edd->func.str_free = eddc->func.str_free;
   edd->func.list_next = eddc->func.list_next;
   edd->func.list_append = eddc->func.list_append;
   edd->func.list_data = eddc->func.list_data;
   edd->func.list_free = eddc->func.list_free;
   edd->func.hash_foreach = eddc->func.hash_foreach;
   edd->func.hash_add = eddc->func.hash_add;
   edd->func.hash_free = eddc->func.hash_free;

   return edd;
}

EAPI Eet_Data_Descriptor *
eet_data_descriptor3_new(Eet_Data_Descriptor_Class *eddc)
{
   Eet_Data_Descriptor *edd;

   if (!eddc) return NULL;
   if (eddc->version < 2) return NULL;
   edd = calloc(1, sizeof(Eet_Data_Descriptor));
   if (!edd) return NULL;

   edd->name = eddc->name;
   edd->ed = NULL;
   edd->size = eddc->size;
   edd->func.mem_alloc = _eet_mem_alloc;
   edd->func.mem_free = _eet_mem_free;
   edd->func.str_alloc = _eet_str_alloc;
   edd->func.str_free = _eet_str_free;
   if (eddc->func.mem_alloc)
     edd->func.mem_alloc = eddc->func.mem_alloc;
   if (eddc->func.mem_free)
     edd->func.mem_free = eddc->func.mem_free;
   if (eddc->func.str_alloc)
     edd->func.str_alloc = eddc->func.str_alloc;
   if (eddc->func.str_free)
     edd->func.str_free = eddc->func.str_free;
   edd->func.list_next = eddc->func.list_next;
   edd->func.list_append = eddc->func.list_append;
   edd->func.list_data = eddc->func.list_data;
   edd->func.list_free = eddc->func.list_free;
   edd->func.hash_foreach = eddc->func.hash_foreach;
   edd->func.hash_add = eddc->func.hash_add;
   edd->func.hash_free = eddc->func.hash_free;
   edd->func.str_direct_alloc = eddc->func.str_direct_alloc;
   edd->func.str_direct_free = eddc->func.str_direct_free;

   return edd;
}

EAPI void
eet_data_descriptor_free(Eet_Data_Descriptor *edd)
{
   _eet_descriptor_hash_free(edd);
   if (edd->elements.set) free(edd->elements.set);
   free(edd);
}

EAPI void
eet_data_descriptor_element_add(Eet_Data_Descriptor *edd,
				const char *name,
				int type,
				int group_type,
				int offset,
				int count,
/* 				int counter_offset, */
				const char *counter_name /* Useless should go on a major release */,
				Eet_Data_Descriptor *subtype)
{
   Eet_Data_Element *ede;
   /* int l1, l2, p1, p2, i;
   char *ps;*/

   /* FIXME: Fail safely when realloc fail. */
   edd->elements.num++;
   edd->elements.set = realloc(edd->elements.set, edd->elements.num * sizeof(Eet_Data_Element));
   if (!edd->elements.set) return;
   ede = &(edd->elements.set[edd->elements.num - 1]);
   ede->name = name;
   ede->directory_name_ptr = NULL;

   /*
    * We do a special case when we do list,hash or whatever group of simple type.
    * Instead of handling it in encode/decode/dump/undump, we create an
    * implicit structure with only the simple type.
    */
   if (group_type > EET_G_UNKNOWN
       && group_type < EET_G_LAST
       && type > EET_T_UNKNOW && type < EET_T_STRING
       && subtype == NULL)
     {
	subtype = calloc(1, sizeof (Eet_Data_Descriptor));
	if (!subtype) return ;
	subtype->name = "implicit";
	subtype->size = eet_basic_codec[type - 1].size;
	memcpy(&subtype->func, &edd->func, sizeof(subtype->func));

	eet_data_descriptor_element_add(subtype, eet_basic_codec[type - 1].name, type,
					EET_G_UNKNOWN, 0, 0, /* 0,  */NULL, NULL);
	type = EET_T_UNKNOW;
     }

   ede->type = type;
   ede->group_type = group_type;
   ede->offset = offset;
   ede->count = count;
   /* FIXME: For the time being, EET_G_VAR_ARRAY will put the counter_offset in count. */
   ede->counter_offset = count;
/*    ede->counter_offset = counter_offset; */
   ede->counter_name = counter_name;

   ede->subtype = subtype;
}

EAPI void *
eet_data_read_cipher(Eet_File *ef, Eet_Data_Descriptor *edd, const char *name, const char *key)
{
   const Eet_Dictionary *ed = NULL;
   const void           *data = NULL;
   void                 *data_dec;
   Eet_Free_Context      context;
   int                   required_free = 0;
   int                   size;

   ed = eet_dictionary_get(ef);

   if (!key)
     data = eet_read_direct(ef, name, &size);
   if (!data)
     {
	required_free = 1;
	data = eet_read_cipher(ef, name, &size, key);
	if (!data) return NULL;
     }

   memset(&context, 0, sizeof (context));
   data_dec = _eet_data_descriptor_decode(&context, ed, edd, data, size, 0, NULL, NULL);
   if (required_free)
     free((void*)data);

   return data_dec;
}

EAPI void *
eet_data_read(Eet_File *ef, Eet_Data_Descriptor *edd, const char *name)
{
   return eet_data_read_cipher(ef, edd, name, NULL);
}

EAPI int
eet_data_write_cipher(Eet_File *ef, Eet_Data_Descriptor *edd, const char *name, const char *key, const void *data, int compress)
{
   Eet_Dictionary       *ed;
   void                 *data_enc;
   int                   size;
   int                   val;

   ed = eet_dictionary_get(ef);

   data_enc = _eet_data_descriptor_encode(ed, edd, data, &size);
   if (!data_enc) return 0;
   val = eet_write_cipher(ef, name, data_enc, size, compress, key);
   free(data_enc);
   return val;
}

EAPI int
eet_data_write(Eet_File *ef, Eet_Data_Descriptor *edd, const char *name, const void *data, int compress)
{
   return eet_data_write_cipher(ef, edd, name, NULL, data, compress);
}

static int
_eet_free_hash(void *data)
{
   unsigned long ptr = (unsigned long)(data);
   int hash;

   hash = ptr;
   hash ^= ptr >> 8;
   hash ^= ptr >> 16;
   hash ^= ptr >> 24;

#if LONG_BIT != 32
   hash ^= ptr >> 32;
   hash ^= ptr >> 40;
   hash ^= ptr >> 48;
   hash ^= ptr >> 56;
#endif

   return hash & 0xFF;
}

static void
_eet_free_add(Eet_Free *ef, void *data)
{
   int hash;
   int i;

   hash = _eet_free_hash(data);

   for (i = 0; i < ef->num[hash]; ++i)
     if (ef->list[hash][i] == data) return;

   ef->num[hash]++;
   if (ef->num[hash] > ef->len[hash])
     {
        void    **tmp;

        tmp = realloc(ef->list[hash], (ef->len[hash] + 16) * sizeof(void*));
        if (!tmp) return ;

        ef->len[hash] += 16;
        ef->list[hash] = tmp;
     }
   ef->list[hash][ef->num[hash] - 1] = data;
}
static void
_eet_free_reset(Eet_Free *ef)
{
   int i;

   if (ef->ref > 0) return ;
   for (i = 0; i < 256; ++i)
     {
	ef->len[i] = 0;
	ef->num[i] = 0;
	if (ef->list[i]) free(ef->list[i]);
	ef->list[i] = NULL;
     }
}
static void
_eet_free_ref(Eet_Free *ef)
{
   ef->ref++;
}
static void
_eet_free_unref(Eet_Free *ef)
{
   ef->ref--;
}

#define _eet_freelist_add(Ctx, Data)	_eet_free_add(&Ctx->freelist, Data);
#define _eet_freelist_reset(Ctx)	_eet_free_reset(&Ctx->freelist);
#define _eet_freelist_ref(Ctx)		_eet_free_ref(&Ctx->freelist);
#define _eet_freelist_unref(Ctx)	_eet_free_unref(&Ctx->freelist);

static void
_eet_freelist_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freelist.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freelist.num[j]; ++i)
       {
	  if (edd)
	    edd->func.mem_free(context->freelist.list[j][i]);
	  else
	    free(context->freelist.list[j][i]);
       }
   _eet_free_reset(&context->freelist);
}

#define _eet_freeleak_add(Ctx, Data)	_eet_free_add(&Ctx->freeleak, Data);
#define _eet_freeleak_reset(Ctx)	_eet_free_reset(&Ctx->freeleak);
#define _eet_freeleak_ref(Ctx)		_eet_free_ref(&Ctx->freeleak);
#define _eet_freeleak_unref(Ctx)	_eet_free_unref(&Ctx->freeleak);

static void
_eet_freeleak_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freeleak.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freeleak.num[j]; ++i)
       {
	  if (edd)
	    edd->func.mem_free(context->freeleak.list[j][i]);
	  else
	    free(context->freeleak.list[j][i]);
       }
   _eet_free_reset(&context->freeleak);
}

#define _eet_freelist_list_add(Ctx, Data)  _eet_free_add(&Ctx->freelist_list, Data);
#define _eet_freelist_list_reset(Ctx)      _eet_free_reset(&Ctx->freelist_list);
#define _eet_freelist_list_ref(Ctx)        _eet_free_ref(&Ctx->freelist_list);
#define _eet_freelist_list_unref(Ctx)      _eet_free_unref(&Ctx->freelist_list);

static void
_eet_freelist_list_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freelist_list.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freelist_list.num[j]; ++i)
       {
	  if (edd)
	    edd->func.list_free(*((void**)(context->freelist_list.list[j][i])));
       }
   _eet_free_reset(&context->freelist_list);
}

#define _eet_freelist_str_add(Ctx, Data)   _eet_free_add(&Ctx->freelist_str, Data);
#define _eet_freelist_str_reset(Ctx)       _eet_free_reset(&Ctx->freelist_str);
#define _eet_freelist_str_ref(Ctx)         _eet_free_ref(&Ctx->freelist_str);
#define _eet_freelist_str_unref(Ctx)       _eet_free_unref(&Ctx->freelist_str);

static void
_eet_freelist_str_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freelist_str.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freelist_str.num[j]; ++i)
       {
	  if (edd)
	    edd->func.str_free(context->freelist_str.list[j][i]);
	  else
	    free(context->freelist_str.list[j][i]);
       }
   _eet_free_reset(&context->freelist_str);
}

#define _eet_freelist_direct_str_add(Ctx, Data)    _eet_free_add(&Ctx->freelist_direct_str, Data);
#define _eet_freelist_direct_str_reset(Ctx)        _eet_free_reset(&Ctx->freelist_direct_str);
#define _eet_freelist_direct_str_ref(Ctx)          _eet_free_ref(&Ctx->freelist_direct_str);
#define _eet_freelist_direct_str_unref(Ctx)        _eet_free_unref(&Ctx->freelist_direct_str);

static void
_eet_freelist_direct_str_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freelist_direct_str.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freelist_direct_str.num[j]; ++i)
       {
	  if (edd)
	    edd->func.str_direct_free(context->freelist_direct_str.list[j][i]);
	  else
	    free(context->freelist_direct_str.list[j][i]);
       }
   _eet_free_reset(&context->freelist_direct_str);
}

#define _eet_freelist_hash_add(Ctx, Data) _eet_free_add(&Ctx->freelist_hash, Data);
#define _eet_freelist_hash_reset(Ctx)     _eet_free_reset(&Ctx->freelist_hash);
#define _eet_freelist_hash_ref(Ctx)	  _eet_free_ref(&Ctx->freelist_hash);
#define _eet_freelist_hash_unref(Ctx)	  _eet_free_unref(&Ctx->freelist_hash);

static void
_eet_freelist_hash_free(Eet_Free_Context *context, Eet_Data_Descriptor *edd)
{
   int j;
   int i;

   if (context->freelist_hash.ref > 0) return;
   for (j = 0; j < 256; ++j)
     for (i = 0; i < context->freelist_hash.num[j]; ++i)
       {
	  if (edd)
	    edd->func.hash_free(context->freelist_hash.list[j][i]);
	  else
	    free(context->freelist_hash.list[j][i]);
       }
   _eet_free_reset(&context->freelist_hash);
}

static void
_eet_freelist_all_ref(Eet_Free_Context *freelist_context)
{
   _eet_freelist_ref(freelist_context);
   _eet_freeleak_ref(freelist_context);
   _eet_freelist_str_ref(freelist_context);
   _eet_freelist_list_ref(freelist_context);
   _eet_freelist_hash_ref(freelist_context);
   _eet_freelist_direct_str_ref(freelist_context);
}

static void
_eet_freelist_all_unref(Eet_Free_Context *freelist_context)
{
   _eet_freelist_unref(freelist_context);
   _eet_freeleak_unref(freelist_context);
   _eet_freelist_str_unref(freelist_context);
   _eet_freelist_list_unref(freelist_context);
   _eet_freelist_hash_unref(freelist_context);
   _eet_freelist_direct_str_unref(freelist_context);
}

static int
eet_data_descriptor_encode_hash_cb(void *hash __UNUSED__, const char *key, void *hdata, void *fdata)
{
   Eet_Dictionary               *ed;
   Eet_Data_Encode_Hash_Info    *edehi;
   Eet_Data_Stream              *ds;
   Eet_Data_Element             *ede;
   Eet_Data_Chunk               *echnk;
   void                         *data = NULL;
   int                           size;

   edehi = fdata;
   ede = edehi->ede;
   ds = edehi->ds;
   ed = edehi->ed;

   /* Store key */
   data = eet_data_put_type(ed,
                            EET_T_STRING,
			    &key,
			    &size);
   if (data)
     {
	echnk = eet_data_chunk_new(data, size, ede->name, ede->type, ede->group_type);
	eet_data_chunk_put(ed, echnk, ds);
	eet_data_chunk_free(echnk);
	free(data);
	data = NULL;
     }

   EET_ASSERT(!((ede->type > EET_T_UNKNOW) && (ede->type < EET_T_STRING)), return );

   /* Store data */
   if (ede->type >= EET_T_STRING)
     eet_data_put_unknown(ed, NULL, ede, ds, &hdata);
   else
     {
	if (ede->subtype)
	  data = _eet_data_descriptor_encode(ed,
					     ede->subtype,
					     hdata,
					     &size);
	if (data)
	  {
	     echnk = eet_data_chunk_new(data, size, ede->name, ede->type, ede->group_type);
	     eet_data_chunk_put(ed, echnk, ds);
	     eet_data_chunk_free(echnk);
	     free(data);
	     data = NULL;
	  }
     }

   return 1;
}

static char *
_eet_data_string_escape(const char *str)
{
   char *s, *sp;
   const char *strp;
   int sz = 0;

   for (strp = str; *strp; strp++)
     {
	if (*strp == '\"') sz += 2;
	else if (*strp == '\\') sz += 2;
	else sz += 1;
     }
   s = malloc(sz + 1);
   if (!s) return NULL;
   for (strp = str, sp = s; *strp; strp++, sp++)
     {
	if (*strp == '\"')
	  {
	     *sp = '\\';
	     sp++;
	  }
	else if (*strp == '\\')
	  {
	     *sp = '\\';
	     sp++;
	  }
	*sp = *strp;
     }
   *sp = 0;
   return s;
}

static void
_eet_data_dump_string_escape(void *dumpdata, void dumpfunc(void *data, const char *str), const char *str)
{
   char *s;

   s = _eet_data_string_escape(str);
   if (s)
     {
	dumpfunc(dumpdata, s);
	free(s);
     }
}

static char *
_eet_data_dump_token_get(const char *src, int *len)
{
   const char *p;
   char *tok = NULL;
   int in_token = 0;
   int in_quote = 0;
   int tlen = 0, tsize = 0;

#define TOK_ADD(x) \
   { \
      tlen++; \
      if (tlen >= tsize) \
	{ \
	   tsize += 32; \
	   tok = realloc(tok, tsize); \
	} \
      tok[tlen - 1] = x; \
   }

   for (p = src; *len > 0; p++, (*len)--)
     {
	if (in_token)
	  {
	     if (in_quote)
	       {
		  if ((p[0] == '\"') && (p > src) && (p[-1] != '\\'))
		    {
		       in_quote = 0;
		    }
		  else if ((p[0] == '\\') && (*len > 1) && (p[1] == '\"'))
		    {
		       /* skip */
		    }
		  else if ((p[0] == '\\') && (p > src) && (p[-1] == '\\'))
		    {
		       /* skip */
		    }
		  else
		    TOK_ADD(p[0]);
	       }
	     else
	       {
		  if (p[0] == '\"') in_quote = 1;
		  else
		    {
		       if ((isspace(p[0])) || (p[0] == ';')) /* token ends here */
			 {
			    TOK_ADD(0);
			    (*len)--;
			    return tok;
			 }
		       else
			 TOK_ADD(p[0]);
		    }
	       }
	  }
	else
	  {
	     if (!((isspace(p[0])) || (p[0] == ';')))
	       {
		  in_token = 1;
		  (*len)++;
		  p--;
	       }
	  }
     }
   if (in_token)
     {
	TOK_ADD(0);
	return tok;
     }
   if (tok) free(tok);
   return NULL;
}

static void *
_eet_data_dump_encode(Eet_Dictionary *ed,
                      Eet_Node *node,
		      int *size_ret)
{
   Eet_Data_Chunk *chnk = NULL, *echnk = NULL;
   Eet_Data_Stream *ds;
   void *cdata, *data;
   int csize, size;
   Eet_Node *n;

   if (words_bigendian == -1)
     {
	unsigned long int v;

	v = htonl(0x12345678);
	if (v == 0x12345678) words_bigendian = 1;
	else words_bigendian = 0;
     }

   ds = eet_data_stream_new();
   if (!ds) return NULL;

   switch (node->type)
     {
      case EET_G_UNKNOWN:
	for (n = node->values; n; n = n->next)
	  {
	     data = _eet_data_dump_encode(ed, n, &size);
	     if (data)
	       {
		  eet_data_stream_write(ds, data, size);
		  free(data);
	       }
	  }
	break;
      case EET_G_ARRAY:
      case EET_G_VAR_ARRAY:
	data = eet_data_put_type(ed,
				 EET_T_INT,
				 &node->count,
				 &size);
	if (data)
	  {
	     echnk = eet_data_chunk_new(data, size, node->name, node->type, node->type);
	     eet_data_chunk_put(ed, echnk, ds);
	     eet_data_chunk_free(echnk);
	     free(data);
	  }
	for (n = node->values; n; n = n->next)
	  {
	     data = _eet_data_dump_encode(ed, n, &size);
	     if (data)
	       {
		  echnk = eet_data_chunk_new(data, size, node->name, node->type, node->type);
		  eet_data_chunk_put(ed, echnk, ds);
		  eet_data_chunk_free(echnk);
		  free(data);
	       }
	  }

	/* Array is somekind of special case, so we should embed it inside another chunk. */
	*size_ret = ds->pos;
	cdata = ds->data;

	ds->data = NULL;
	ds->size = 0;
	eet_data_stream_free(ds);

	return cdata;
      case EET_G_LIST:
	for (n = node->values; n; n = n->next)
	  {
	     data = _eet_data_dump_encode(ed, n, &size);
	     if (data)
	       {
		  eet_data_stream_write(ds, data, size);
		  free(data);
	       }
	  }
	break;
      case EET_G_HASH:
	if (node->key)
	  {
	     data = eet_data_put_type(ed,
                                      EET_T_STRING,
				      &node->key,
				      &size);
	     if (data)
	       {
		  echnk = eet_data_chunk_new(data, size, node->name, node->type, node->type);
		  eet_data_chunk_put(ed, echnk, ds);
		  eet_data_chunk_free(echnk);
		  free(data);
	       }
	  }
	for (n = node->values; n; n = n->next)
	  {
	     data = _eet_data_dump_encode(ed, n, &size);
	     if (data)
	       {
		  echnk = eet_data_chunk_new(data, size, node->name, node->type, node->type);
		  eet_data_chunk_put(ed, echnk, ds);
		  eet_data_chunk_free(echnk);
		  free(data);
	       }
	  }

	/* Hash is somekind of special case, so we should embed it inside another chunk. */
	*size_ret = ds->pos;
	cdata = ds->data;

	ds->data = NULL;
	ds->size = 0;
	eet_data_stream_free(ds);

	return cdata;
      case EET_T_NULL:
	 break;
      case EET_T_CHAR:
        data = eet_data_put_type(ed, node->type, &(node->data.c), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_SHORT:
        data = eet_data_put_type(ed, node->type, &(node->data.s), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_INT:
        data = eet_data_put_type(ed, node->type, &(node->data.i), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_LONG_LONG:
        data = eet_data_put_type(ed, node->type, &(node->data.l), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_FLOAT:
        data = eet_data_put_type(ed, node->type, &(node->data.f), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_DOUBLE:
        data = eet_data_put_type(ed, node->type, &(node->data.d), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_UCHAR:
        data = eet_data_put_type(ed, node->type, &(node->data.uc), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_USHORT:
        data = eet_data_put_type(ed, node->type, &(node->data.us), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_UINT:
        data = eet_data_put_type(ed, node->type, &(node->data.ui), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_ULONG_LONG:
        data = eet_data_put_type(ed, node->type, &(node->data.ul), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_INLINED_STRING:
        data = eet_data_put_type(ed, node->type, &(node->data.str), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      case EET_T_STRING:
        data = eet_data_put_type(ed, node->type, &(node->data.str), &size);
	if (data)
	  {
	     eet_data_stream_write(ds, data, size);
	     free(data);
	  }
	break;
      default:
	break;
     }

   if ((node->type >= EET_G_UNKNOWN) && (node->type < EET_G_LAST))
     chnk = eet_data_chunk_new(ds->data, ds->pos, node->name, EET_T_UNKNOW, node->type);
   else
     chnk = eet_data_chunk_new(ds->data, ds->pos, node->name, node->type, EET_G_UNKNOWN);
   ds->data = NULL;
   ds->size = 0;
   eet_data_stream_free(ds);

   ds = eet_data_stream_new();
   eet_data_chunk_put(ed, chnk, ds);
   cdata = ds->data;
   csize = ds->pos;

   ds->data = NULL;
   ds->size = 0;
   eet_data_stream_free(ds);
   *size_ret = csize;

   free(chnk->data);
   eet_data_chunk_free(chnk);

   return cdata;
}

static void *
_eet_data_dump_parse(Eet_Dictionary *ed,
                     int *size_ret,
		     const char *src,
		     int size)
{
   void *cdata = NULL;
   const char *p;
#define M_NONE 0
#define M_STRUCT 1
#define M_ 2
   int left, jump;
   Eet_Node *node_base = NULL;
   Eet_Node *node = NULL;
   Eet_Node *n, *nn;

   /* FIXME; handle parse errors */
#define TOK_GET(t) \
   jump = left; t = _eet_data_dump_token_get(p, &left); p += jump - left;
   left = size;
   for (p = src; p < (src + size);)
     {
	char *tok1, *tok2, *tok3, *tok4;

	TOK_GET(tok1);
	if (tok1)
	  {
	     if (!strcmp(tok1, "group"))
	       {
		  TOK_GET(tok2);
		  if (tok2)
		    {
		       TOK_GET(tok3);
		       if (tok3)
			 {
			    TOK_GET(tok4);
			    if (tok4)
			      {
				 if (!strcmp(tok4, "{"))
				   {
				      /* we have 'group NAM TYP {' */
				      n = calloc(1, sizeof(Eet_Node));
				      if (n)
					{
					   n->parent = node;
					   if (!node_base)
					     {
						node_base = n;
					     }
					   if (node)
					     {
						/* append node */
						if (!node->values)
						  node->values = n;
						else
						  {
						     for (nn = node->values; nn; nn = nn->next)
						       {
							  if (!nn->next)
							    {
							       nn->next = n;
							       break;
							    }
						       }
						  }
					     }
					   n->name = eina_stringshare_add(tok2);
					   if      (!strcmp(tok3, "struct"))    n->type = EET_G_UNKNOWN;
					   else if (!strcmp(tok3, "array"))     n->type = EET_G_ARRAY;
					   else if (!strcmp(tok3, "var_array")) n->type = EET_G_VAR_ARRAY;
					   else if (!strcmp(tok3, "list"))      n->type = EET_G_LIST;
					   else if (!strcmp(tok3, "hash"))      n->type = EET_G_HASH;
					   else
					     {
						printf("ERROR: group type '%s' invalid.\n", tok3);
					     }
					   node = n;
					}
				   }
				 free(tok4);
			      }
			    free(tok3);
			 }
		       free(tok2);
		    }
	       }
	     else if (!strcmp(tok1, "value"))
	       {
		  TOK_GET(tok2);
		  if (tok2)
		    {
		       TOK_GET(tok3);
		       if (tok3)
			 {
			    TOK_GET(tok4);
			    if (tok4)
			      {
				 /* we have 'value NAME TYP XXX' */
				 if (node_base)
				   {
				      n = calloc(1, sizeof(Eet_Node));
				      if (n)
					{
					   n->parent = node;
					   /* append node */
					   if (!node->values)
					     node->values = n;
					   else
					     {
						for (nn = node->values; nn; nn = nn->next)
						  {
						     if (!nn->next)
						       {
							  nn->next = n;
							  break;
						       }
						  }
					     }
					   n->name = eina_stringshare_add(tok2);
					   if      (!strcmp(tok3, "char:"))
					     {
						n->type = EET_T_CHAR;
						sscanf(tok4, "%hhi", &(n->data.c));
					     }
					   else if (!strcmp(tok3, "short:"))
					     {
						n->type = EET_T_SHORT;
						sscanf(tok4, "%hi", &(n->data.s));
					     }
					   else if (!strcmp(tok3, "int:"))
					     {
						n->type = EET_T_INT;
						sscanf(tok4, "%i", &(n->data.i));
					     }
					   else if (!strcmp(tok3, "long_long:"))
					     {
						n->type = EET_T_LONG_LONG;
						sscanf(tok4, "%lli", &(n->data.l));
					     }
					   else if (!strcmp(tok3, "float:"))
					     {
						n->type = EET_T_FLOAT;
						sscanf(tok4, "%f", &(n->data.f));
					     }
					   else if (!strcmp(tok3, "double:"))
					     {
						n->type = EET_T_DOUBLE;
						sscanf(tok4, "%lf", &(n->data.d));
					     }
					   else if (!strcmp(tok3, "uchar:"))
					     {
						n->type = EET_T_UCHAR;
						sscanf(tok4, "%hhu", &(n->data.uc));
					     }
					   else if (!strcmp(tok3, "ushort:"))
					     {
						n->type = EET_T_USHORT;
						sscanf(tok4, "%hu", &(n->data.us));
					     }
					   else if (!strcmp(tok3, "uint:"))
					     {
						n->type = EET_T_UINT;
						sscanf(tok4, "%u", &(n->data.ui));
					     }
					   else if (!strcmp(tok3, "ulong_long:"))
					     {
						n->type = EET_T_ULONG_LONG;
						sscanf(tok4, "%llu", &(n->data.ul));
					     }
					   else if (!strcmp(tok3, "string:"))
					     {
						n->type = EET_T_STRING;
						n->data.str = eina_stringshare_add(tok4);
					     }
					   else if (!strcmp(tok3, "inlined:"))
					     {
						n->type = EET_T_INLINED_STRING;
						n->data.str = eina_stringshare_add(tok4);
					     }
					   else if (!strcmp(tok3, "null"))
					     {
						n->type = EET_T_NULL;
						n->data.str = NULL;
					     }
					   else
					     {
						printf("ERROR: value type '%s' invalid.\n", tok4);
					     }
					}
				   }
				 free(tok4);
			      }
			    free(tok3);
			 }
		       free(tok2);
		    }
	       }
	     else if (!strcmp(tok1, "key"))
	       {
		  TOK_GET(tok2);
		  if (tok2)
		    {
		       /* we have 'key NAME' */
		       if (node)
			 {
			    node->key = eina_stringshare_add(tok2);
			 }
		       free(tok2);
		    }
	       }
	     else if (!strcmp(tok1, "count"))
	       {
		  TOK_GET(tok2);
		  if (tok2)
		    {
		       /* we have a 'count COUNT' */
		       if (node)
			 {
			    sscanf(tok2, "%i", &(node->count));
			 }
		       free(tok2);
		    }
	       }
	     else if (!strcmp(tok1, "}"))
	       {
		  /* we have an end of the group */
		  if (node) node = node->parent;
	       }
	     free(tok1);
	  }
     }

   if (node_base)
     {
	cdata = _eet_data_dump_encode(ed, node_base, size_ret);
	eet_node_del(node_base);
     }
   return cdata;
}

#define NEXT_CHUNK(P, Size, Echnk, Ed)                  \
  {                                                     \
     int        tmp;                                    \
     tmp = Ed ? (int) (sizeof(int) * 2) : Echnk.len + 4;\
     P += (4 + Echnk.size + tmp);                       \
     Size -= (4 + Echnk.size + tmp);                    \
  }

static const char *_dump_g_name[6] = {
  "struct",
  "array",
  "var_array",
  "list",
  "hash",
  "???"
};

static const char *_dump_t_name[14][2] = {
  { "???: ", "???" },
  { "char: ", "%hhi" },
  { "short: ", "%hi" },
  { "int: ", "%i" },
  { "long_long: ", "%lli" },
  { "float: ", "%1.25f" },
  { "double: ", "%1.25f" },
  { "uchar: ", "%hhu" },
  { "ushort: ", "%i" },
  { "uint: ", "%u" },
  { "ulong_long: ", "%llu" },
  { "null", "" }
};

static void *
_eet_data_descriptor_decode(Eet_Free_Context *context,
			    const Eet_Dictionary *ed,
                            Eet_Data_Descriptor *edd,
			    const void *data_in,
			    int size_in,
			    int level,
			    void (*dumpfunc) (void *data, const char *str),
			    void *dumpdata)
{
   void *data = NULL;
   char *p;
   int size, i, dump;
   int chnk_type;
   Eet_Data_Chunk chnk;

   if (words_bigendian == -1)
     {
	unsigned long int v;

	v = htonl(0x12345678);
	if (v == 0x12345678) words_bigendian = 1;
	else words_bigendian = 0;
     }

   if (edd)
     {
	data = edd->func.mem_alloc(edd->size);
	if (!data) return NULL;
	if (edd->ed != ed)
	  {
	     for (i = 0; i < edd->elements.num; i++)
	       edd->elements.set[i].directory_name_ptr = NULL;
	     edd->ed = ed;
	  }
     }
   _eet_freelist_all_ref(context);
   if (data) _eet_freelist_add(context, data);
   dump = 0;
   memset(&chnk, 0, sizeof(Eet_Data_Chunk));
   eet_data_chunk_get(ed, &chnk, data_in, size_in);
   if (!chnk.name) goto error;
   if (edd)
     {
	if (strcmp(chnk.name, edd->name)) goto error;
     }
   p = chnk.data;
   if (ed)
     size = size_in - (4 + sizeof(int) * 2);
   else
     size = size_in - (4 + 4 + chnk.len);
   if (edd)
     {
	if (!edd->elements.hash.buckets) _eet_descriptor_hash_new(edd);
     }
   else if (dumpfunc)
     {
	dump = 1;
	if (chnk.type == EET_T_UNKNOW)
	  eet_data_dump_group_start(level, dumpfunc, dumpdata, chnk.group_type, chnk.name);
     }
   while (size > 0)
     {
	Eet_Data_Chunk echnk;
	Eet_Data_Element *ede = NULL;
	unsigned char dd[128];
	int group_type = EET_G_UNKNOWN, type = EET_T_UNKNOW;
	int ret = 0;

	/* get next data chunk */
	memset(&echnk, 0, sizeof(Eet_Data_Chunk));
	eet_data_chunk_get(ed, &echnk, p, size);
	if (!echnk.name) goto error;
	/* FIXME: don't REPLY on edd - work without */
	if ((edd) && (!dumpfunc))
	  {
	     ede = _eet_descriptor_hash_find(edd, echnk.name, echnk.hash);
	     if (ede)
	       {
		  group_type = ede->group_type;
		  type = ede->type;
		  if ((echnk.type == 0) && (echnk.group_type == 0))
		    {
		       type = ede->type;
		       group_type = ede->group_type;
		    }
		  else
		    {
		       if (IS_SIMPLE_TYPE(echnk.type) &&
			   (echnk.type == ede->type))
			 type = echnk.type;
		       else if ((echnk.group_type > EET_G_UNKNOWN) &&
				(echnk.group_type < EET_G_LAST) &&
				(echnk.group_type == ede->group_type))
			 group_type = echnk.group_type;
		    }
	       }
	  }
	/*...... dump func */
	else if (dumpfunc)
	  {
	     if ((echnk.type > EET_T_UNKNOW) &&
		 (echnk.type < EET_T_LAST))
	       type = echnk.type;
	     else if ((echnk.group_type > EET_G_UNKNOWN) &&
		      (echnk.group_type < EET_G_LAST))
	       group_type = echnk.group_type;
	  }

	if (dumpfunc && group_type == EET_G_UNKNOWN && IS_SIMPLE_TYPE(type))
	  {
	     ret = eet_data_get_type(ed,
				     type,
				     echnk.data,
				     ((char *)echnk.data) + echnk.size,
				     dd);
	     if (ret <= 0) goto error;

	     eet_data_dump_simple_type(type, echnk.name, dd, level, dumpfunc, dumpdata);
	  }
	else
	  {
	     ret = eet_group_codec[group_type - 100].get(context,
							 ed, edd, ede, &echnk,
							 type, group_type, ede ? ((char *)data) + ede->offset : dd,
							 level, dumpfunc, dumpdata,
							 &p, &size);
	     if (ret <= 0) goto error;
	  }
	/* advance to next chunk */
        NEXT_CHUNK(p, size, echnk, ed);
     }
   _eet_freelist_all_unref(context);
   if (dumpfunc)
     {
	_eet_freelist_str_free(context, edd);
	_eet_freelist_direct_str_free(context, edd);
	_eet_freelist_list_free(context, edd);
	_eet_freelist_hash_free(context, edd);
	_eet_freelist_free(context, edd);
	_eet_freeleak_reset(context);
     }
   else
     {
	_eet_freelist_reset(context);
	_eet_freeleak_free(context, edd);
	_eet_freelist_str_reset(context);
	_eet_freelist_list_reset(context);
	_eet_freelist_hash_reset(context);
	_eet_freelist_direct_str_reset(context);
     }
   if (dumpfunc)
     {
	if (dump)
	  {
	     if (chnk.type == EET_T_UNKNOW)
	       {
		  for (i = 0; i < level; i++) dumpfunc(dumpdata, "  ");
		  dumpfunc(dumpdata, "}\n");
	       }
	  }
	return (void *)1;
     }
   return data;

error:
   _eet_freelist_all_unref(context);
   _eet_freelist_str_free(context, edd);
   _eet_freelist_direct_str_free(context, edd);
   _eet_freelist_list_free(context, edd);
   _eet_freelist_hash_free(context, edd);
   _eet_freelist_free(context, edd);
   _eet_freeleak_reset(context);
   if (dumpfunc)
     {
	if (dump)
	  {
	     if (chnk.type == EET_T_UNKNOW)
	       {
		  for (i = 0; i < level; i++) dumpfunc(dumpdata, "  ");
		  dumpfunc(dumpdata, "}\n");
	       }
	  }
     }
   return NULL;
}

static void
eet_data_dump_level(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata)
{
   int i;

   for (i = 0; i < level; i++) dumpfunc(dumpdata, "  ");
}

static void
eet_data_dump_group_start(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata,
			  int group_type, const char *name)
{
   int chnk_type;

   chnk_type = (group_type >= EET_G_UNKNOWN && group_type <= EET_G_HASH) ?
     group_type : EET_G_LAST;

   eet_data_dump_level(level, dumpfunc, dumpdata);
   dumpfunc(dumpdata, "group \"");
   _eet_data_dump_string_escape(dumpdata, dumpfunc, name);
   dumpfunc(dumpdata, "\" ");

   dumpfunc(dumpdata, _dump_g_name[chnk_type - EET_G_UNKNOWN]);
   dumpfunc(dumpdata, " {\n");
}

static void
eet_data_dump_group_end(int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata)
{
   eet_data_dump_level(level, dumpfunc, dumpdata);
   dumpfunc(dumpdata, "  }\n");
}

static int
eet_data_get_list(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk,
		  int type, int group_type __UNUSED__, void *data,
		  int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata,
		  char **p, int *size)
{
   Eet_Data_Descriptor *subtype = NULL;
   void *list = NULL;
   void **ptr;
   void *data_ret;
   int et = EET_T_UNKNOW;

   EET_ASSERT(!((type > EET_T_UNKNOW) && (type < EET_T_STRING)), return 0);

   if (edd)
     {
	subtype = ede->subtype;
	et = ede->type;
     }
   else if (dumpfunc)
     {
	eet_data_dump_group_start(level + 1, dumpfunc, dumpdata, echnk->group_type, echnk->name);
     }

   ptr = (void **)data;
   list = *ptr;
   data_ret = NULL;

   if (et >= EET_T_STRING)
     {
	int ret;

	ret = eet_data_get_unknown(context, ed, edd, ede, echnk, et, EET_G_UNKNOWN,
				   &data_ret, level, dumpfunc, dumpdata, p, size);
	if (!ret) return 0;
     }
   else
     {
	data_ret = _eet_data_descriptor_decode(context, ed, subtype,
					       echnk->data, echnk->size,
					       level + 2, dumpfunc, dumpdata);
	if (!data_ret) return 0;
     }

   if (edd)
     {
	list = edd->func.list_append(list, data_ret);
	*ptr = list;
	_eet_freelist_list_add(context, ptr);
     }
   else if (dumpfunc)
     eet_data_dump_group_end(level, dumpfunc, dumpdata);

   return 1;
}

static int
eet_data_get_hash(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk,
		  int type, int group_type __UNUSED__, void *data,
		  int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata,
		  char **p, int *size)
{
   void **ptr;
   void *hash = NULL;
   char *key = NULL;
   void *data_ret = NULL;
   int ret = 0;

   EET_ASSERT(!((type > EET_T_UNKNOW) && (type < EET_T_STRING)), return 0);

   ptr = (void **)data;
   hash = *ptr;

   /* Read key */
   ret = eet_data_get_type(ed,
			   EET_T_STRING,
			   echnk->data,
			   ((char *)echnk->data) + echnk->size,
			   &key);
   if (ret <= 0) goto on_error;

   /* Advance to next chunk */
   NEXT_CHUNK((*p), (*size), (*echnk), ed);
   memset(echnk, 0, sizeof(Eet_Data_Chunk));

   /* Read value */
   eet_data_chunk_get(ed, echnk, *p, *size);
   if (!echnk->name) goto on_error;

   if (dumpfunc && key)
     {
	eet_data_dump_group_start(level + 1, dumpfunc, dumpdata, echnk->group_type, echnk->name);

	eet_data_dump_level(level, dumpfunc, dumpdata);
	dumpfunc(dumpdata, "    key \"");
	_eet_data_dump_string_escape(dumpdata, dumpfunc, key);
	dumpfunc(dumpdata, "\";\n");
     }

   if (type >= EET_T_STRING)
     {
	int ret;

	ret = eet_data_get_unknown(context, ed, edd, ede, echnk, ede ? ede->type : type, EET_G_UNKNOWN,
				   &data_ret, level, dumpfunc, dumpdata, p, size);
	if (!ret) return 0;
     }
   else
     {
	data_ret = _eet_data_descriptor_decode(context,
					       ed,
					       ede ? ede->subtype : NULL,
					       echnk->data,
					       echnk->size,
					       level + 2,
					       dumpfunc,
					       dumpdata);
	if (!data_ret) goto on_error;
     }

   if (edd)
     {
	hash = edd->func.hash_add(hash, key, data_ret);
	*ptr = hash;
	_eet_freelist_hash_add(context, ptr);
     }
   else if (dumpfunc)
     eet_data_dump_group_end(level, dumpfunc, dumpdata);

   return 1;

 on_error:
   return ret;
}

/* var arrays and fixed arrays have to
 * get all chunks at once. for fixed arrays
 * we can get each chunk and increment a
 * counter stored on the element itself but
 * it wont be thread safe. for var arrays
 * we still need a way to get the number of
 * elements from the data, so storing the
 * number of elements and the element data on
 * each chunk is pointless.
 */
static int
eet_data_get_array(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd __UNUSED__,
		   Eet_Data_Element *ede, Eet_Data_Chunk *echnk,
		   int type, int group_type, void *data,
		   int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata,
		   char **p, int *size)
{
   const char *name;
   void *ptr;
   int count;
   int ret;
   int subsize = 0;
   int i;

   EET_ASSERT(!((type > EET_T_UNKNOW) && (type < EET_T_STRING)), return 0);

   ptr = data;
   /* read the number of elements */
   ret = eet_data_get_type(ed,
			   EET_T_INT,
			   echnk->data,
			   ((char *)echnk->data) + echnk->size,
			   &count);
   if (ret <= 0) return ret;

   name = echnk->name;

   if (ede)
     {
	if (type >= EET_T_STRING)
	  subsize = eet_basic_codec[ede->type].size;
	else
	  subsize = ede->subtype->size;

	if (group_type == EET_G_VAR_ARRAY)
	  {
	     /* store the number of elements
	      * on the counter offset */
	     *(int *)(((char *)data) + ede->count - ede->offset) = count;
	     /* allocate space for the array of elements */
	     *(void **)ptr = edd->func.mem_alloc(count * subsize);

	     if (!*(void **)ptr) return 0;

	     memset(*(void **)ptr, 0, count * subsize);

	     _eet_freelist_add(context, *(void **)ptr);
	  }
     }
   else
     {
	char tbuf[256];

	eet_data_dump_group_start(level + 1, dumpfunc, dumpdata, echnk->group_type, echnk->name);

	eet_data_dump_level(level, dumpfunc, dumpdata);
	dumpfunc(dumpdata, "    count ");
	eina_convert_itoa(count, tbuf);
	dumpfunc(dumpdata, tbuf);
	dumpfunc(dumpdata, ";\n");
     }

   /* get all array elements */
   for (i = 0; i < count; i++)
     {
	void *dst = NULL;
	void *data_ret = NULL;

	/* Advance to next chunk */
	NEXT_CHUNK((*p), (*size), (*echnk), ed);
	memset(echnk, 0, sizeof(Eet_Data_Chunk));

	eet_data_chunk_get(ed, echnk, *p, *size);
	if (!echnk->name || strcmp(echnk->name, name) != 0) return 0;
	/* get the data */

	/* get the destination pointer */
	if (ede)
	  {
	     if (group_type == EET_G_ARRAY)
	       dst = (char *)ptr + (subsize * i);
	     else
	       dst = *(char **)ptr + (subsize * i);
	  }

	if (type >= EET_T_STRING)
	  {
	     int ret;

	     ret = eet_data_get_unknown(context, ed, edd, ede, echnk, ede ? ede->type : type, EET_G_UNKNOWN,
					&data_ret, level, dumpfunc, dumpdata, p, size);
	     if (!ret) return 0;
	     if (dst) memcpy(dst, &data_ret, subsize);
	  }
	else
	  {
	     data_ret = _eet_data_descriptor_decode(context, ed, ede ? ede->subtype : NULL,
						    echnk->data, echnk->size,
						    level + 2, dumpfunc, dumpdata);
	     if (!data_ret) return 0;
	     if (dst)
	       {
		  memcpy(dst, data_ret, subsize);
		  _eet_freelist_add(context, data_ret);
	       }
	  }
     }

   if (dumpfunc)
     eet_data_dump_group_end(level, dumpfunc, dumpdata);

   return 1;
}

static void
eet_data_dump_simple_type(int type, const char *name, void *dd,
			 int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata)
{
   const char *type_name = NULL;
   char tbuf[256];

   eet_data_dump_level(level, dumpfunc, dumpdata);
   dumpfunc(dumpdata, "  value \"");
   _eet_data_dump_string_escape(dumpdata, dumpfunc, name);
   dumpfunc(dumpdata, "\" ");

#define EET_T_TYPE(Eet_Type, Type)					\
   case Eet_Type:							\
     {									\
	dumpfunc(dumpdata, _dump_t_name[Eet_Type][0]);			\
	snprintf(tbuf, sizeof (tbuf), _dump_t_name[Eet_Type][1], *((Type *)dd)); \
	dumpfunc(dumpdata, tbuf);					\
	break;								\
     }

   switch (type)
     {
	EET_T_TYPE(EET_T_CHAR, char);
	EET_T_TYPE(EET_T_SHORT, short);
	EET_T_TYPE(EET_T_INT, int);
	EET_T_TYPE(EET_T_LONG_LONG, long long);
	EET_T_TYPE(EET_T_FLOAT, float);
	EET_T_TYPE(EET_T_DOUBLE, double);
	EET_T_TYPE(EET_T_UCHAR, unsigned char);
	EET_T_TYPE(EET_T_USHORT, unsigned short);
	EET_T_TYPE(EET_T_UINT, unsigned int);
	EET_T_TYPE(EET_T_ULONG_LONG, unsigned long long);
      case EET_T_INLINED_STRING:
	 type_name = "inlined: \"";
      case EET_T_STRING:
	 if (!type_name) type_name = "string: \"";

	 {
	    char *s;

	    s = *((char **)dd);
	    if (s)
	      {
		 dumpfunc(dumpdata, type_name);
		 _eet_data_dump_string_escape(dumpdata, dumpfunc, s);
		 dumpfunc(dumpdata, "\"");
	      }
	 }
	 break;
      case EET_T_NULL:
	 dumpfunc(dumpdata, "null");
	 break;
      default:
	 dumpfunc(dumpdata, "???: ???"); break;
	 break;
     }
   dumpfunc(dumpdata, ";\n");
}

static int
eet_data_get_unknown(Eet_Free_Context *context, const Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Chunk *echnk,
		     int type, int group_type __UNUSED__, void *data,
		     int level, void (*dumpfunc) (void *data, const char *str), void *dumpdata,
		     char **p __UNUSED__, int *size __UNUSED__)
{
   int ret;
   void *data_ret;

   if (IS_SIMPLE_TYPE(type))
     {
	ret = eet_data_get_type(ed, type, echnk->data, ((char *)echnk->data) + echnk->size, ((char *)data));
	if (ret <= 0) return ret;

	if (!edd && dumpfunc)
	  {
	     eet_data_dump_simple_type(type, echnk->name, data, level, dumpfunc, dumpdata);
	  }
	else if (type == EET_T_STRING)
	  {
	     char **str;

	     str = (char **)(((char *)data));
	     if (*str)
	       {
		  if ((ed == NULL) || (edd->func.str_direct_alloc == NULL))
		    {
		       *str = edd->func.str_alloc(*str);
		       _eet_freelist_str_add(context, *str);
		    }
		  else
		    {
		       *str = edd->func.str_direct_alloc(*str);
		       _eet_freelist_direct_str_add(context, *str);
		    }
	       }
	  }
	else if (type == EET_T_INLINED_STRING)
	  {
	     char **str;

	     str = (char **)(((char *)data));
	     if (*str)
	       {
		  *str = edd->func.str_alloc(*str);
		  _eet_freelist_str_add(context, *str);
	       }
	  }
     }
   else
     {
	Eet_Data_Descriptor *subtype;

	subtype = ede ? ede->subtype : NULL;

	if (subtype || dumpfunc)
	  {
	     void **ptr;

	     data_ret = _eet_data_descriptor_decode(context, ed, subtype, echnk->data, echnk->size, level + 1, dumpfunc, dumpdata);
	     if (!data_ret) return 0;

	     ptr = (void **)(((char *)data));
	     *ptr = (void *)data_ret;
	  }
     }

   return 1;
}

static void
eet_data_encode(Eet_Dictionary *ed, Eet_Data_Stream *ds, void *data, const char *name, int size, int type, int group_type)
{
   Eet_Data_Chunk *echnk;

   echnk = eet_data_chunk_new(data, size, name, type, group_type);
   eet_data_chunk_put(ed, echnk, ds);
   eet_data_chunk_free(echnk);
   if (data) free(data);
}

static void
eet_data_put_array(Eet_Dictionary *ed, Eet_Data_Descriptor *edd __UNUSED__, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in)
{
   void *data;
   int offset = 0;
   int subsize;
   int count;
   int size;
   int j;

   EET_ASSERT(!((ede->type > EET_T_UNKNOW) && (ede->type < EET_T_STRING)), return );

   if (ede->group_type == EET_G_ARRAY)
     count = ede->counter_offset;
   else
     count = *(int *)(((char *)data_in) + ede->count - ede->offset);

   if (count <= 0) return;
   /* Store number of elements */
   data = eet_data_put_type(ed, EET_T_INT, &count, &size);
   if (data) eet_data_encode(ed, ds, data, ede->name, size, ede->type, ede->group_type);

   if (ede->type >= EET_T_STRING)
     subsize = eet_basic_codec[ede->type].size;
   else
     subsize = ede->subtype->size;

   for (j = 0; j < count; j++)
     {
	void *d;
	int pos = ds->pos;

	if (ede->group_type == EET_G_ARRAY)
	  d = (void *)(((char *)data_in) + offset);
	else
	  d = *(((char **)data_in)) + offset;

	if (ede->type >= EET_T_STRING)
	  eet_data_put_unknown(ed, NULL, ede, ds, d);
	else
	  {
	     data = _eet_data_descriptor_encode(ed, ede->subtype, d, &size);
	     if (data) eet_data_encode(ed, ds, data, ede->name, size, ede->type, ede->group_type);
	  }

	if (pos == ds->pos)
	  {
	     /* Add a NULL element just to have the correct array layout. */
	     eet_data_encode(ed, ds, NULL, ede->name, 0, EET_T_NULL, ede->group_type);
	  }

	offset += subsize;
     }
}

static void
eet_data_put_unknown(Eet_Dictionary *ed, Eet_Data_Descriptor *edd __UNUSED__, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in)
{
   void *data = NULL;
   int size;

   if (IS_SIMPLE_TYPE(ede->type))
     data = eet_data_put_type(ed, ede->type, data_in, &size);
   else if (ede->subtype)
     {
	if (*((char **)data_in))
	  data = _eet_data_descriptor_encode(ed,
					     ede->subtype,
					     *((char **)((char *)(data_in))),
					     &size);
     }
   if (data) eet_data_encode(ed, ds, data, ede->name, size, ede->type, ede->group_type);
}

static void
eet_data_put_list(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in)
{
   void *data;
   void *l;
   int size;

   EET_ASSERT(!((ede->type > EET_T_UNKNOW) && (ede->type < EET_T_STRING)), return );

   l = *((void **)(((char *)data_in)));
   for (; l; l = edd->func.list_next(l))
     {
	if (ede->type >= EET_T_STRING)
	  {
	     const char *str = edd->func.list_data(l);
	     eet_data_put_unknown(ed, NULL, ede, ds, &str);
	  }
	else
	  {
	     data = _eet_data_descriptor_encode(ed,
						ede->subtype,
						edd->func.list_data(l),
						&size);
	     if (data) eet_data_encode(ed, ds, data, ede->name, size, ede->type, ede->group_type);
	  }
     }
}

static void
eet_data_put_hash(Eet_Dictionary *ed, Eet_Data_Descriptor *edd, Eet_Data_Element *ede, Eet_Data_Stream *ds, void *data_in)
{
   Eet_Data_Encode_Hash_Info fdata;
   void *l;

   l = *((void **)(((char *)data_in)));
   fdata.ds = ds;
   fdata.ede = ede;
   fdata.ed = ed;
   edd->func.hash_foreach(l, eet_data_descriptor_encode_hash_cb, &fdata);
}

EAPI int
eet_data_dump_cipher(Eet_File *ef,
		     const char *name, const char *key,
		     void (*dumpfunc) (void *data, const char *str),
		     void *dumpdata)
{
   const Eet_Dictionary *ed = NULL;
   const void		*data = NULL;
   Eet_Free_Context      context;
   int			 ret = 0;
   int			 required_free = 0;
   int			 size;

   ed = eet_dictionary_get(ef);

   if (!key)
     data = eet_read_direct(ef, name, &size);
   if (!data)
     {
	required_free = 1;
	data = eet_read_cipher(ef, name, &size, key);
	if (!data) return 0;
     }

   memset(&context, 0, sizeof (context));
   if (_eet_data_descriptor_decode(&context, ed, NULL, data, size, 0,
				   dumpfunc, dumpdata))
     ret = 1;

   if (required_free)
     free((void*)data);

   return ret;
}

EAPI int
eet_data_dump(Eet_File *ef,
	      const char *name,
	      void (*dumpfunc) (void *data, const char *str),
	      void *dumpdata)
{
   return eet_data_dump_cipher(ef, name, NULL, dumpfunc, dumpdata);
}


EAPI int
eet_data_text_dump_cipher(const void *data_in,
			  const char *key, int size_in,
			  void (*dumpfunc) (void *data, const char *str),
			  void *dumpdata)
{
   void *ret = NULL;
   Eet_Free_Context context;
   unsigned int ret_len = 0;

   if (data_in && key)
     {
       if (eet_decipher(data_in, size_in, key, strlen(key), &ret, &ret_len))
	 {
	   if (ret) free(ret);
	   return 1;
	 }
       memset(&context, 0, sizeof (context));
       if (_eet_data_descriptor_decode(&context, NULL, NULL, ret, ret_len, 0,
				       dumpfunc, dumpdata))
	 {
	   free(ret);
	   return 1;
	 }
       free(ret);
       return 0;
     }
   memset(&context, 0, sizeof (context));
   if (_eet_data_descriptor_decode(&context, NULL, NULL, data_in, size_in, 0,
				   dumpfunc, dumpdata))
     return 1;
   return 0;
}

EAPI int
eet_data_text_dump(const void *data_in,
		   int size_in,
		   void (*dumpfunc) (void *data, const char *str),
		   void *dumpdata)
{
   return eet_data_text_dump_cipher(data_in, NULL, size_in, dumpfunc, dumpdata);
}

EAPI void *
eet_data_text_undump_cipher(const char *text,
			    const char *key,
			    int textlen,
			    int *size_ret)
{
   void *ret = NULL;

   ret = _eet_data_dump_parse(NULL, size_ret, text, textlen);
   if (ret && key)
     {
	void *ciphered = NULL;
	unsigned int ciphered_len;

	if (eet_cipher(ret, *size_ret, key, strlen(key), &ciphered, &ciphered_len))
	  {
	     if (ciphered) free(ciphered);
	     size_ret = 0;
	     free(ret);
	     return NULL;
	  }
	free(ret);
	*size_ret = ciphered_len;
	ret = ciphered;
     }
   return ret;
}

EAPI void *
eet_data_text_undump(const char *text,
		     int textlen,
		     int *size_ret)
{
   return eet_data_text_undump_cipher(text, NULL, textlen, size_ret);
}

EAPI int
eet_data_undump_cipher(Eet_File *ef,
		       const char *name,
		       const char *key,
		       const char *text,
		       int textlen,
		       int compress)
{
   Eet_Dictionary       *ed;
   void                 *data_enc;
   int                   size;
   int                   val;

   ed = eet_dictionary_get(ef);

   data_enc = _eet_data_dump_parse(ed, &size, text, textlen);
   if (!data_enc) return 0;
   val = eet_write_cipher(ef, name, data_enc, size, compress, key);
   free(data_enc);
   return val;
}

EAPI int
eet_data_undump(Eet_File *ef,
		const char *name,
		const char *text,
		int textlen,
		int compress)
{
   return eet_data_undump_cipher(ef, name, NULL, text, textlen, compress);
}

EAPI void *
eet_data_descriptor_decode_cipher(Eet_Data_Descriptor *edd,
				  const void *data_in,
				  const char *key,
				  int size_in)
{
   void *deciphered = NULL;
   void *ret;
   Eet_Free_Context context;
   unsigned int deciphered_len = 0;

   if (key && data_in)
     {
       if (eet_decipher(data_in, size_in, key, strlen(key), &deciphered, &deciphered_len))
	 {
	   if (deciphered) free(deciphered);
	   return NULL;
	 }
       memset(&context, 0, sizeof (context));
       ret = _eet_data_descriptor_decode(&context, NULL, edd, deciphered, deciphered_len, 0,
					 NULL, NULL);
       free(deciphered);
       return ret;
     }
   memset(&context, 0, sizeof (context));
   return _eet_data_descriptor_decode(&context, NULL, edd, data_in, size_in, 0,
                                      NULL, NULL);
}

EAPI void *
eet_data_descriptor_decode(Eet_Data_Descriptor *edd,
			   const void *data_in,
			   int size_in)
{
   return eet_data_descriptor_decode_cipher(edd, data_in, NULL, size_in);
}

static void *
_eet_data_descriptor_encode(Eet_Dictionary *ed,
                            Eet_Data_Descriptor *edd,
                            const void *data_in,
                            int *size_ret)
{
   Eet_Data_Stream      *ds;
   Eet_Data_Chunk       *chnk;
   void                 *cdata;
   int                   csize;
   int                   i;

   if (words_bigendian == -1)
     {
	unsigned long int v;

	v = htonl(0x12345678);
	if (v == 0x12345678) words_bigendian = 1;
	else words_bigendian = 0;
     }

   ds = eet_data_stream_new();
   for (i = 0; i < edd->elements.num; i++)
     {
	Eet_Data_Element *ede;

	ede = &(edd->elements.set[i]);
	eet_group_codec[ede->group_type - 100].put(ed, edd, ede, ds, ((char *)data_in) + ede->offset);
     }
   chnk = eet_data_chunk_new(ds->data, ds->pos, edd->name, EET_T_UNKNOW, EET_G_UNKNOWN);
   ds->data = NULL;
   ds->size = 0;
   eet_data_stream_free(ds);

   ds = eet_data_stream_new();
   eet_data_chunk_put(ed, chnk, ds);
   cdata = ds->data;
   csize = ds->pos;

   ds->data = NULL;
   ds->size = 0;
   eet_data_stream_free(ds);
   *size_ret = csize;

   free(chnk->data);
   eet_data_chunk_free(chnk);

   return cdata;
}

EAPI int
eet_data_node_write_cipher(Eet_File *ef, const char *name, const char *key, Eet_Node *node, int compress)
{
   Eet_Dictionary       *ed;
   void                 *data_enc;
   int                   size;
   int                   val;

   ed = eet_dictionary_get(ef);

   data_enc = _eet_data_dump_encode(ed, node, &size);
   if (!data_enc) return 0;
   val = eet_write_cipher(ef, name, data_enc, size, compress, key);
   free(data_enc);
   return val;
}

EAPI void *
eet_data_node_encode_cipher(Eet_Node *node,
			    const char *key,
			    int *size_ret)
{
   void *ret = NULL;
   void *ciphered = NULL;
   unsigned int ciphered_len = 0;
   int size;

   ret = _eet_data_dump_encode(NULL, node, &size);
   if (key && ret)
     {
	if (eet_cipher(ret, size, key, strlen(key), &ciphered, &ciphered_len))
	  {
	     if (ciphered) free(ciphered);
	     if (size_ret) *size_ret = 0;
	     free(ret);
	     return NULL;
	  }
	free(ret);
	size = (int) ciphered_len;
	ret = ciphered;
     }

   if (size_ret) *size_ret = size;
   return ret;
}

EAPI void *
eet_data_descriptor_encode_cipher(Eet_Data_Descriptor *edd,
				  const void *data_in,
				  const char *key,
				  int *size_ret)
{
   void *ret = NULL;
   void *ciphered = NULL;
   unsigned int ciphered_len = 0;
   int size;

   ret = _eet_data_descriptor_encode(NULL, edd, data_in, &size);
   if (key && ret)
     {
       if (eet_cipher(ret, size, key, strlen(key), &ciphered, &ciphered_len))
	 {
	   if (ciphered) free(ciphered);
	   if (size_ret) *size_ret = 0;
	   free(ret);
	   return NULL;
	 }
       free(ret);
       size = ciphered_len;
       ret = ciphered;
     }

   if (size_ret) *size_ret = size;
   return ret;
}

EAPI void *
eet_data_descriptor_encode(Eet_Data_Descriptor *edd,
			   const void *data_in,
			   int *size_ret)
{
   return eet_data_descriptor_encode_cipher(edd, data_in, NULL, size_ret);
}
