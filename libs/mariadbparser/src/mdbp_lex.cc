/* src/mdbp_lex.cc -- the ONE translation unit that contains MariaDB's sliced
 * lexer, plus every shim body that slice needs, plus the bridge declared in
 * src/mdbp_lex.h.  The library's one C++ file; the parser, the runtime and
 * the API are C.
 *
 * WHY IT #includes generated/lexer_extracted.cc INSTEAD OF LINKING IT
 * ------------------------------------------------------------------
 * Exactly the reason src/parser_glue.c #includes bison's parser.c: the slice
 * is a generated artefact that must be compiled in exactly one place, and the
 * shim bodies below have to see the sliced TYPES (Lex_input_stream and the
 * YYSTYPE union whose members are sliced out of sql/lex_ident_cli.h) to be
 * definable at all.  generated/ is on the include path.  This is our own
 * file including a generated one; changes to the slice go into
 * generated/lexer_extracted.cc as UPSTREAM.md describes, not here.
 *
 * WHAT IS SHIM AND WHAT IS SLICE
 * ------------------------------
 * Slice (tools/lexslice.py, MariaDB's own bytes): lex_token,
 * lex_one_token, find_keyword, the four scan_ident_* members,
 * get_7bit_or_8bit_ident, get_token, consume_comment, init, reset, get_text,
 * unescape, my_unescape, int_token, lex_casecmp, lex_init, MYSQLlex, ORAlex,
 * to_upper_lex, empty_clex_str, the int_token constants, the utf8mb4 ctype
 * table and charlen, latin1's ctype table and my_strtoll10.
 *
 * Shim (below, each with its reason): the fake THD/LEX/SELECT_LEX state
 * docs/thd.md ledgers, the memory the lexer allocates from, the
 * keyword hash MariaDB generates with sql/gen_lex_hash.cc, the charset-name
 * lookup the server answers from a runtime table, and the two
 * "not modelled" families -- the pre-processed (binlog) buffer and the
 * performance-schema digest.
 *
 * THE DIGEST HOOK IS THE SEAM, AND IT IS DELIBERATE
 * -------------------------------------------------
 * MariaDB's own lex_token calls `add_digest_token(token, yylval)` immediately
 * after EVERY lex_one_token() return and before it reads
 * `current_select->parsing_place` (sql_lex.cc:1908-1918).  thd.md records the
 * digest as not modelled, so the body is ours to write -- and it is the only
 * point in MariaDB's own control flow where (a) the token just scanned and
 * (b) the stream position that produced it are both in hand.  So this file's
 * add_digest_token records the token's byte span -- observation, not
 * behaviour.  (`parsing_place` is not touched here: it is written only by
 * the parser's preserved grammar actions; see section 6.)
 */
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* From generated/.  Pulls in src/lexshim.h (twice, phases 1 and 2),
 * generated/lexstates.h, generated/keywords.h, generated/ctype-utf8.h (a copy
 * of MariaDB's strings/ctype-utf8.h) and bison's parser.h. */
#include "lexer_extracted.cc"

/* The two 256-byte tables, computed once by the state-map generator that
 * tools/lexslice.py builds and runs (see UPSTREAM.md) from MariaDB's own
 * init_state_maps() over its own ctype_utf8mb4, and committed. */
#include "state_maps.h"

extern "C" {
#include "mdbp_internal.h"
#include "mdbp_lex.h"
}

/* ======================================================================
 * 1.  The character set.  thd.md row `thd->charset()`: "utf8mb4, always".
 * ====================================================================== */

static int mdbp_strnncollsp(struct charset_info_st *cs, const uchar *a,
                            size_t alen, const uchar *b, size_t blen);

/* Nothing ever writes mdbp_coll, mdbp_cs_utf8mb4, my_charset_bin or
 * my_charset_latin1, but without `const` all four would sit in .data,
 * writable for the life of the process.  `const` here moves the storage to
 * .data.rel.ro: it still carries relocations (the pointers below), but the
 * loader write-protects it once those are resolved, which .data never is.
 * src/lexshim.h's charset_info_st::coll field is const to match, so this
 * needs no cast. */
static const MY_COLLATION_HANDLER mdbp_coll= { mdbp_strnncollsp };

/* ctype / state_map / ident_map / mbmaxlen / number / coll.
 * `number` is utf8mb4_general_ci's collation id in the pinned tree; nothing
 * here compares it with the server, it exists because the struct has it.
 * const, same reason as mdbp_coll above.  CHARSET_INFO itself STAYS
 * non-const (src/lexshim.h's charset_info_st): get_charset_by_csname()'s
 * return type and mdbp_strnncollsp()'s parameter type are dictated by
 * sliced sql_lex.cc call sites that declare non-const `CHARSET_INFO *`
 * locals from them, and changing either would be changing a signature the
 * sliced lexer calls, which the shim must never do.  So every place THIS
 * FILE needs a non-const CHARSET_INFO* out of this const table
 * (system_charset_info, national_charset_info, get_charset_by_csname()'s
 * return, below) casts the const away itself, in our own shim code, never
 * inside sliced output -- the same pattern charset_info_st::charlen()
 * already uses. */
static const charset_info_st mdbp_cs_utf8mb4=
{
  ctype_utf8mb4, mdbp_state_map, mdbp_ident_map, 4, 45, &mdbp_coll
};

/* sql_lex.cc:2986 builds an ErrConvString over &my_charset_bin to name an
 * unknown character set.  The only thing read off it is `ctype`, and only if
 * something asks a ctype question, which nothing here does.  const, same
 * reason as above; src/lexshim.h's ErrConvString constructor takes
 * `const CHARSET_INFO *` to match, so the sliced call site needs no cast. */
const CHARSET_INFO my_charset_bin=
{
  ctype_utf8mb4, mdbp_state_map, mdbp_ident_map, 1, 63, &mdbp_coll
};

/* strings/my_strtoll10.c skips leading spaces through my_isspace() against
 * latin1; ctype_latin1 is sliced out of strings/ctype-latin1.c.  const, same
 * reason as above; my_isspace() is a macro over ->ctype, so it does not care
 * whether its argument is const. */
const CHARSET_INFO my_charset_latin1=
{
  ctype_latin1, mdbp_state_map, mdbp_ident_map, 1, 8, &mdbp_coll
};

/* thread_local: the pointer itself is a per-parse parameter
 * (mdbp_lex_new()/mdbp_lex_next() below set it to the lexer's charset) and
 * the sliced lexer reaches it as a global, so one copy per thread is what
 * keeps concurrent parses apart.
 * Every thread's copy is initialised to the same address, &mdbp_cs_utf8mb4,
 * which is not itself thread_local: there is one shared utf8mb4 table and
 * every thread's pointer points at it.  The const_cast is the one this file
 * comment above explains: the pointee is const, the pointer variable is not,
 * because sql_lex.cc's own call sites need it not to be. */
thread_local CHARSET_INFO *system_charset_info=
  const_cast<CHARSET_INFO *>(&mdbp_cs_utf8mb4);
/* sql_lex.cc:2158 stores this in yylval->charset for N'...' (NCHAR_STRING).
 * The server's is utf8mb3; the value never reaches the token stream (thd.md:
 * the semantic value is not modelled), so it points at the one charset this
 * lexer has. */
thread_local CHARSET_INFO *national_charset_info=
  const_cast<CHARSET_INFO *>(&mdbp_cs_utf8mb4);

bool charset_info_st::use_mb() const
{
  return mbmaxlen > 1;
}

int charset_info_st::charlen(const char *s, const char *e) const
{
  /* MariaDB's my_ci_charlen(cs, s, e) for utf8mb4; the function is sliced out
   * of strings/ctype-utf8.c, so this is a call, not a reimplementation. */
  return my_charlen_utf8mb4((CHARSET_INFO *) this, (const uchar *) s,
                            (const uchar *) e);
}

my_bool my_charset_is_ascii_based(CHARSET_INFO *)
{
  /* utf8mb4 is ASCII-based.  Read only by Lex_string_with_metadata_st::
   * repertoire(), which is semantic-value machinery the token stream never
   * consults; unreferenced by the slice today, defined so it cannot become an
   * undefined symbol silently. */
  return 1;
}

/* find_keyword_qualified_special_func (sql_lex.cc:2782) asks utf8mb4_general_ci
 * whether the identifier equals one of four ASCII names -- SUBSTRING, SUBSTR,
 * TRIM, REPLACE -- and only after checking that the byte LENGTHS are equal
 * ("Check length equality to avoid non-ASCII variants compared as equal to
 * ASCII variants", sql_lex.cc:2779).  Under general_ci every non-ASCII byte
 * sequence weighs its (upper-cased) code point, which is >= 0x80, while all
 * four names weigh < 0x80; so equality is reachable only for all-ASCII input,
 * where general_ci IS ASCII case folding.  That is what this does.  A faithful
 * my_strnncollsp_utf8mb4_general_ci would drag in strings/ctype-utf8.c's
 * template machinery for a predicate that cannot answer differently here. */
static int mdbp_strnncollsp(struct charset_info_st *, const uchar *a,
                            size_t alen, const uchar *b, size_t blen)
{
  size_t n= alen < blen ? alen : blen;
  for (size_t i= 0; i < n; i++)
  {
    uchar x= (uchar) to_upper_lex[a[i]];
    uchar y= (uchar) to_upper_lex[b[i]];
    if (x != y)
      return x < y ? -1 : 1;
  }
  if (alen == blen)
    return 0;
  return alen < blen ? -1 : 1;
}

/* sql_lex.cc:2992: does `_name` in `_latin1'x'` name a character set?  The
 * server answers from all_charsets[], filled at runtime from
 * sql/share/charsets/Index.xml plus the compiled-in charsets, so there is no
 * symbol to slice -- but the NAMES are MariaDB's own data and are derived from
 * the pinned tree into the slice by tools/lexslice.py (see
 * mdbp_charset_names[] there).  Only non-NULL-ness reaches the token stream:
 * what the server returns lands in yylval->charset, which thd.md records as
 * not modelled.  MY_CS_PRIMARY and the utf8 flag are accepted and ignored,
 * exactly as the comment at the call site expects (no MY_WME, no error). */
struct charset_info_st *get_charset_by_csname(const char *name, uint, myf)
{
  if (name == NULL)
    return NULL;
  for (size_t i= 0; i < mdbp_charset_names_count; i++)
  {
    const char *want= mdbp_charset_names[i];
    size_t j= 0;
    while (want[j] != '\0' && name[j] != '\0' &&
           to_upper_lex[(uchar) want[j]] == to_upper_lex[(uchar) name[j]])
      j++;
    if (want[j] == '\0' && name[j] == '\0')
      /* const_cast: mdbp_cs_utf8mb4 is const (see above);
       * this function's return type stays non-const `struct charset_info_st
       * *` because sql_lex.cc:2992's call site declares a non-const
       * `CHARSET_INFO *cs` local from it -- changing the return type would
       * be changing a signature the sliced lexer calls. */
      return const_cast<charset_info_st *>(&mdbp_cs_utf8mb4);
  }
  return NULL;
}

/* sql/sql_error.h's ErrConv wrapper.  scan_ident_middle (sql_lex.cc:2986)
 * builds one for the sole purpose of handing get_charset_by_csname() a
 * NUL-terminated copy of the identifier after the '_'; the comment there says
 * so.  A bounded copy is exactly that, and truncation at the buffer size is
 * what the server's own MYSQL_ERRMSG_SIZE buffer does too. */
ErrConvString::ErrConvString(const char *str, size_t length,
                             const CHARSET_INFO *)
{
  if (length >= sizeof(m_buf))
    length= sizeof(m_buf) - 1;
  if (length > 0 && str != NULL)
    memcpy(m_buf, str, length);
  m_buf[length]= '\0';
}

const char *ErrConvString::ptr() const
{
  return m_buf;
}

/* ======================================================================
 * 2.  The keyword hash.  MariaDB generates get_hash_symbol() into lex_hash.h
 *     with sql/gen_lex_hash.cc, which #includes the server to build a perfect
 *     hash over the very tables generated/keywords.h already carries.  We
 *     cannot run that generator (it needs the server's headers), so this is
 *     the same QUESTION answered without the hash: gen_lex_hash.cc:404-478
 *     searches symbols[] when `function` is false and symbols[] +
 *     sql_functions[] when it is true, always through
 *     lex_casecmp/to_upper_lex, always on the full length.  Both tables
 *     and lex_casecmp are sliced; only the search is ours, and it is linear
 *     rather than hashed because a parser that lexes one statement at a
 *     time does not need the server's hash.
 * ====================================================================== */

struct st_symbol *get_hash_symbol(const char *s, unsigned int len, bool function)
{
  if (len == 0)
    return NULL;                     /* gen_lex_hash.cc:410 */
  for (size_t i= 0; i < symbols_length; i++)
    if (symbols[i].length == len && !lex_casecmp(s, symbols[i].name, len))
      return &symbols[i];
  if (function)
    for (size_t i= 0; i < sql_functions_length; i++)
      if (sql_functions[i].length == len &&
          !lex_casecmp(s, sql_functions[i].name, len))
        return &sql_functions[i];
  return NULL;
}

/* ======================================================================
 * 3.  The lexer object and its memory.
 * ====================================================================== */

/* One malloc per lexer allocation, chained so mdbp_lex_free gives them all
 * back.  MariaDB allocates from the THD's MEM_ROOT (thd.md: "not modelled");
 * a chain of mallocs has the one property the lexer needs -- everything stays
 * alive until the statement is done. */
struct mdbp_block
{
  struct mdbp_block *next;
};

struct mdbp_span
{
  long start;
  long end;
};

struct mdbp_lexer
{
  char *buf;                    /* our own writable, NUL-terminated copy */
  size_t len;

  THD thd;
  LEX lex;
  SELECT_LEX sel;
  Parser_state ps;
  YYSTYPE yylval;

  struct mdbp_block *blocks;

  mdbp_valid_fn valid;
  void *user;

  /* This lexer's charset, resolved once by
   * mdbp_charset_by_name() in mdbp_lex_new() below.  mdbp_lex_new() and
   * mdbp_lex_next() set system_charset_info/national_charset_info to this
   * value for the duration of the sliced calls that can read them, the same
   * bracket-and-restore mdbp_current already uses. */
  CHARSET_INFO *charset;

  /* filled by Lex_input_stream::add_digest_token, one entry per
   * lex_one_token() return inside the current lex_token() call */
  int nrec;
  mdbp_span rec[2];

  mdbp_span pending;            /* the span of the token lex_token buffered */
  int have_pending;

  int finished;
  const char *eof_token;
};

/* The lexer whose lex_token() call is in progress, for add_digest_token to
 * find.  A member function has no user pointer, and each thread parses one
 * statement at a time; mdbp_lex_next sets it around the call and clears it
 * after, so it is never stale within a thread.
 *
 * It is per parse, so it is `thread_local` -- one pointer per thread rather
 * than one shared across every thread's concurrent parse.  No signature
 * change. */
static thread_local mdbp_lexer *mdbp_current= NULL;

static void *mdbp_alloc(mdbp_lexer *lx, size_t size)
{
  struct mdbp_block *block=
    (struct mdbp_block *) malloc(sizeof(struct mdbp_block) + size);

  if (block == NULL)
    return NULL;
  block->next= lx->blocks;
  lx->blocks= block;
  return (void *) (block + 1);
}

/* ======================================================================
 * 4.  The THD shim.  One body per thd.md row.
 * ====================================================================== */

/* thd.md `thd->charset()` (sql_lex.cc:2038): utf8mb4, always.  FAITHFUL:
 * utf8mb4 is the only connection charset this lexer accepts. */
CHARSET_INFO *THD::charset() const
{
  return system_charset_info;
}

/* thd.md `thd->variables.sql_mode & MODE_NO_BACKSLASH_ESCAPES`
 * (sql_lex.cc:1665).  FAITHFUL: the flag reaches here from the caller's
 * sql_mode list. */
bool THD::backslash_escapes() const
{
  return (variables.sql_mode & MODE_NO_BACKSLASH_ESCAPES) == 0;
}

/* thd.md `thd->alloc` (sql_lex.cc:1693): "not modelled" -- the MEM_ROOT is
 * server state.  FAITHFUL IN EFFECT: the caller needs `size` bytes that
 * outlive the token, which is what this gives it. */
char *THD::alloc(size_t size)
{
  return (char *) mdbp_alloc(mdbp_current, size);
}

/* sql_lex.cc:1794, get_token(): a copy of `size` bytes with a NUL.  Same
 * arena as alloc(). */
char *THD::strmake(const char *str, size_t size)
{
  char *to= (char *) mdbp_alloc(mdbp_current, size + 1);

  if (to == NULL)
    return NULL;
  if (size > 0 && str != NULL)
    memcpy(to, str, size);
  to[size]= '\0';
  return to;
}

/* thd.md `thd->killed` (sql_lex.cc:2042): "never" for a parse-only run.
 * DELIBERATE NO-OP: `killed` is memset to 0 and nothing ever sets it, so the
 * branch that would call this is unreachable. */
void THD::send_kill_message()
{
}

/* sql_lex.cc:2987, the utf8 flag passed to get_charset_by_csname(): a MYF()
 * bit that selects utf8mb3-vs-utf8mb4 aliasing for the name `utf8`.  Our
 * get_charset_by_csname resolves `utf8` either way (both spellings are in the
 * derived name table), so 0 is the whole of it. */
unsigned long THD::get_utf8_flag() const
{
  return 0;
}

/* sql_lex.cc reads thd->query() only inside the WITH_WSREP block that
 * tools/lexslice.py's isolate_wsrep rule removes.  Defined so a future
 * reference cannot become an undefined symbol at link time. */
const char *THD::query() const
{
  return mdbp_current != NULL ? mdbp_current->buf : NULL;
}

/* ======================================================================
 * 5.  The two "not modelled" families (thd.md), as DELIBERATE NO-OPS.
 *
 *     m_body_utf8 is the pre-processed buffer the binlog and the query cache
 *     see; the digest is performance_schema's.  Neither is ever read back by
 *     lex_one_token or lex_token, so neither can change a token -- which is
 *     why an empty body is honest here and would not be for get_text().
 * ====================================================================== */

void Lex_input_stream::body_utf8_append(const char *)
{
}

void Lex_input_stream::body_utf8_append(const char *, const char *)
{
}

void Lex_input_stream::body_utf8_append_ident(THD *,
                                              const Lex_string_with_metadata_st *,
                                              const char *)
{
}

void Lex_input_stream::body_utf8_append_escape(THD *, const LEX_CSTRING *,
                                               CHARSET_INFO *, const char *,
                                               my_wc_t)
{
}

/* ======================================================================
 * 6.  add_digest_token -- the digest no-op, and the seam (see the file
 *     header).  MariaDB calls it once per lex_one_token() return, before
 *     lex_token reads current_select->parsing_place.
 * ====================================================================== */

/* The six tokens lex_token invents by CONTRACTING two lex_one_token results
 * (sql_lex.cc:1922-1958); every other two-call path BUFFERS the second token
 * instead.  That is the whole difference the bridge needs in order to know
 * whether the span it reports covers one lexeme or two. */
static int mdbp_is_contraction(int token)
{
  return token == WITH_CUBE_SYM || token == WITH_ROLLUP_SYM ||
         token == WITH_SYSTEM_SYM || token == FOR_SYSTEM_TIME_SYM ||
         token == VALUES_LESS_SYM || token == VALUES_IN_SYM;
}

/* WHERE `parsing_place` COMES FROM.
 *
 * Not from here.  The field is written only by the parser's preserved
 * grammar actions: MariaDB's own statements, kept verbatim inside otherwise
 * blanked actions by `tools/stub.py`'s `preserve_parsing_place` rule, which
 * write into the very `SELECT_LEX` this lexer reads: `mdbp_lex_select()`
 * below hands `&lx->sel` to the parse context, and `src/mdbp_api.c`'s
 * `mdbp_parse_sql()` installs it before `yyparse` runs.
 *
 * When no parser runs (mdbp_lex_sql() and mdbp_lex_sql_prefix()), nothing
 * writes it and it stays the NO_MATTER mdbp_lex_new() gives it.  Nothing
 * infers the value or its timing; the spend (`sql_lex.cc:1963` and the
 * rest, already in the slice) is MariaDB's own and is what clears it.
 *
 * `add_digest_token` therefore does one job -- recording the span. */

void Lex_input_stream::add_digest_token(uint token, LEX_YYSTYPE yylval)
{
  mdbp_lexer *lx= mdbp_current;
  long limit;
  long start;
  long end;

  if (lx == NULL)
    return;                       /* the digest itself: not modelled */

  limit= (long) (m_end_of_query - m_buf);
  start= (long) (m_tok_start - m_buf);
  end= (long) (m_ptr - m_buf);

  /* The LEXEME, where the C recorded one.  scan_ident_middle and
   * scan_ident_common set `str`/`length` to exactly (m_tok_start, length)
   * before returning, and MY_LEX_CHAR sets (m_tok_start, 1); m_ptr can be
   * further along than that -- under IGNORE_SPACE it sits past the skipped
   * spaces (sql_lex.cc:2946), after a MY_LEX_ESCAPE fall-through it sits past
   * the escaped byte.  Anchoring on `str == m_tok_start` is what tells a
   * lexeme the C just measured from a stale semantic value: get_token() and
   * get_text() both point `str` at THD-allocated memory instead. */
  /* ABORT_SYM is EXCLUDED from this anchor.  The plain '/' '*' comment branch
   * (sql_lex.cc, the arm before MY_LEX_LONG_COMMENT's consume_comment(0))
   * writes yylval->lex_comment.str = m_ptr for its own bookkeeping, at the
   * moment m_ptr == m_tok_start -- before the '/' and '*' are even skipped
   * -- and lex_comment shares the union's leading pointer field with
   * ident_cli, so lex_comment.str ALIASES ident_cli.str and satisfies the
   * anchor by coincidence, while ident_cli.length is left holding whatever
   * the PRECEDING real token set (lex_comment.length is never written on
   * this path): a stale value that would truncate the ABORT_SYM span. No
   * ABORT_SYM return in the slice ever sets an identifier lexeme
   * (sql/sql_lex.cc:2312, 2324, 2328, 2338, 2599, 2644, 2652, 2805), so its
   * end is always m_ptr, never yylval-derived. */
  if (token != ABORT_SYM &&
      yylval != NULL && yylval->ident_cli.str == m_tok_start &&
      yylval->ident_cli.length > 0 &&
      m_tok_start + yylval->ident_cli.length <= m_ptr)
    end= start + (long) yylval->ident_cli.length;
  else if (token == UNDERSCORE_CHARSET && m_cpp_text_end > m_cpp_text_start)
    /* the introducer arm overwrites yylval->ident_cli with the CHARSET_INFO
     * (sql_lex.cc:2990, a union), but it has already recorded the identifier's
     * extent in the pre-processed buffer, whose LENGTH is the same */
    end= start + (long) (m_cpp_text_end - m_cpp_text_start);

  if (token > 0 && token < 256)
  {
    /* A character token is always one byte.  MY_LEX_COMMA is the one return
     * that has moved m_tok_start past it first (restart_token(),
     * sql_lex.cc:2137), which leaves m_tok_start == m_ptr. */
    if (m_tok_start == m_ptr)
      start= end - 1;
    else
      end= start + 1;
  }
  else if (token == 0 || token == END_OF_INPUT ||
           (token == ABORT_SYM && in_comment != NO_COMMENT &&
            m_ptr >= m_end_of_query && m_ptr == m_tok_start + 1))
  {
    /* The three EMPTY lexemes.  MY_LEX_EOL (sql_lex.cc:2635) and MY_LEX_END
     * (:2650) both arrive here having consumed the buffer's NUL terminator --
     * a byte that is not part of the query -- so the token covers nothing.
     * The condition on ABORT_SYM is MY_LEX_EOL's own (`eof()` and inside a
     * comment, one yyGet since start_token); every OTHER ABORT_SYM return
     * scanned real bytes and keeps them. */
    end= start;
  }

  if (start > limit)
    start= limit;
  if (end > limit)
    end= limit;
  if (end < start)
    end= start;

  if (lx->nrec < (int) (sizeof lx->rec / sizeof lx->rec[0]))
  {
    lx->rec[lx->nrec].start= start;
    lx->rec[lx->nrec].end= end;
  }
  lx->nrec++;
}

/* ======================================================================
 * 7.  The bridge (src/mdbp_lex.h).
 * ====================================================================== */

/* The five sql_mode flags this lexer implements (the ones thd.md section 2
 * shows the lexer reading), the two it refuses (ORACLE, MSSQL), and the six
 * composite names of MDBP_MODE_COMPOSITES below, which OR in some of these
 * five bits the same way sql/sys_vars.cc's expand_sql_mode() does.  Every
 * other element of the comma list -- TRADITIONAL included -- is a server
 * mode with no lexical effect and is accepted and inert. */
struct mdbp_mode_flag
{
  const char *name;
  unsigned long long bit;
};

static const struct mdbp_mode_flag MDBP_MODE_FLAGS[]=
{
  { "ANSI_QUOTES", MODE_ANSI_QUOTES },
  { "PIPES_AS_CONCAT", MODE_PIPES_AS_CONCAT },
  { "HIGH_NOT_PRECEDENCE", MODE_HIGH_NOT_PRECEDENCE },
  { "IGNORE_SPACE", MODE_IGNORE_SPACE },
  { "NO_BACKSLASH_ESCAPES", MODE_NO_BACKSLASH_ESCAPES },
};

/* The six composite sql_mode names sql/sys_vars.cc's expand_sql_mode()
 * (pinned tree bab03b0f) ORs some of MDBP_MODE_FLAGS' five bits into --
 * ANSI, POSTGRESQL, DB2 and MAXDB each add ANSI_QUOTES | PIPES_AS_CONCAT |
 * IGNORE_SPACE; MYSQL40 and MYSQL323 each add HIGH_NOT_PRECEDENCE.
 * expand_sql_mode() also
 * ORs bits this lexer has no flag for (MODE_NO_KEY_OPTIONS and the rest),
 * which is why this table's bits are a SUBSET of the server's, never the
 * whole of it.  TRADITIONAL is not here: it ORs none of the five (only
 * MODE_STRICT_TRANS_TABLES and other flags this lexer does not read), so it
 * stays inert exactly like any other unmatched name.  ORACLE and MSSQL are
 * not here either: they stay refused regardless of what they would expand
 * to.  When the pin moves, re-check this table against expand_sql_mode() in
 * the new sql/sys_vars.cc.
 *
 * Each row's `name` is a fixed-size char array, not a pointer: a
 * `const char *` initialised from a string literal needs a load-time
 * relocation, which would move this table out of `.rodata` into
 * `.data.rel.ro`, among the library's writable-at-load-time globals.  A
 * fixed-size array holds its bytes inline: no relocation, and the table
 * stays in `.rodata`. */
struct mdbp_mode_composite
{
  char name[11];               /* "POSTGRESQL", the longest, plus the NUL */
  unsigned long long bits;
};

static const struct mdbp_mode_composite MDBP_MODE_COMPOSITES[]=
{
  { "ANSI",       MODE_ANSI_QUOTES | MODE_PIPES_AS_CONCAT | MODE_IGNORE_SPACE },
  { "POSTGRESQL", MODE_ANSI_QUOTES | MODE_PIPES_AS_CONCAT | MODE_IGNORE_SPACE },
  { "DB2",        MODE_ANSI_QUOTES | MODE_PIPES_AS_CONCAT | MODE_IGNORE_SPACE },
  { "MAXDB",      MODE_ANSI_QUOTES | MODE_PIPES_AS_CONCAT | MODE_IGNORE_SPACE },
  { "MYSQL40",    MODE_HIGH_NOT_PRECEDENCE },
  { "MYSQL323",   MODE_HIGH_NOT_PRECEDENCE },
};

static int mdbp_name_eq(const char *begin, size_t n, const char *name)
{
  size_t i;

  for (i = 0; i < n; i++)
    if (name[i] == '\0' ||
        to_upper_lex[(uchar) begin[i]] != to_upper_lex[(uchar) name[i]])
      return 0;
  return name[n] == '\0';
}

/* Returns 0 and fills *bits, or -1 when the list names ORACLE or MSSQL --
 * modes this lexer cannot honour, which must be refused rather than look
 * honoured. */
static int mdbp_parse_sql_mode(const char *list, unsigned long long *bits)
{
  const char *p= list == NULL ? "" : list;

  *bits= 0;
  while (*p != '\0')
  {
    const char *begin= p;
    size_t n;
    size_t k;

    while (*p != '\0' && *p != ',')
      p++;
    n= (size_t) (p - begin);
    if (mdbp_name_eq(begin, n, "ORACLE") || mdbp_name_eq(begin, n, "MSSQL"))
      return -1;
    for (k= 0; k < sizeof MDBP_MODE_FLAGS / sizeof MDBP_MODE_FLAGS[0]; k++)
      if (mdbp_name_eq(begin, n, MDBP_MODE_FLAGS[k].name))
        *bits |= MDBP_MODE_FLAGS[k].bit;
    /* A composite ORs its own bits in, on top of (never instead of) a
     * single flag's -- the same loop shape as MDBP_MODE_FLAGS above, so a
     * list may mix composites and single flags freely. */
    for (k= 0; k < sizeof MDBP_MODE_COMPOSITES / sizeof MDBP_MODE_COMPOSITES[0]; k++)
      if (mdbp_name_eq(begin, n, MDBP_MODE_COMPOSITES[k].name))
        *bits |= MDBP_MODE_COMPOSITES[k].bits;
    if (*p == ',')
      p++;
  }
  return 0;
}

/* The name -> table step, for a name
 * mdbp_charset_refused() (src/mdbp_api.c) has ALREADY accepted -- this is not
 * a second refusal rule, it is the resolution the C side has no CHARSET_INFO
 * type to perform itself.  utf8mb4_general_ci is the one alias
 * mdbp_charset_refused() accepts, and it resolves to the SAME table: there is
 * only one, so accepting the alias is not accepting a second behaviour.
 * NULL only if this is ever called with a name the C-side
 * refusal did not see -- defensive, mirrors mdbp_parse_sql_mode()'s -1 for
 * ORACLE/MSSQL above.  The const_cast is the one src/mdbp_lex.cc's charset
 * section explains: mdbp_cs_utf8mb4 is const, this function's return type
 * matches system_charset_info/national_charset_info's (non-const), which
 * sql_lex.cc's own call sites require. */
static CHARSET_INFO *mdbp_charset_by_name(const char *name)
{
  const char *n= name != NULL ? name : "utf8mb4";
  size_t len= strlen(n);

  if (mdbp_name_eq(n, len, "utf8mb4") || mdbp_name_eq(n, len, "utf8mb4_general_ci"))
    return const_cast<CHARSET_INFO *>(&mdbp_cs_utf8mb4);
  return NULL;
}

extern "C" mdbp_lexer *mdbp_lex_new(const char *sql, size_t len,
                                    const char *sql_mode, const char *charset,
                                    int prepare_mode, mdbp_valid_fn valid,
                                    void *user)
{
  /* sql_lex.cc:686 -- symbols[].length / sql_functions[].length, which
   * get_hash_symbol compares against.  MariaDB calls it once per server
   * start; once per process is the same thing.
   *
   * lex_init() WRITES MariaDB's own shared keyword tables
   * (symbols[].length, sql_functions[].length), so two threads calling
   * mdbp_parser_new() at the same moment must not both run it, concurrently,
   * over the same tables while a third thread's lexer could already be
   * reading them.  Those tables are MariaDB's and live in generated output
   * (generated/keywords.h), which a re-extraction rewrites: the guard
   * therefore does not go on them, it goes HERE, on the one call site,
   * which is ours and survives regeneration.
   *
   * pthread_once(3) rather than a C++ function-local static: a dynamically
   * initialised local static makes the compiler emit __cxa_guard_acquire /
   * __cxa_guard_release, which live in the C++ runtime library and would
   * make this library depend on it.  std::call_once is out for the same
   * reason.  On POSIX systems this is the same primitive the C side's
   * mdbp_once() (src/mdbp_once.h) uses, and it lives in libc.  Every thread
   * returning from pthread_once() sees the finished tables, which is the
   * happens-before edge a bare `if (!done)` would not give. */
  static pthread_once_t keyword_lengths_once= PTHREAD_ONCE_INIT;
  unsigned long long bits= 0;
  mdbp_lexer *lx;
  mdbp_lexer *saved;
  CHARSET_INFO *cs;
  CHARSET_INFO *saved_sys;
  CHARSET_INFO *saved_nat;

  if (mdbp_parse_sql_mode(sql_mode, &bits) != 0)
    return NULL;
  cs= mdbp_charset_by_name(charset);
  if (cs == NULL)
    return NULL;

  lx= (mdbp_lexer *) malloc(sizeof *lx);
  if (lx == NULL)
    return NULL;
  memset((void *) lx, 0, sizeof *lx);

  lx->len= len;
  lx->buf= (char *) malloc(len + 1);
  if (lx->buf == NULL)
  {
    free(lx);
    return NULL;
  }
  if (len > 0 && sql != NULL)
    memcpy(lx->buf, sql, len);
  /* MariaDB's alloc_query() NUL-terminates the query buffer, and
   * lex_one_token relies on it: state_map[0] == MY_LEX_EOL is how the scanner
   * learns it is at the end. */
  lx->buf[len]= '\0';

  lx->valid= valid;
  lx->user= user;
  lx->charset= cs;
  lx->thd.lex= &lx->lex;
  lx->thd.m_parser_state= &lx->ps;
  lx->thd.variables.sql_mode= bits;
  /* The comment gate's release id (src/lexshim.h's MYSQL_VERSION_ID).
   * Every lexer starts at the one default; mdbp_lex_set_version_id() below
   * is how a caller changes it.  A field of this lexer, so no thread_local
   * and no global. */
  lx->thd.variables.mdbp_version_id= MDBP_LEX_DEFAULT_VERSION_ID;
  lx->lex.current_select= &lx->sel;
  /* NO_MATTER is the value the field carries everywhere outside the grammar
   * actions that set it.  The memset above already made it so; it is spelled
   * out because it is the contract, not an accident of zero. */
  lx->sel.parsing_place= NO_MATTER;
  lx->sel.save_parsing_place= NO_MATTER;

  pthread_once(&keyword_lengths_once, lex_init);

  /* init() allocates the pre-processed buffer through THD::alloc, which reads
   * mdbp_current; set it for the duration.  system_charset_info and
   * national_charset_info bracket it the same way:
   * init() is one of the two places the sliced lexer can read them, the
   * other being MYSQLlex() in mdbp_lex_next below. */
  saved= mdbp_current;
  mdbp_current= lx;
  saved_sys= system_charset_info;
  saved_nat= national_charset_info;
  system_charset_info= lx->charset;
  national_charset_info= lx->charset;
  if (lx->ps.m_lip.init(&lx->thd, lx->buf, len))
  {
    system_charset_info= saved_sys;
    national_charset_info= saved_nat;
    mdbp_current= saved;
    mdbp_lex_free(lx);
    return NULL;
  }
  system_charset_info= saved_sys;
  national_charset_info= saved_nat;
  mdbp_current= saved;

  /* reset() sets stmt_prepare_mode FALSE (sql_lex.cc:857) because the server
   * turns it on only for a PREPARE; thd.md section 3 (`stmt_prepare_mode`)
   * defaults it ON here, so that `?` is accepted, and a caller's
   * prepare_mode 0 (the CLI's --no-prepare-mode) turns it off. */
  lx->ps.m_lip.stmt_prepare_mode= prepare_mode ? TRUE : FALSE;
  return lx;
}

extern "C" void mdbp_lex_free(mdbp_lexer *lx)
{
  struct mdbp_block *block;

  if (lx == NULL)
    return;
  block= lx->blocks;
  while (block != NULL)
  {
    struct mdbp_block *next= block->next;

    free(block);
    block= next;
  }
  free(lx->buf);
  free(lx);
}

extern "C" const char *mdbp_lex_eof_token(const mdbp_lexer *lx)
{
  return lx == NULL ? NULL : lx->eof_token;
}

/* THE JOIN (src/lexshim.h's `THE JOIN` comment).
 *
 * The `SELECT_LEX` this lexer reads through `thd->lex->current_select` is
 * `lx->sel`, one object, owned by the lexer for its whole life.  Handing its
 * address to the parse context is what makes `mdbp_ctx.current_select` and
 * this field THE SAME STORAGE, so a lifted `sql_yacc.yy` action's write is
 * the byte `Lex_input_stream::lex_token` reads at its next feedback site.
 * Returns NULL only for a NULL lexer. */
extern "C" SELECT_LEX *mdbp_lex_select(mdbp_lexer *lx)
{
  return lx == NULL ? NULL : &lx->sel;
}

/* The comment gate's release id, for this lexer only.  It writes the field
 * src/lexshim.h's MYSQL_VERSION_ID names, which the sliced lex_one_token()
 * reads through its own `thd` parameter at both read sites, so the sliced
 * bytes never see a difference.  The range is checked BEFORE anything is
 * stored: an id the server's five- or six-digit test cannot mean is refused
 * with -1 and the lexer keeps the id it had. */
extern "C" int mdbp_lex_set_version_id(mdbp_lexer *lx, unsigned long id)
{
  if (lx == NULL || id < MDBP_LEX_VERSION_ID_MIN || id > MDBP_LEX_VERSION_ID_MAX)
    return -1;
  lx->thd.variables.mdbp_version_id= id;
  return 0;
}

/* The second half of thd.md row 5's `valid()` predicate for sql_lex.cc:2004.
 *
 * The C's flag is CONSUMED (sql_lex.cc:2013 sets NO_MATTER before the four-way
 * return at 2014-2021), so one grammar action arms exactly one `(`.
 * valid_symbols[] is a property of the parser state and cannot be spent, so
 * the consumption is modelled the only way it can be: by asking valid() about
 * the token the C actually chose and emitting it only if the parser can take
 * it.  Without this step the arm fires where the server's would already
 * be spent; the witness is the partition list of `CREATE TABLE t1
 * (a int) PARTITION BY RANGE (a) (PARTITION p0 VALUES LESS THAN (1))`, whose
 * state can shift LEFT_PAREN_WITH but not LEFT_PAREN_ALT (thd.md row 5).
 *
 * Downgrading AFTER lex_token returned is equivalent to declining inside it:
 * the arm's only other effects -- buffering the lookahead
 * (sql_lex.cc:2010-2012) and clearing parsing_place (2013) -- happen before
 * the choice and are what the C does for the plain `'('` return at 2020 too.
 * The reported span is unaffected: none of the three is a contraction
 * (mdbp_is_contraction), so mdbp_lex_next already reports rec[0], the `(`
 * itself, and keeps rec[1] pending.
 *
 * Only sql_lex.cc:2017/2019/2021 can produce these three token numbers --
 * lex_one_token returns none of them, so neither can the buffered token of
 * sql_lex.cc:1896 -- which is what makes the token number alone a sound test
 * for "this call took the `(` arm".
 */
static int mdbp_downgrade_left_paren(mdbp_lexer *lx, int token)
{
  const char *chosen;

  if (token != LEFT_PAREN_LIKE && token != LEFT_PAREN_WITH &&
      token != LEFT_PAREN_ALT)
    return token;

  chosen= mdbp_name_for_token_number(token);
  if (lx->valid != NULL && chosen != NULL && lx->valid(chosen, lx->user))
    return token;
  return (int) '(';                   /* sql_lex.cc:2020's return, one arm on */
}

extern "C" int mdbp_lex_next(mdbp_lexer *lx, mdbp_token *out)
{
  int token;
  const char *name;
  mdbp_span span;
  CHARSET_INFO *saved_sys;
  CHARSET_INFO *saved_nat;

  if (lx == NULL || out == NULL)
    return -1;
  if (lx->finished)
    return 0;

  lx->nrec= 0;
  mdbp_current= lx;
  /* system_charset_info/national_charset_info bracket MYSQLlex() the same
   * way mdbp_current does: this is the other of the two
   * places the sliced lexer can read them (the other is init(), in
   * mdbp_lex_new above). */
  saved_sys= system_charset_info;
  saved_nat= national_charset_info;
  system_charset_info= lx->charset;
  national_charset_info= lx->charset;
  /* MYSQLlex is sliced (sql_lex.cc:1879); it is the MariaDB grammar variant's
   * own entry point and forwards to lex_token, which is where the merge layer
   * and the five feedback sites live. */
  token= MYSQLlex(&lx->yylval, &lx->thd);
  system_charset_info= saved_sys;
  national_charset_info= saved_nat;
  mdbp_current= NULL;
  token= mdbp_downgrade_left_paren(lx, token);

  if (lx->nrec == 0)
  {
    /* lex_token returned the token it had buffered on an earlier call
     * (sql_lex.cc:1896); its span was recorded when it was scanned. */
    if (!lx->have_pending)
      return -1;
    span= lx->pending;
    lx->have_pending= 0;
  }
  else if (lx->nrec == 1)
  {
    span= lx->rec[0];
  }
  else if (lx->nrec == 2)
  {
    if (mdbp_is_contraction(token))
    {
      span.start= lx->rec[0].start;   /* one token out of two: the span covers
                                       * both */
      span.end= lx->rec[1].end;
    }
    else
    {
      span= lx->rec[0];               /* the second token was buffered */
      lx->pending= lx->rec[1];
      lx->have_pending= 1;
    }
  }
  else
  {
    return -1;                        /* lex_token never scans more than two */
  }

  name= mdbp_name_for_token_number(token);
  if (name == NULL)
    return -1;

  if (token == 0)
  {
    /* bison's end marker.  Not yielded; it is what ends the stream. */
    lx->finished= 1;
    lx->eof_token= "$end";
    return 0;
  }

  out->name= name;
  out->text= lx->buf + span.start;
  out->token_number= token;
  out->byte_start= span.start;
  out->byte_end= span.end;

  if (strcmp(name, "ABORT_SYM") == 0)
  {
    /* yielded AND ends the stream: it is the lexer telling the parser the
     * input is broken, and lex_one_token can return it forever (MY_LEX_EOL
     * inside an unclosed comment). */
    lx->finished= 1;
    lx->eof_token= "ABORT_SYM";
  }
  return 1;
}
