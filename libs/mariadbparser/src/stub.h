/* src/stub.h -- the extracted parser's runtime contract, and nothing else.
 *
 * generated/yy_stub.yy's prologue is
 *
 *     %{ #include "stub.h" ... %}
 *
 * followed only by the parsing_place macros (see mdbp_ctx.ctx below), so this
 * header is what bison's generated parser is compiled against.  Every one of
 * the grammar's actions is `{ MDBP_R(<serial>); }`, `ctx` is the
 * `%parse-param`/`%lex-param` name, and the value type is
 * `%define api.value.type {int}` -- so YYSTYPE is `int`.
 *
 * THE TRAP, recorded here because it is easy to fall into: bison emits the
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
 * answer: mdbp_parse_sql() has the PARSER drive the lexer, exactly as
 * MariaDB does, because valid(T) -- can the parser shift terminal T here? --
 * can only be answered from a live parser stack.  Returns 1 and fills *out (only `name` and `token_number` are read by
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
 * `generated/lexer_extracted.cc`.  The WRITES are grammar actions, and
 * `tools/stub.py` blanks action bodies; its named rule
 * `preserve_parsing_place` lifts them out of the blanking -- see that script,
 * and UPSTREAM.md for the rule that keeps them when the grammar is edited by
 * hand.
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
 * error record.  One `mdbp_ctx` per record; an mdbp_parser holds one and
 * reuses it for every call (mdbp_ctx_reset()). */
typedef struct mdbp_ctx {
    /* --- the lifted actions' state --------------------------------------- */
    /* `ctx` IS A SELF-POINTER, and it is spelled `ctx` on purpose.  The
     * lifted statements reach the field through five spellings, two of which
     * go through `thd->lex`; `generated/yy_stub.yy`'s prologue resolves them with
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
    /* NULL for the token-vector path, mdbp_parse_tokens().  Non-NULL
     * (mdbp_parse_sql()) puts yylex on the pull path above; `tokens` and
     * `ntokens` are then unused and stay NULL/0, because the caller's vector
     * is grown (and reallocated) while the parse is running. */
    mdbp_source_fn source;
    void *source_user;
    /* how many tokens yylex has handed the parser (the synthetic
     * end-of-input is not one of them) */
    size_t cursor;
    /* index of the current lookahead in `tokens`; == ntokens once yylex has
     * returned the synthetic $end, which is what makes
     * mdbp_result.error_token_index equal ntokens at end of input */
    size_t lookahead_index;

    /* --- the derivation ------------------------------------------------- */
    /* Bison rule numbers in reduce order.  GROWN DYNAMICALLY: a derivation's
     * length is bounded only by the input's, so a fixed cap would be a
     * defect. */
    int *derivation;
    /* derivation_depth[i]: symbols on the parser's stack when derivation[i]
     * was reduced, the rule's own right-hand side included.  Same length
     * and capacity as `derivation`; lets a caller see how many tokens were
     * shifted between two reductions, and so rebuild the parse tree. */
    int *derivation_depth;
    size_t nderivation;
    size_t derivation_cap;
    int want_derivation;
    /* set when a realloc failed: the run's status becomes "internal" */
    int internal_error;

    /* --- the error record ----------------------------------------------- */
    int error_seen;
    /* the LR state on top of the parser's own stack when the error was
     * reported, in bison's own state numbering; -1 when unknown */
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

/* Appends bison rule number `rule` to ctx->derivation.  Called once per
 * reduction, in reduce order, which is exactly what a derivation is. */
void mdbp_reduce(mdbp_ctx *ctx, int rule, int depth);

/* Inside a bison action `yyn` is the reduced rule's number plus one (bison's
 * yacc.c skeleton numbers its `case` labels that way), so the derivation
 * records bison's own rule number with no table in between, and the stack
 * depth (yyssp - yyss: the symbols on the stack, the rule's right-hand side
 * still on it, because bison runs an action before popping).  The serial `n`
 * that tools/stub.py writes stays in the grammar text as a reader's aid. */
#define MDBP_R(n) ((void) (n), mdbp_reduce(ctx, yyn - 1, (int) (yyssp - yyss)))

/* YYSTYPE is `int`; see the trap above for why this is not spelled YYSTYPE. */
int yylex(int *lvalp, mdbp_ctx *ctx);
void yyerror(mdbp_ctx *ctx, const char *msg);

#endif /* MDBP_STUB_H */
