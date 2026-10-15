/* src/stub.h -- the extracted parser's runtime contract, and nothing else.
 *
 * build/yy_stub.yy's whole prologue is the single line
 *
 *     %{ #include "stub.h" %}
 *
 * so this header is what bison's generated parser is compiled against.  Every
 * one of the grammar's 3,927 actions is `{ MDBP_R(<serial>); }` (PLAN.md
 * S2.1), `ctx` is the `%parse-param`/`%lex-param` name, and the value type is
 * `%define api.value.type {int}` -- so YYSTYPE is `int`.
 *
 * THE TRAP, recorded here because it costs a round otherwise: bison emits the
 * first user prologue BEFORE `#include "parser.h"`, so at the point this file
 * is read YYSTYPE does not exist yet.  Hence `yylex` is declared with `int *`
 * (which is what YYSTYPE is here) and this header must NOT include parser.h.
 * The reverse direction is fine and is why this works: parser.h says
 * `int yyparse (mdbp_ctx *ctx);`, which needs mdbp_ctx to be declared first --
 * and it is, right here, before parser.h is reached.
 */
#ifndef MDBP_STUB_H
#define MDBP_STUB_H

#include <stddef.h>

/* THE PUBLIC HEADER, and the ONE definition of mdbp_token.  It is safe here
 * even under the ordering trap above, because include/mdbparse.h includes nothing
 * of ours -- only <stddef.h> -- and declares no type this file or the
 * generated parser could collide with.  The token struct used to live in this
 * file; it moved out when the library surface was written, because one token
 * type with the byte offsets in it is the whole point (a second one is a
 * second place for an offset to be wrong). */
#include "mdbparse.h"

/* Where yylex takes the next token from when the token vector is not the
 * answer: `--valid live` (task 8.3) has the PARSER drive the lexer, exactly as
 * MariaDB does, because valid(T) can only be answered from a live parser
 * stack.  Returns 1 and fills *out (only `name` and `token_number` are read by
 * yylex), 0 at the end of the stream, -1 when the source failed; the caller
 * owns the failure and inspects its own state afterwards.
 *
 * It is declared HERE, beside mdbp_ctx, and not in src/mdbp_internal.h,
 * because it is a member of the struct below and this header is what bison's
 * generated parser is compiled against. */
typedef int (*mdbp_source_fn)(void *user, mdbp_token *out);

/* --- parsing_place: the ONE piece of parser state MariaDB's lexer reads ----
 *
 * `sql/sql_lex.cc`'s `lex_token` reads `curr_sel->parsing_place` at five
 * feedback sites and SPENDS it (sets NO_MATTER) when it uses it.  That whole
 * mechanism is MariaDB's own, compiled in verbatim through
 * `build/lexer_extracted.cc`.  What used to be missing was the WRITES: they
 * are grammar actions, and `tools/stub.py` blanks action bodies.  Task
 * P1.1's named rule `preserve_parsing_place` lifts them -- see that script,
 * and `build/stub-map.json`'s `preserve` block for the enumeration of every
 * lifted statement and every site it declined to lift.
 *
 * THESE TWO DECLARATIONS ARE OURS, NOT MariaDB's TEXT.  The enum's NAMES and
 * ORDER are `sql/sql_priv.h:327`'s, because the lifted statements and the
 * sliced lexer both spell them; the struct carries the two fields those
 * statements touch and NOTHING ELSE -- MariaDB's SELECT_LEX has hundreds.
 *
 * `src/lexshim.h` includes THIS header for them rather than declaring its own,
 * so `SELECT_LEX` is ONE type and `mdbp_ctx.current_select` and the sliced
 * lexer's `thd->lex->current_select` are THE SAME STORAGE.  See the join in
 * src/mdbp_lex.cc (`mdbp_lex_select`) and src/mdbp_api.c (`mdbp_parse_sql`). */
enum enum_parsing_place
{
    NO_MATTER,
    IN_HAVING,
    SELECT_LIST,
    IN_WHERE,
    IN_ON,
    IN_GROUP_BY,
    IN_ORDER_BY,
    IN_UPDATE_ON_DUP_KEY,
    IN_PART_FUNC,
    BEFORE_OPT_LIST,
    AFTER_LIST,
    FOR_LOOP_BOUND,
    IN_RETURNING,
    PARSING_PLACE_SIZE          /* always should be the last */
};

typedef struct SELECT_LEX {
    enum enum_parsing_place parsing_place;
    enum enum_parsing_place save_parsing_place;
} SELECT_LEX;

/* The parse context: the input token vector, the derivation buffer and the
 * error record.  One `mdbp_ctx` per record; P3's batch mode reuses one. */
typedef struct mdbp_ctx {
    /* --- the lifted actions' state (task P1.1) --------------------------- */
    /* `ctx` IS A SELF-POINTER, and it is spelled `ctx` on purpose.  The
     * lifted statements reach the field through five spellings, two of which
     * go through `thd->lex`; `build/yy_stub.yy`'s prologue resolves them with
     * `#define thd ctx` and `#define lex ctx` (tools/stub.py's
     * PARSING_PLACE_MACROS), so `thd->lex` is literally `ctx->ctx` and this
     * member is what it reads.  Set by mdbp_ctx_init(); an mdbp_ctx that is
     * COPIED rather than initialised in place would carry a stale one, which
     * is why nothing in this library ever copies one.
     *
     * `current_select` is never NULL: mdbp_ctx_init()/mdbp_ctx_reset() point
     * it at `own_select` below, and mdbp_parse_sql() re-points it at the
     * LEXER's own SELECT_LEX for the duration of a live parse, which is the
     * join that makes the parser's writes and the lexer's reads one field. */
    struct mdbp_ctx *ctx;
    SELECT_LEX *current_select;
    /* The fallback storage `current_select` points at when no lexer owns
     * one -- every `mdbp_parse_tokens()` parse.  The actions still run and
     * still write; nothing reads it. */
    SELECT_LEX own_select;

    /* --- input ---------------------------------------------------------- */
    const mdbp_token *tokens;
    size_t ntokens;
    /* NULL for the token-vector path (--tokens/--tokens-json/--jsonl with a
     * "tokens" array, and every static --valid mode), which is untouched by
     * task 8.3.  Non-NULL puts yylex on the pull path above; `tokens` and
     * `ntokens` are then unused and stay NULL/0, because the caller's vector
     * is grown (and reallocated) while the parse is running. */
    mdbp_source_fn source;
    void *source_user;
    /* how many tokens yylex has handed the parser (the synthetic
     * end-of-input is not one of them) */
    size_t cursor;
    /* index of the current lookahead in `tokens`; == ntokens once yylex has
     * returned the synthetic $end, which is what makes error.at_index the
     * same quantity as replay.py's error.token_index */
    size_t lookahead_index;

    /* --- the derivation ------------------------------------------------- */
    /* Bison rule numbers in reduce order.  GROWN DYNAMICALLY: the longest
     * derivation in corpus/expected/sakila.jsonl is 1,107 entries and the
     * corpus is not bounded by that, so a fixed cap would be a defect. */
    int *derivation;
    size_t nderivation;
    size_t derivation_cap;
    int want_derivation;
    /* set when a realloc failed: the run's status becomes "internal" */
    int internal_error;

    /* --- the error record ----------------------------------------------- */
    int error_seen;
    /* the LR state on top of the parser's own stack when the error was
     * reported, in build/grammar.output's numbering; -1 when unknown */
    int error_state;
    const char *error_message;
    /* every terminal that can still be SHIFTED from the parser stack as it
     * stood when the error was reported -- after any number of reductions,
     * which is bison's own yy_lac() under `%define parse.lac full` -- sorted
     * by name (strcmp order).  NOT this state's action row: at a state
     * carrying a default reduction the row is a strict subset of what the
     * grammar accepts next, and this field answers the larger, true set.
     *
     * NULL and nexpected == 0 are DIFFERENT answers.  NULL: the set could
     * not be computed (no automaton ran on this record, the shiftability
     * check itself ran out of memory -- a SHORT set is never reported in its
     * place -- or an allocation failed).  nexpected == 0 with `expected`
     * non-NULL: computed, and nothing at all can be shifted here.
     * See include/mdbparse.h's mdbp_error contract, which publishes the same
     * rule to library consumers. */
    const char **expected;
    size_t nexpected;
} mdbp_ctx;

/* Appends the RULE NUMBER `serial` maps to (through the generated
 * mdbp_serial_to_rule[] -- serial != rule number) to ctx->derivation.  Called
 * once per reduction, in reduce order, which is exactly what a derivation
 * is. */
void mdbp_reduce(mdbp_ctx *ctx, int serial);

#define MDBP_R(n) mdbp_reduce(ctx, (n))

/* YYSTYPE is `int`; see the trap above for why this is not spelled YYSTYPE. */
int yylex(int *lvalp, mdbp_ctx *ctx);
void yyerror(mdbp_ctx *ctx, const char *msg);

#endif /* MDBP_STUB_H */
