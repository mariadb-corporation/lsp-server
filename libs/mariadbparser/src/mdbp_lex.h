/* src/mdbp_lex.h -- the bridge, and the ONLY thing that crosses between the
 * C++ lexer translation unit (src/mdbp_lex.cc, which #includes the sliced
 * build/lexer_extracted.cc) and the C parser translation units.
 *
 * WHY THERE HAS TO BE A BRIDGE AT ALL.  The two halves disagree about
 * YYSTYPE.  src/stub.h records that the extracted grammar is built with
 * `%define api.value.type {int}`, so the parser's YYSTYPE is `int`; the
 * sliced lexer writes into MariaDB's YYSTYPE, a union with `ident_cli`,
 * `lex_str`, `charset`, `kwd` and `lex_comment` members that src/lexshim.h
 * defines.  One `union YYSTYPE` cannot be both, so the two live in different
 * translation units and this header is the seam.  It names no MariaDB type,
 * declares no C++, and is valid C.
 *
 * Its shared types are `mdbp_token` from include/mdbparse.h and `SELECT_LEX` /
 * `enum enum_parsing_place` from src/stub.h (which includes the first) --
 * deliberately REUSED, not duplicated: a second token struct would be a
 * second place for a byte offset to be wrong, and a second SELECT_LEX would
 * be a second place for the one field MariaDB's lexer reads to live (task
 * P1.1; see src/lexshim.h's `THE JOIN`).
 */
#ifndef MDBP_LEX_H
#define MDBP_LEX_H

#include <stddef.h>

#include "stub.h"

typedef struct mdbp_lexer mdbp_lexer;
/* 1 when terminal_name is shiftable in the caller's current context, else 0. */
typedef int (*mdbp_valid_fn)(const char *terminal_name, void *user);

/* sql/len need not be NUL-terminated; the lexer keeps its own copy.
 * sql_mode is the CLI's comma list; charset is the connection charset name
 * (P3.1, PLAN.md D-9/D-10) -- NULL means "utf8mb4"; prepare_mode is 0/1.
 * valid may be NULL, which means "always 0" (parsing_place == NO_MATTER).
 * Returns NULL on allocation failure, an sql_mode this lexer refuses, or a
 * charset this lexer refuses (the caller is expected to have already
 * refused both through mdbp_sql_mode_refused()/mdbp_charset_refused() --
 * this is the defensive backstop, not a second place the rule is stated). */
mdbp_lexer *mdbp_lex_new(const char *sql, size_t len, const char *sql_mode,
                         const char *charset, int prepare_mode,
                         mdbp_valid_fn valid, void *user);
/* 1: *out is the next token.  0: the stream ended.  -1: internal error.
 * out->name is a static string, out->text points into the lexer's copy of
 * the source and stays valid until mdbp_lex_free; out->byte_start/byte_end
 * are byte offsets into the string passed to mdbp_lex_new. */
int mdbp_lex_next(mdbp_lexer *lx, mdbp_token *out);
/* "$end" or "ABORT_SYM" once the stream ended; NULL before that. */
const char *mdbp_lex_eof_token(const mdbp_lexer *lx);

/* THE JOIN (task P1.1).  The one `SELECT_LEX` this lexer reads its
 * `parsing_place` out of, so the parser's own lifted actions can write it:
 * src/mdbp_api.c's mdbp_parse_sql() points `mdbp_ctx.current_select` here
 * before yyparse runs, and from then on the field the actions set and the
 * field `Lex_input_stream::lex_token` reads are the same bytes.  Valid until
 * mdbp_lex_free(); NULL for a NULL lexer. */
SELECT_LEX *mdbp_lex_select(mdbp_lexer *lx);


/* THE COMMENT GATE'S RELEASE ID (plan `tokeniser-entry-points.md` P3).
 *
 * MariaDB's lexer reads a versioned comment -- '/' '*' '!' followed by five
 * or six digits -- as SQL only when those digits pass a test against the
 * server's own release id, MYSQL_VERSION_ID (major*10000 + minor*100 +
 * patch).  src/lexshim.h makes that macro a field of this lexer's THD
 * (its D-7), so the id is per lexer and nothing about it is global (D-8).
 *
 * MDBP_LEX_DEFAULT_VERSION_ID is THE ONE SOURCE of the default (D-9): the
 * pinned MariaDB tree's own release id.  mdbp_lex_new() gives every lexer
 * this value and src/mdbp_api.c's mdbp_parser_new() gives every parser this
 * value; nothing else spells the number.  tests/test_version_id.py asserts
 * that it equals source/PROVENANCE.json's version.id and the Python
 * oracle's reflex.MYSQL_VERSION_ID, so a pin move that forgets it fails
 * there.  The public header and README.md do not repeat it: a caller learns
 * it from mdbp_parser_version_id() on a new parser.
 *
 * MDBP_LEX_VERSION_ID_MIN/_MAX are the accepted range (D-6): the server's
 * test compares five- or six-digit numbers, so an id outside it cannot mean
 * a release.  Both setters -- this file's and the public
 * mdbp_parser_set_version_id() -- check against these two macros and
 * nothing else. */
#define MDBP_LEX_DEFAULT_VERSION_ID 130100UL
#define MDBP_LEX_VERSION_ID_MIN 10000UL
#define MDBP_LEX_VERSION_ID_MAX 999999UL

/* Stores `id` as this lexer's release id.  0 on success; -1, with nothing
 * stored, for a NULL lexer or an id outside MDBP_LEX_VERSION_ID_MIN..
 * MDBP_LEX_VERSION_ID_MAX.  Call it after mdbp_lex_new() and before the first
 * mdbp_lex_next(): the id then
 * governs every versioned comment of the stream. */
int mdbp_lex_set_version_id(mdbp_lexer *lx, unsigned long id);
void mdbp_lex_free(mdbp_lexer *lx);

/* NOT NUL-TERMINATED.  `out->text` points at `byte_start` inside the lexer's
 * own copy of the query, so its length is `byte_end - byte_start` and it is
 * NOT a C string: the byte after it is the next byte of the query.  Every
 * consumer in this repository uses the offsets; nothing calls strlen() on it.
 *
 * THE STREAM ENDS THE WAY tools/vendor/bison2ts/reflex.py's next() ends it:
 *   - END_OF_INPUT IS yielded.  It is a declared terminal (sql_yacc.yy:426)
 *     that three live rules need, and MariaDB's lex_token passes it through.
 *   - bison's $end (token 0) is NOT yielded; it ends the stream and makes
 *     mdbp_lex_eof_token() answer "$end".
 *   - ABORT_SYM is yielded AND ends the stream, with eof_token "ABORT_SYM".
 */

#endif /* MDBP_LEX_H */
