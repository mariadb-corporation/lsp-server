/* src/lexshim.h -- the fake THD / LEX / SELECT_LEX surface the sliced lexer
 * compiles against.
 *
 * SEEDED FROM docs/thd.md, WHICH IS THE LEDGER OF SERVER STATE THE
 * LEXER READS.  One field per row of that document, each carrying the
 * sql_lex.cc line the ledger cites as a comment.  Nothing here is invented:
 * a `thd.md` tag on a field says the ledger asked for it; an untagged
 * declaration is a name the sliced code needs to compile or link beyond the
 * ledger, with the sql_lex.cc line that uses it; and `[with X]` says the
 * slice names X, not this, and this is what X's declaration needs.
 *
 * WHAT THIS IS NOT.  It is not a lexer.  Everything that IS MariaDB source is
 * sliced by tools/lexslice.py into generated/lexer_extracted.cc; what lives
 * here is the fake THD/LEX/SELECT_LEX surface that slice compiles against,
 * plus MariaDB's spellings for the C scaffolding its own sources use.
 *
 * Every function declared below has a body in src/mdbp_lex.cc, carrying the
 * docs/thd.md row or the sql_lex.cc behaviour that justifies it and
 * saying whether it is faithful or a deliberate no-op.  Nothing that is
 * lexing is shimmed: `init`, `reset`, `get_text` and `unescape` are part of
 * the slice.
 *
 * docs/thd.md is maintained together with this file: a change to what the
 * shim models updates both.
 */
#ifndef MDBP_LEXSHIM_H
#define MDBP_LEXSHIM_H

/* src/stub.h -- the extracted parser's runtime contract -- declares
 * `enum enum_parsing_place` and `struct SELECT_LEX`, which used to be
 * declared HERE.  They moved so that there is exactly one of each: see THE
 * JOIN below.  It pulls in include/mdbparse.h (which this file already saw
 * through src/mdbp_lex.h) and nothing else of ours; it must never pull in
 * bison's parser.h, and it does not. */
#include "stub.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* As MariaDB's my_compiler.h: GCC attributes (the extracted lexer has
   __attribute__((unused))) vanish on other compilers. */
#if !defined(__GNUC__) && !defined(__clang__)
#define __attribute__(A)
#endif

/* --- primitives ---------------------------------------------------------- */
/* Scaffolding, not ledger rows: MariaDB's spellings for the C types its own
 * sources use.  Named here so a diagnostic about them is never mistaken for a
 * server-state dependency. */
typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;
typedef char my_bool;
typedef uint32_t my_wc_t;
typedef unsigned int my_repertoire_t;
typedef unsigned long myf;                       /* sql_lex.cc:2987 */
/* MariaDB's spellings for the widths strings/my_strtoll10.c is written
 * in.  Scaffolding, not ledger rows -- the FUNCTION is sliced. */
typedef long long longlong;
typedef unsigned long long ulonglong;
typedef unsigned short uint16;
typedef short int16;
typedef unsigned int uint32;

/* The comment token's value, read at sql_lex.cc:2565-2575 (HINT_COMMENT).
 * thd.md's `lip->hint_comment` row says the branch is never taken; the type
 * still has to exist for the slice to compile. [with YYSTYPE::lex_comment] */
struct Lex_comment_st
{
  char *str;
  size_t length;
  uint lineno;
};

/* include/my_sys.h's element-count macro, used by
 * find_keyword_qualified_special_func (sql_lex.cc:2775). */
#define array_elements(A) ((size_t) (sizeof(A) / sizeof(A[0])))

/* strings/ library entry point, read at sql_lex.cc:2474 for the version
 * digits of a versioned comment.  The definition is sliced
 * (strings/my_strtoll10.c, SLICE_SUPPORT), so this is only the forward
 * declaration that keeps the ordering honest. */
extern longlong my_strtoll10(const char *nptr, char **endptr, int *error);

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

struct st_mysql_lex_string { char *str; size_t length; };
typedef struct st_mysql_const_lex_string { const char *str; size_t length; }
  LEX_CSTRING;

/* --- character set ------------------------------------------------------- */
/* thd.md row `thd->charset()` (sql_lex.cc:2038-2040, 1643, 2913, 2877, 3026):
 * "utf8mb4, always".  The lexer reads exactly three things off CHARSET_INFO --
 * the two 256-byte maps and the character length -- so those three are what
 * the shim carries.  The two maps come from generated/state_maps.h, computed
 * by MariaDB's own init_state_maps() over the sliced utf8mb4 ctype table (see
 * libs/mariadbparser/UPSTREAM.md). */
struct MY_COLLATION_HANDLER
{
  /* find_keyword_qualified_special_func (sql_lex.cc:2782) is the one caller
   * in the slice. [with CHARSET_INFO::coll] */
  int (*strnncollsp)(struct charset_info_st *, const uchar *, size_t,
                     const uchar *, size_t);
};

struct charset_info_st
{
  const uchar *ctype;
  const uchar *state_map;
  const uchar *ident_map;
  uint mbmaxlen;
  uint number;
  /* const, so an instance built entirely from constant
   * data (src/mdbp_lex.cc's mdbp_coll) can itself be const and move out of
   * writable memory.  The one sliced reader, sql_lex.cc:2782's
   * `cs->coll->strnncollsp(cs, ...)`, only ever reads the field and calls
   * through it. */
  const MY_COLLATION_HANDLER *coll;              /* sql_lex.cc:2782 */
  bool use_mb() const;                           /* sql_lex.cc:2875, 2918 */
  int charlen(const char *s, const char *e) const; /* sql_lex.cc:2880 */
};
typedef struct charset_info_st CHARSET_INFO;

/* include/m_ctype.h's `my_ismbchar(cs, a, b)` -- "the length of the multi-byte
 * character at `a`, or 0".  A macro there too; here it forwards to the sliced
 * my_charlen_utf8mb4 through CHARSET_INFO::charlen (src/mdbp_lex.cc).  Read by
 * get_text() (sql_lex.cc:1656) and my_unescape() (sql_lex.cc:1577). */
static inline int mdbp_ismbchar(const CHARSET_INFO *cs, const char *s,
                                const char *e)
{
  int n= cs->charlen(s, e);
  return n > 1 ? n : 0;
}
#define my_ismbchar(cs, a, b) mdbp_ismbchar((cs), (a), (b))
/* include/my_global.h: multi-byte support is compiled in.  get_text() and
 * my_unescape() both guard their charlen() calls with it. */
#define USE_MB 1
/* const: nothing ever writes it, so it can live in read-only memory --
 * ErrConvString's constructor below takes `const CHARSET_INFO *` to match, so
 * the sliced call `ErrConvString csname(str->str + 1, str->length - 1,
 * &my_charset_bin);` (sql_lex.cc:2986) needs no cast. */
extern const CHARSET_INFO my_charset_bin;        /* sql_lex.cc:2986
                                                  * [with ErrConvString] */
/* thread_local: the pointer is per parse -- mdbp_lex_new()/mdbp_lex_next()
 * set it to the lexer's charset around the sliced calls that read it -- and
 * the sliced lexer reaches it as a global.  Every declaration of a
 * thread_local entity in one translation unit must repeat the specifier, so
 * this extern and src/mdbp_lex.cc's definition agree. */
extern thread_local CHARSET_INFO *national_charset_info; /* sql_lex.cc:2158 */
#define MY_CS_PRIMARY 32                         /* sql_lex.cc:2993 */
extern CHARSET_INFO *get_charset_by_csname(const char *name, uint flags,
                                           unsigned long my_flags);
                                                 /* sql_lex.cc:2992 */

/* sql/sql_error.h's error-message string wrapper: scan_ident_middle
 * (sql_lex.cc:2986) builds one to name an unknown character set. */
class ErrConvString
{
  /* sql/sql_error.h sizes ErrConv's buffer at MYSQL_ERRMSG_SIZE; the one call
   * site builds one on the stack, so the buffer has to be here rather than in
   * src/mdbp_lex.cc.  512 is MYSQL_ERRMSG_SIZE in the pinned tree. */
  char m_buf[512];
public:
  ErrConvString(const char *str, size_t length, const CHARSET_INFO *cs);
  const char *ptr() const;
};

/* my_ctype.h's classifiers, as the lexer uses them: a lookup in cs->ctype,
 * which is offset by one (index 0 is the "EOF" slot).  Values from
 * include/m_ctype.h's _MY_* bits. */
#define _MY_U   01
#define _MY_L   02
#define _MY_NMR 04
#define _MY_SPC 010
#define _MY_PNT 020
#define _MY_CTR 040
#define _MY_B   0100
#define _MY_X   0200

#define my_ctype_bits(cs, ch) ((cs)->ctype[(uchar) (ch) + 1])
#define my_isalpha(cs, ch)  (my_ctype_bits(cs, ch) & (_MY_U | _MY_L))
#define my_isupper(cs, ch)  (my_ctype_bits(cs, ch) & _MY_U)
#define my_islower(cs, ch)  (my_ctype_bits(cs, ch) & _MY_L)
#define my_isdigit(cs, ch)  (my_ctype_bits(cs, ch) & _MY_NMR)
#define my_isxdigit(cs, ch) (my_ctype_bits(cs, ch) & _MY_X)
#define my_isspace(cs, ch)  (my_ctype_bits(cs, ch) & _MY_SPC)
#define my_iscntrl(cs, ch)  (my_ctype_bits(cs, ch) & _MY_CTR)
#define my_isalnum(cs, ch)  (my_ctype_bits(cs, ch) & (_MY_U | _MY_L | _MY_NMR))
#define my_isvar(cs, ch)    (my_isalnum(cs, ch) || (ch) == '_')

#define MY_CS_ILSEQ      0
#define MY_CS_TOOSMALL  -101
#define MY_CS_TOOSMALL2 -102
#define MY_CS_TOOSMALL3 -103
#define MY_CS_TOOSMALL4 -104
#define MY_CS_TOOSMALL5 -105
#define MY_CS_TOOSMALL6 -106
#define MY_CS_IS_TOOSMALL(rc) ((rc) >= MY_CS_TOOSMALL6 && (rc) <= MY_CS_TOOSMALL)

#define MY_REPERTOIRE_ASCII      1
#define MY_REPERTOIRE_UNICODE30  2
extern my_bool my_charset_is_ascii_based(CHARSET_INFO *cs);

/* thread_local, same reasoning as national_charset_info above. */
extern thread_local CHARSET_INFO *system_charset_info;
/* strings/my_strtoll10.c skips leading spaces through my_isspace() against
 * latin1; the ctype table is sliced (SLICE_SUPPORT), the CHARSET_INFO that
 * wraps it is built in src/mdbp_lex.cc.  const because nothing ever writes
 * it; my_isspace() is a macro over ->ctype, so it does not care whether its
 * argument is const. */
extern const CHARSET_INFO my_charset_latin1;
/* `to_upper_lex` is not declared here: the slice carries sql_lex.cc's own
 * `static uchar to_upper_lex[]` (sql_lex.cc:646), and a non-static
 * declaration ahead of a static definition is a hard error. */

/* --- sql_mode (thd.md section 2) ----------------------------------------- */
/* One #define per flag row of the ledger's sql_mode table, with the
 * sql_lex.cc line it cites.  The values are bit positions, not MariaDB's own
 * numbering: nothing here compares a mode value with the server. */
#define MODE_ANSI_QUOTES          (1ULL << 0)  /* thd.md: sql_lex.cc:2388 */
#define MODE_PIPES_AS_CONCAT      (1ULL << 1)  /* thd.md: sql_lex.cc:1450 */
#define MODE_HIGH_NOT_PRECEDENCE  (1ULL << 2)  /* thd.md: sql_lex.cc:1447 */
#define MODE_IGNORE_SPACE         (1ULL << 3)  /* thd.md: sql_lex.cc:858/2946 */
#define MODE_NO_BACKSLASH_ESCAPES (1ULL << 4)  /* thd.md: sql_lex.cc:1665 */
#define MODE_ORACLE               (1ULL << 5)  /* thd.md: sql_lex.cc:1420-1444 */
#define MODE_MSSQL                (1ULL << 6)  /* thd.md: sql_lex.cc:2084 */

/* --- MYSQL_VERSION_ID (thd.md section 3) --------------------------------- */
/* thd.md: read at sql_lex.cc:2488, 2493 -- in the slice,
 * generated/lexer_extracted.cc:1767 and :1769, both inside
 * `Lex_input_stream::lex_one_token(YYSTYPE *yylval, THD *thd)` -- and the
 * ledger records the pinned tree's release id there, a constant.
 *
 * Here it is per lexer.  The sliced lexer's bytes do not change:
 * MYSQL_VERSION_ID stays a macro, and it names `mdbp_version_id`, a field of
 * this file's own struct system_variables below, reached through the `thd`
 * parameter both read sites already have.  src/mdbp_lex.cc's mdbp_lex_new()
 * sets the field to MDBP_LEX_DEFAULT_VERSION_ID (src/mdbp_lex.h, the one
 * source of the default), and mdbp_lex_set_version_id() stores a caller's
 * id.  The macro relies on a parameter called `thd` being in scope: after a
 * re-extraction or a hand edit that moves a read site, check that every
 * MYSQL_VERSION_ID in generated/lexer_extracted.cc is still inside a
 * function whose parameter list declares `THD *thd`; nothing checks this
 * automatically. */
#define MYSQL_VERSION_ID (thd->variables.mdbp_version_id)

/* --- parsing_place (thd.md section 1) ------------------------------------ */
/* THE JOIN, and a future reader will look for exactly this.
 *
 * `enum enum_parsing_place` and `struct SELECT_LEX` are NOT declared here.
 * They are declared ONCE, in src/stub.h, which is included just above -- and
 * that is deliberate, because src/stub.h is also what bison's generated
 * parser is compiled against.  So `mdbp_ctx.current_select` (the field the
 * lifted `sql_yacc.yy` actions write) and `LEX::current_select`
 * (the field `Lex_input_stream::lex_token` reads at its five feedback sites,
 * sql_lex.cc:1961/1971/1985/1994/2006) are POINTERS TO THE SAME TYPE, and on
 * the live path they are pointers to THE SAME OBJECT: src/mdbp_lex.cc's
 * `mdbp_lexer` owns one `SELECT_LEX sel`, `mdbp_lex_select()` hands its
 * address out, and src/mdbp_api.c's `mdbp_parse_sql()` points the parse
 * context at it before yyparse runs.  One field, written by MariaDB's own
 * action text and read by MariaDB's own lexer text.
 *
 * Two consequences worth stating: the enum carries all THIRTEEN of
 * sql/sql_priv.h's values now (thd.md names only the four the LEXER tests,
 * but the lifted actions assign nine more), and SELECT_LEX carries
 * `save_parsing_place` too, because sql_yacc.yy:12796/12797/14295 move the
 * field through it. */

#define OPTION_LEX_FOUND_COMMENT (1UL << 0)   /* sql_lex.cc:2412 */

struct LEX
{
  SELECT_LEX *current_select;             /* thd.md rows 1-5, via thd->lex */
  ulong lex_options;                      /* sql_lex.cc:2412, 2423 */
  bool safe_to_cache_query;               /* sql_lex.cc:2510, 2535 */
  /* thd.md section 3, `lookup_keywords_after_qualifier` (sql_lex.cc:2191):
   * assumed false; only sql_yacc.yy's field_spec rule sets it. */
  struct { bool lookup_keywords_after_qualifier; } parsing_options;
};

/* --- THD ----------------------------------------------------------------- */
class Lex_input_stream;          /* defined by the slice, below this header */

/* MYSQLlex/ORAlex reach the lexer as thd->m_parser_state->m_lip.  The
 * definition is in PHASE 2 at the bottom of this file, because it holds a
 * Lex_input_stream BY VALUE and that class is sliced out of sql/sql_lex.h
 * between the two inclusions.  THD only needs the pointer.  (A reference
 * member would not do: a reference cannot be initialised by the
 * malloc-and-memset the bridge uses.) */
struct Parser_state;

struct sql_digest_state;         /* thd.md: digest only, "not modelled" */

struct system_variables
{
  unsigned long long sql_mode;            /* thd.md section 2 */
  bool query_cache_strip_comments;        /* sql_lex.cc versioned-comment arm */
  /* MYSQL_VERSION_ID, above: the
   * release id this lexer judges versioned comments against. */
  unsigned long mdbp_version_id;
};

struct THD
{
  LEX *lex;
  Parser_state *m_parser_state;
  struct system_variables variables;
  int killed;                             /* thd.md: sql_lex.cc:2042, never */
  void send_kill_message();               /* thd.md: sql_lex.cc:2042 */
  CHARSET_INFO *charset() const;          /* thd.md: sql_lex.cc:2038-2040 */
  /* thd.md: sql_lex.cc:1693, "not modelled".  Returns char*, as
   * sql/sql_class.h's own `inline char *alloc(size_t)` does -- get_text
   * (sql_lex.cc:1693) and body_utf8_start both assign it to a char*. */
  char *alloc(size_t size);
  const char *query() const;
  char *strmake(const char *str, size_t size);  /* sql_lex.cc:1794 */
  unsigned long get_utf8_flag() const;          /* sql_lex.cc:2987
                                                 * [with myf: one
                                                 * declaration, both names] */
  /* thd.md section 2, MODE_NO_BACKSLASH_ESCAPES (sql_lex.cc:1665): read by
   * the sliced Lex_input_stream::unescape (sql_lex.cc:1630). */
  bool backslash_escapes() const;
};

/* --- sql_lex.h spellings the sliced class body needs --------------------- */
#define LEX_YYSTYPE YYSTYPE *
typedef int (*my_charset_conv_wc_mb)(CHARSET_INFO *, my_wc_t, uchar *, uchar *);
union YYSTYPE;                   /* defined in phase 2, at the bottom */

/* --- keyword lookup ------------------------------------------------------ */
/* find_keyword calls get_hash_symbol, which MariaDB defines in GENERATED
 * lex_hash.h (built by sql/gen_lex_hash.cc, which itself #includes the
 * server).  Declared here; src/mdbp_lex.cc defines it as a plain search
 * over the same tables.  generated/keywords.h carries sql/lex.h's two
 * tables so the SYM() names are checked against bison's parser.h at compile
 * time. */
struct st_symbol;
extern struct st_symbol *get_hash_symbol(const char *s, unsigned int len,
                                         bool function);

/* bison's parser.h declares `int yyparse(struct mdbp_ctx *ctx)` -- this
 * library's own %param, not a server symbol.  Forward-declared here so
 * including parser.h outside src/mdbp_internal.h is not a diagnostic. */
struct mdbp_ctx;

/* --- state-map generator only (state_maps_gen.cc) ----------------------- */
/* mysys/charset.c's init_state_maps() allocates the 512 bytes through
 * my_once_alloc and asks the charset for a character's length through
 * my_ci_charlen.  Both are macros in MariaDB's own headers.  The generator,
 * state_maps_gen.cc, is written, compiled and run by `tools/lexslice.py
 * --compile-state-maps` to produce generated/state_maps.h; its only output
 * is the two tables, so plain malloc and a direct utf8mb4 call are all it
 * needs.  The library itself does not use these two macros. */
#define my_once_alloc(size, flags) malloc(size)
extern int mdbp_charlen(CHARSET_INFO *cs, const uchar *s, const uchar *e);
#define my_ci_charlen(cs, s, e) mdbp_charlen((cs), (s), (e))

#define DBUG_ASSERT(x) ((void) 0)
#if defined(_MSC_VER) && !defined(__clang__)
/* MSVC reads x = x as a use of x (C4700); MariaDB's FORCE_INIT_OF_VARS form. */
#define UNINIT_VAR(x) x = 0
#else
#define UNINIT_VAR(x) x = x
#endif
#define MYF(v) (v)
#define MY_WME 0
#define STRING_WITH_LEN(X) (X), ((size_t) (sizeof(X) - 1))

/* include/my_global.h / include/my_dbug.h, as the slice uses them.  All five
 * are debug-build scaffolding that compiles to nothing in a release server
 * too; MY_TEST is a boolean cast.  Demanded by the sliced
 * Lex_input_stream::init (sql_lex.cc:810) and ::reset (:855) and by
 * lex_init (:686). */
#define MY_TEST(a) ((a) ? 1 : 0)
#define DBUG_EXECUTE_IF(keyword, code) do { } while (0)
#define DBUG_SET(a) do { } while (0)
#define DBUG_ENTER(name) do { } while (0)
#define DBUG_VOID_RETURN return
#define DBUG_PRINT(keyword, arglist) do { } while (0)

/* include/my_global.h's integer limits and include/my_sys.h's two conversion
 * errno values, as the sliced my_strtoll10 (strings/my_strtoll10.c) uses them.
 * ULONGLONG_MAX is `#define`d twice there, inside an #ifdef, so it is the one
 * constant find_define() cannot locate unambiguously; both spellings are the
 * platform's own limits, which is what <limits.h> already carries. */
#include <limits.h>
#define ULONGLONG_MAX ULLONG_MAX
#define LONGLONG_MIN LLONG_MIN
#define MY_ERRNO_EDOM 33
#define MY_ERRNO_ERANGE 34

#endif /* MDBP_LEXSHIM_H */

/* =========================================================================
 * PHASE 2.  Included a second time by generated/lexer_extracted.cc, AFTER the
 * sliced types, because MariaDB's %union holds `Lex_ident_cli_st` and
 * `Lex_string_with_metadata_st` BY VALUE and those are sliced out of
 * sql/lex_ident_cli.h -- they cannot exist before the slice does.  Nothing in
 * phase 1 needs the union: Lex_input_stream only ever sees `union YYSTYPE *`.
 * ========================================================================= */
#if defined(MDBP_LEXSHIM_PHASE2) && !defined(MDBP_LEXSHIM_PHASE2_DONE)
#define MDBP_LEXSHIM_PHASE2_DONE 1

/* The %union members the sliced lexer touches, and only those.  Each names the
 * sql_lex.cc site that reads it; the rest of sql_yacc.yy's %union is parser
 * business and is not modelled. */
union YYSTYPE
{
  int num;                                       /* scaffolding */
  ulong ulong_num;                               /* scaffolding */
  LEX_CSTRING lex_str;                           /* sql_lex.cc:2069, 2300 */
  Lex_ident_cli_st ident_cli;                    /* sql_lex.cc:2085, 2178 */
  Lex_ident_cli_st kwd;                          /* sql_lex.cc:2090, 2351 */
  Lex_string_with_metadata_st lex_string_with_metadata; /* sql_lex.cc:2150 */
  CHARSET_INFO *charset;                         /* sql_lex.cc:2179, 2183 */
  Lex_comment_st lex_comment;                    /* sql_lex.cc:2565-2575 */
};
#define YYSTYPE_IS_DECLARED 1

/* Now that the slice has defined Lex_input_stream, the parser state can hold
 * one by value.  MYSQLlex/ORAlex (sql_lex.cc:1879/1885) reach the lexer as
 * `thd->m_parser_state->m_lip`, which is the whole reason this type exists
 * here. */
struct Parser_state
{
  Lex_input_stream m_lip;
};

/* parser.h is bison's output for generated/yy_stub.yy: our terminals, our
 * numbering, so sql/lex.h's SYM(A) resolves to OUR token values.  Declaring
 * YYSTYPE above stops parser.h's `typedef int YYSTYPE` fallback. */
#include "parser.h"

#endif /* MDBP_LEXSHIM_PHASE2 */
