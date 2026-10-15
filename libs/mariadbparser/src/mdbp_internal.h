/* src/mdbp_internal.h -- the interface between the three translation units of
 * build/mdbparse.  Deliberately NOT src/stub.h (which is the grammar's own
 * runtime contract and holds nothing else) and deliberately NOT
 * include/mdbparse.h, which is P6's library surface and is not written yet.
 *
 *   src/mdbp_runtime.c  the token vector, the derivation buffer, yylex, and
 *                       the token tables, built from bison's own arrays
 *   src/parser_glue.c   THE one translation unit that contains bison's
 *                       generated parser (it #includes build/parser.c)
 *   src/mdbparse.c      the CLI: arguments, input, the JSON verdict
 *
 * Since task 9.1 there is a fourth, src/mdbp_api.c, which implements
 * include/mdbparse.h -- the PUBLIC surface.  It is the one implementation of
 * "parse one record"; the CLI is its first client and reaches the parser only
 * through it.  The two declarations at the bottom of this file are the only
 * things the CLI still borrows from it directly, and both are rules
 * ("what does this library refuse") rather than parsing.
 */
#ifndef MDBP_INTERNAL_H
#define MDBP_INTERNAL_H

#include "stub.h"

/* --- src/parser_glue.c (bison's own tables, read by the runtime) -------- */

int mdbp_yy_ntokens(void);              /* YYNTOKENS: terminal symbol kinds */
int mdbp_yy_nrules(void);               /* YYNRULES, including rule 0 $accept */
int mdbp_yy_maxutok(void);              /* YYMAXUTOK: the largest token number */
int mdbp_yy_translate(int token_number);/* token number -> symbol kind */
const char *mdbp_yy_tname(int kind);    /* yytname[kind], bison's spelling */

/* --- src/mdbp_runtime.c (the token tables, built once from the above) ---- */

/* Terminal name -> bison token number, O(log n) over all terminals.
 * Returns -1 when `name` is not a terminal of this grammar. */
int mdbp_token_number(const char *name);

/* Bison symbol kind -> terminal name in the grammar's spelling ("$end",
 * "SELECT_SYM", "'('"), or NULL when the kind is out of range or is bison's
 * own undefined-token kind. */
const char *mdbp_symbol_name(int kind);



/* --- src/mdbp_runtime.c (the context) ------------------------------------ */

void mdbp_ctx_init(mdbp_ctx *ctx, const mdbp_token *tokens, size_t ntokens,
                   int want_derivation);
void mdbp_ctx_free(mdbp_ctx *ctx);

/* Re-points an ALREADY INITIALISED context at the next record.  This is what
 * batch mode (--jsonl) calls between records, and neither of the two calls
 * above can stand in for it: mdbp_ctx_init() memsets the whole struct, so a
 * second call over a live context leaks the derivation buffer, and
 * mdbp_ctx_free() gives back the capacity the run has already paid to grow
 * (the longest recorded derivation in the corpus/expected jsonl files is
 * 144,053 entries, so re-growing it 212,332 times is not a rounding error).
 *
 * Keeps: the derivation allocation and its capacity, and want_derivation.
 * Clears: the cursor, the lookahead, the derivation LENGTH, the internal-error
 * flag and the whole error record -- including ctx->expected, which
 * src/parser_glue.c mallocs afresh on EVERY rejected parse, so not freeing it
 * here would leak once per rejection. */
void mdbp_ctx_reset(mdbp_ctx *ctx, const mdbp_token *tokens, size_t ntokens);

/* Arms (or, with a NULL `source`, disarms) the pull path yylex takes when the
 * PARSER drives the lexer -- `--valid live`.  Always called AFTER
 * mdbp_ctx_init/mdbp_ctx_reset, both of which clear it. */
void mdbp_ctx_set_source(mdbp_ctx *ctx, mdbp_source_fn source, void *user);

/* --- src/parser_glue.c --------------------------------------------------- */

/* Runs yyparse over ctx.  Returns bison's own code: 0 accepted, 1 rejected
 * (the grammar has no error-recovery rules, so a syntax error ends the parse
 * immediately), 2 memory exhausted. */
int mdbp_run_parser(mdbp_ctx *ctx);

/* Records the first error yyparse reports, together with the LR state on top
 * of the parser's own stack (-1 when unknown).  Called from inside yyparse
 * through the yyerror interception in parser_glue.c. */
void mdbp_note_error(mdbp_ctx *ctx, const char *msg, int state);

/* valid(T) FROM THE LIVE PARSE -- an `mdbp_valid_fn` (src/mdbp_lex.h) to be
 * handed straight to mdbp_lex_new().  `terminal_name` is the grammar model's
 * own spelling ("VALUES", "LEFT_PAREN_ALT", "'('"); `user` is unused and may
 * be NULL.  Answers 1 iff bison's own yy_lac() says that terminal can
 * eventually be shifted -- after any number of reductions -- from the parser
 * stack as it stands inside the yylex call that is running now.
 *
 * Answers 0, always, when no parse is running: outside yyparse there is no
 * stack to ask, which is the same answer `--valid none` gives and the same
 * one tools/vendor/bison2ts/lexdump.py's ReplayValid gives once its cursor
 * has derailed.  That is not a fallback, it is the honest answer.
 *
 * Answers 0 for a name that is not a terminal of this grammar (including
 * every character token the grammar does not declare).
 *
 * NOT a general-purpose predicate: it reads a file-static binding that only
 * src/parser_glue.c's yylex call-site interception sets, so it is meaningful
 * only from inside a lexer called by yyparse.  See src/parser_glue.c. */
int mdbp_valid_live(const char *terminal_name, void *user);

/* Bison TOKEN NUMBER -> terminal name, the inverse of mdbp_token_number().
 *
 * Implemented in src/parser_glue.c because that is the one translation unit
 * with bison's `yytranslate[]` in scope, and yytranslate is exactly this map:
 * external token number -> symbol kind, which mdbp_symbol_name() then names.
 * (A generated reverse table would have to re-derive what yytranslate
 * already IS, so the glue TU wins on having one source of truth.)
 *
 * Returns "$end" for 0 and the grammar model's own spelling for every
 * terminal it declares ("SELECT_SYM", "IDENT_QUOTED", "'('").  For a
 * CHARACTER token number the grammar does NOT declare it returns bison's own
 * spelling for that character, which is exactly reflex.py's char_token_name:
 * '\'' for ', '\\' for \, '\n', '\t', '\r', '<c>' for 0x20 <= c < 0x7f and
 * '\NNN' (three-digit octal) otherwise.  Those are real: 2,046 corpus records
 * carry a token name that is not a terminal of this grammar.  NULL when the
 * number is negative or is neither declared nor a character. */
const char *mdbp_name_for_token_number(int token_number);

/* --- src/mdbp_api.c (the two refusal rules, shared with the CLI) --------- */

/* 1 when `list` names an sql_mode this library refuses -- ORACLE or MSSQL,
 * which change the language and must not look honoured (PLAN.md S2.2) -- with
 * its canonical spelling written into `found`.  mdbp_parser_new() answers
 * NULL for such a list; the CLI calls this directly only so it can name the
 * offender in its own exit-3 diagnostic, off the same rule. */
int mdbp_sql_mode_refused(const char *list, char *found, size_t found_size);

/* 1 when `name` names a charset this library refuses -- everything but
 * utf8mb4 and its alias utf8mb4_general_ci (PLAN.md D-8, D-9) -- with the
 * offending spelling, exactly as given, written into `found`.  NULL means
 * the default and is never refused.  mdbp_parser_new() answers NULL for such
 * a name; the CLI calls this directly only so it can name the offender in
 * its own exit-3 diagnostic, off the same rule mdbp_sql_mode_refused() is. */
int mdbp_charset_refused(const char *name, char *found, size_t found_size);

/* 1 when `s`/`len` is valid UTF-8 by Python's strictness (no overlong forms,
 * no surrogates, nothing above U+10FFFF).  The connection charset is utf8mb4
 * (PLAN.md D-9): mdbp_parse_sql() refuses anything else as unsupported input,
 * and the CLI reports the same bytes as a usage error (exit 2) off this same
 * one implementation. */
int mdbp_input_is_utf8(const char *s, size_t len);

#endif /* MDBP_INTERNAL_H */
