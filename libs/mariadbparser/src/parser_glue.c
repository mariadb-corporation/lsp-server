/* src/parser_glue.c -- the ONE translation unit that contains bison's
 * generated parser, and the only place that reads its tables.
 *
 * WHY IT #includes parser.c INSTEAD OF LINKING IT
 * -----------------------------------------------
 * A rejection has to carry `error_state` (the LR state on top of the parser's
 * own stack when the error was reported, in bison's own state numbering) and
 * the expected-token set.  Neither is reachable from outside yyparse:
 * bison's `yyerror(ctx, msg)` hook is handed no state, and the state stack
 * (`yyss`/`yyssp`) and the action tables (`yypact`/`yycheck`/`yytable`) are
 * function locals and file-static respectively.
 *
 * `%define parse.error custom` would hand the state over too, but it needs a
 * different bison invocation and a different generated parser for what is
 * only an error-reporting detail.  Instead this is one glue TU that
 * #includes bison's parser.c, so the tables are in scope, plus a two-line
 * interception of the `yyerror` call site -- which sits inside yyparse, where
 * `yyssp` is a live local:
 *
 *     yyerrlab:
 *       ...
 *           yyerror (ctx, YY_("syntax error"));         <- parser.c
 *
 * The function-like macro below rewrites exactly that call (and the identical
 * one in yyexhaustedlab) into a call that also passes `*yyssp`.  Bison's
 * generated file itself is untouched -- nothing bison writes into the build
 * directory is ever hand-edited: this is our own file changing how our own
 * hook is called.  `*yyssp` rather than `yystate` because the error state is
 * "the state on top of the parser's own stack", and yysetstate keeps the two
 * equal.
 *
 * The real `yyerror` is defined below the macro-free line, so it is a
 * definition and not a macro expansion; it exists so that stub.h's
 * declaration has a definition and so that any future caller outside yyparse
 * still records something sane.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mdbp_once.h"

#include "mdbp_internal.h"

/* Defined BEFORE the interception: with the macro in scope this text would
 * itself expand.  Nothing in yyparse reaches it. */
void yyerror(mdbp_ctx *ctx, const char *msg)
{
    mdbp_note_error(ctx, msg, -1);
}

/* The interception now does TWO things, and the second one is what makes the
 * expected set answerable from LAC at all.
 *
 * mdbp_collect_expected() (below) asks bison's own yy_lac() one question per
 * terminal, and yy_lac() needs the four yyparse locals the mdbp_lac binding
 * carries -- including yyssp, THE STACK POINTER AS IT STANDS NOW.  The
 * binding the yylex interception leaves behind is the one from the last
 * LEXER call, and that one is stale here: between reading the lookahead and
 * reporting the error the parser may have run a chain of reductions, each of
 * which moves yyssp (and may have reallocated the stack out from under the
 * old value).  Asking LAC with that pointer would answer for a state the
 * parser has already left.
 *
 * So the binding is refreshed HERE, at the error call site, where yyssp is
 * the same live local the `(int) *yyssp` argument below reads -- the state
 * the error is reported for and the state the set must describe are then the
 * same state by construction rather than by coincidence.  It is the SAME
 * binding (src/parser_glue.c's one mdbp_lac), rebound; a second binding for
 * the error path is exactly the duplicate this file already refuses to
 * create.
 *
 * All three yyerror call sites bison generates are inside yyparse -- the
 * yyerrlab "syntax error", the yyexhaustedlab "memory exhausted", and the
 * YYBACKUP macro's "cannot back up" (never expanded here: every action body
 * is a stub) -- so yyesa/yyes/yyes_capacity/yyssp are in scope at each. */
#define yyerror(yyctxarg, yymsgarg)                                     \
    (mdbp_lac_bind((void *) yyesa, (void *) &yyes,                      \
                   (void *) &yyes_capacity, (void *) yyssp),            \
     mdbp_note_error((yyctxarg), (yymsgarg), (int) *yyssp))

/* --- valid(T): the second call site inside yyparse ------------------------
 *
 * WHAT IS BEING SOLVED.  MariaDB's lexer rewrites tokens from
 * `curr_sel->parsing_place`, a field the parser's grammar actions set.  Those
 * writes are preserved in generated/yy_stub.yy, but the `(` arm at
 * sql_lex.cc:2004 also consumes the field (sql_lex.cc:2013), which a parser
 * state cannot model by itself.  So the lexer (mdbp_downgrade_left_paren in
 * src/mdbp_lex.cc) emits the token it chose only when valid(T) holds -- "is
 * terminal T shiftable in the parser's current LR state, after any number of
 * reductions".  The answer comes from bison's own `yy_lac()`, under
 * `%define parse.lac full` (which CMake passes to bison as
 * `-Dparse.lac=full`; without it the four names below do not exist and this
 * file does not compile -- a deliberate tripwire).  The cheaper answer is
 * wrong: at an inconsistent state carrying a default reduction, a walk of the
 * state's yypact/yycheck/yytable action row answers a strict subset ({F}
 * where LAC answers {C,E,F}), and bison's own verbose message names LAC's set.
 *
 * HOW IT IS REACHED.  yy_lac() is `static`, and its four arguments -- yyesa,
 * yyes, yyes_capacity, yyssp -- are LOCALS of yyparse.  #including the
 * generated parser makes yy_lac visible; it does not make those locals
 * reachable.  This is the same problem the yyerror interception above solves
 * for `*yyssp`, and it gets the same solution applied to the skeleton's OTHER
 * in-yyparse call site, the one a lexer actually needs:
 *
 *     yychar = yylex (&yylval, ctx);            <- parser.c
 *
 * The macro below rewrites that one call into "bind first, then lex".  The
 * comma expression yields the lexer's value, so `yychar = yylex (...)` still
 * assigns the token; the binding is therefore in place for the whole of the
 * lexer call, which is exactly when the lexer asks valid(T).  Bison's output
 * is untouched: this is our own file changing how OUR OWN hook is called.
 * The macro is variadic so it does not depend on the lexer's arity, and
 * `yylex (__VA_ARGS__)` inside it is not re-expanded (C99 6.10.3.4p2), so the
 * real yylex -- declared in src/stub.h, which is read ABOVE this line and
 * therefore never expands -- is what gets called.
 *
 * The binding is four `void *` because these lines are read before
 * yy_state_t and YYPTRDIFF_T exist; they are cast back below the #include,
 * where those types and yy_lac itself are in scope.
 *
 * LIFETIME.  Bound before every yylex call (the stacks may have been
 * reallocated since the last one) and cleared by mdbp_run_parser() when
 * yyparse returns, because the locals die with it.  With no binding,
 * mdbp_valid_live() answers 0 for everything -- the honest answer when there
 * is no parser stack to ask.
 *
 * yy_lac() only reads the state stack; its scratch is the yyes array and its
 * capacity, which it may reallocate and which are passed by address so
 * yyparse sees the new values (and frees them on the way out).  It does not
 * touch `yy_lac_established`, so the parser's own LAC bookkeeping is
 * unaffected: asking valid(T) cannot change the parse.
 *
 * The binding belongs to one parse, so it is `_Thread_local` -- one binding
 * per thread instead of one shared across every thread's concurrent parse. */
static _Thread_local struct {
    int bound;
    void *esa;      /* yyesa            */
    void *es;       /* &yyes            */
    void *cap;      /* &yyes_capacity   */
    void *ssp;      /* yyssp            */
} mdbp_lac;

static void mdbp_lac_bind(void *esa, void *es, void *cap, void *ssp)
{
    mdbp_lac.bound = 1;
    mdbp_lac.esa = esa;
    mdbp_lac.es = es;
    mdbp_lac.cap = cap;
    mdbp_lac.ssp = ssp;
}

static void mdbp_lac_unbind(void)
{
    mdbp_lac.bound = 0;
    mdbp_lac.esa = NULL;
    mdbp_lac.es = NULL;
    mdbp_lac.cap = NULL;
    mdbp_lac.ssp = NULL;
}

#define yylex(...)                                                      \
    (mdbp_lac_bind((void *) yyesa, (void *) &yyes,                      \
                   (void *) &yyes_capacity, (void *) yyssp),            \
     yylex(__VA_ARGS__))

/* bison's output directory in the build tree is on the include path.  This is
 * the generated parser; it is compiled here and nowhere else, so exactly one
 * object file contains it. */
#include "parser.c"

#undef yyerror
#undef yylex

/* generated/yy_stub.yy's prologue defines these five (tools/stub.py's
 * PARSING_PLACE_MACROS) so that the `parsing_place` statements its
 * `preserve_parsing_place` rule lifted out of sql_yacc.yy compile unmodified.
 * They are macros on ORDINARY-LOOKING names, so they stop at the end of the
 * generated parser rather than leaking into the rest of this translation
 * unit.  Undefining a macro that was never defined is legal C and costs
 * nothing, so this does not depend on the prologue's exact content. */
#undef thd
#undef lex
#undef Lex
#undef Select
#undef sel

/* --- bison's own tables, for src/mdbp_runtime.c ---------------------------
 *
 * The token-name table and the rule numbers are read from the generated
 * parser itself (bison -k emits yytname[]), so a hand edit of
 * generated/yy_stub.yy can never leave a second, stale table behind. */

int mdbp_yy_ntokens(void)
{
    return YYNTOKENS;
}

int mdbp_yy_nrules(void)
{
    return YYNRULES;
}

int mdbp_yy_maxutok(void)
{
    return YYMAXUTOK;
}

int mdbp_yy_translate(int token_number)
{
    return (int) YYTRANSLATE(token_number);
}

const char *mdbp_yy_tname(int kind)
{
    if (kind < 0 || kind >= YYNTOKENS) {
        return NULL;
    }
    return yytname[kind];
}

/* --- valid(T), answered from the binding above --------------------------- */

int mdbp_valid_live(const char *terminal_name, void *user)
{
    int number;
    int kind;
    int rc;

    (void) user;
    if (!mdbp_lac.bound || terminal_name == NULL) {
        return 0;
    }
    /* Terminal NAME -> bison token number -> symbol kind, through the pair
     * src/mdbp_internal.h already owns: mdbp_token_number() is the generated
     * table's binary search and yytranslate[] is bison's own external-number
     * -> kind map, which is what mdbp_name_for_token_number() inverts below.
     * No third table: a second name->number map is how an off-by-one against
     * 800 terminals gets in.  Not a terminal of this grammar -> 0. */
    number = mdbp_token_number(terminal_name);
    if (number < 0) {
        return 0;
    }
    kind = (int) YYTRANSLATE(number);
    if (kind == YYSYMBOL_YYUNDEF || kind == YYSYMBOL_YYerror) {
        return 0;
    }
    rc = yy_lac((yy_state_t *) mdbp_lac.esa, (yy_state_t **) mdbp_lac.es,
                (YYPTRDIFF_T *) mdbp_lac.cap, (yy_state_t *) mdbp_lac.ssp,
                (yysymbol_kind_t) kind);
    /* 0 shiftable, 1 not, YYENOMEM exhausted.  Exhaustion is reported as "not
     * shiftable" rather than guessed at: the lexer reads the predicate as a
     * gate, so a false answer is the conservative one (it spells the plain
     * `'('`, as a lexer with no parser would), and yyparse will hit the same
     * exhaustion on its own next LAC check and end the parse with bison's
     * own memory-exhausted code, which mdbp_parse_sql() reports as
     * MDBP_INTERNAL.  Nothing is silently absorbed. */
    return rc == 0;
}

/* --- the expected-token set ---------------------------------------------- */

static int mdbp_name_cmp(const void *a, const void *b)
{
    const char *const *left = (const char *const *) a;
    const char *const *right = (const char *const *) b;
    return strcmp(*left, *right);
}

/* EVERY TERMINAL THAT CAN STILL BE SHIFTED FROM HERE, asked of bison's own
 * yy_lac() one terminal at a time -- not the yypact/yycheck/yytable action
 * row this used to walk.
 *
 * WHY THE ROW WAS THE WRONG SOURCE.  The row answers "what does this state
 * have an action for", and at an INCONSISTENT state -- one carrying a default
 * reduction -- that is a strict subset of what the grammar accepts next: the
 * terminals reachable only THROUGH that reduction have no entry in the row
 * and were therefore left out ({F} where LAC answers {C,E,F}, as for valid(T)
 * above).  For a consumer that reads this field as a completion candidate set
 * that is not a smaller answer, it is a wrong one: a missing suggestion is
 * indistinguishable from "nothing applies".
 *
 * The candidate set is every terminal kind of the grammar, and everything
 * else about this function is what it always was: one malloc of parser-owned
 * memory (freed by mdbp_ctx_reset/mdbp_ctx_free), YYSYMBOL_YYerror excluded,
 * kinds mdbp_symbol_name() cannot name excluded, sorted by name with qsort
 * so a caller can bsearch it.  YYSYMBOL_YYUNDEF needs no case of its own:
 * yy_lac() answers "always an error" for it by its own first rule, so it
 * cannot reach the set (the same single source of truth mdbp_valid_live()
 * leans on rather than re-deriving).
 *
 * THE ALLOCATION IS YYNTOKENS ENTRIES, ONCE, not a count-then-fill pass:
 * every candidate costs a yy_lac() call, and counting first would double the
 * per-keystroke cost to learn a number the fill pass already knows.
 * At most YYNTOKENS pointers are over-allocated for one rejection, which the
 * next mdbp_ctx_reset() gives back.
 *
 * EXHAUSTION IS NOT A SHORT ANSWER.  mdbp_valid_live() treats YYENOMEM as
 * "not shiftable" because there it is a per-terminal gate and a false answer
 * is the conservative one.  HERE the same policy would be a defect: a set
 * missing the terminals whose LAC checks ran out of memory looks exactly like
 * a complete set, and a caller has no way to tell.  So exhaustion makes the
 * WHOLE set could-not-compute -- ctx->expected stays NULL, which
 * src/mdbparse.cc renders as `"expected_sample": null` -- and nothing is
 * guessed at.  The
 * same holds for the two other ways the set cannot be computed: a state
 * outside the automaton (every MDBP_UNSUPPORTED record, state -1) and no live
 * LAC binding (no parse is running, so there is no stack to ask).
 *
 * A COMPUTED EMPTY SET IS NOT THE SAME ANSWER.  If every terminal is
 * genuinely unshiftable the allocation is kept and nexpected is 0, so
 * src/mdbparse.cc renders `[]` -- "computed, nothing applies" -- and a caller
 * can tell the two apart.  include/mdbparse.h states that contract; this is
 * where it is produced. */
static void mdbp_collect_expected(mdbp_ctx *ctx, int state)
{
    int kind;
    size_t count = 0;
    const char **names;

    if (state < 0 || state >= YYNSTATES || !mdbp_lac.bound) {
        return;
    }
    names = (const char **) malloc((size_t) YYNTOKENS * sizeof *names);
    if (names == NULL) {
        return;
    }
    for (kind = 0; kind < YYNTOKENS; ++kind) {
        const char *name;
        int rc;

        if (kind == YYSYMBOL_YYerror) {
            continue;
        }
        name = mdbp_symbol_name(kind);
        if (name == NULL) {
            continue;
        }
        rc = yy_lac((yy_state_t *) mdbp_lac.esa, (yy_state_t **) mdbp_lac.es,
                    (YYPTRDIFF_T *) mdbp_lac.cap, (yy_state_t *) mdbp_lac.ssp,
                    (yysymbol_kind_t) kind);
        if (rc == YYENOMEM) {
            free(names);
            return;
        }
        if (rc == 0) {
            names[count++] = name;
        }
    }
    qsort(names, count, sizeof *names, mdbp_name_cmp);
    ctx->expected = names;
    ctx->nexpected = count;
}

void mdbp_note_error(mdbp_ctx *ctx, const char *msg, int state)
{
    if (ctx->error_seen) {
        return;
    }
    ctx->error_seen = 1;
    ctx->error_state = state;
    ctx->error_message = msg;
    mdbp_collect_expected(ctx, state);
}

int mdbp_run_parser(mdbp_ctx *ctx)
{
    int rc;

    /* The binding is set by the interception above, per yylex call; it is
     * cleared HERE because yyesa/yyes/yyes_capacity/yyssp are yyparse's own
     * locals and do not outlive this return.  A lexer that keeps running
     * afterwards -- mdbp_parse_sql() drains the rest of the token stream once
     * the parse has ended -- therefore gets valid(T) == 0 from then on. */
    rc = yyparse(ctx);
    mdbp_lac_unbind();
    return rc;
}

/* --- token number -> name (src/mdbp_internal.h) --------------------------- */

/* bison's own spelling of the character token `c`, built once into a static
 * table so mdbp_name_for_token_number() can return a string that outlives the
 * call (its contract says the name is static).  256 entries of at most
 * "'\\377'" + NUL = 7 bytes.
 *
 * The rule is bison's own spelling of a character literal: the five
 * escapes it spells with a backslash, printable ASCII as itself, everything
 * else as three-digit octal.  0 never reaches here (it is "$end"). */
static char mdbp_char_names[256][8];

/* This table is written exactly once, lazily, on whichever thread first asks
 * for a character token's name.  A plain `if (!built) { build(); built = 1; }`
 * would let two threads entering mdbp_name_for_token_number() at the same
 * moment both run it, each writing all 256 entries while the other reads
 * them: a reader could be handed a half-written string, and the flag's
 * unsynchronised load/store is itself a data race.
 *
 * mdbp_once() (src/mdbp_once.h) is the guard rather than a hand-rolled
 * atomic flag because it establishes the happens-before edge a
 * double-checked flag does not -- every thread that returns from mdbp_once()
 * sees the completed table, not merely a set flag.
 *
 * mdbp_char_names itself remains a shared, process-wide table.  That is the
 * point: after this one initialisation it is READ-ONLY for the rest of the
 * process, which is exactly the contract include/mdbparse.h section 3
 * states. */
static mdbp_once_t mdbp_char_names_once = MDBP_ONCE_INIT;

static void mdbp_build_char_names(void)
{
    static const struct { int c; const char *name; } escaped[] = {
        { '\'',  "'\\''" },
        { '\\',  "'\\\\'" },
        { '\n',  "'\\n'" },
        { '\t',  "'\\t'" },
        { '\r',  "'\\r'" },
    };
    size_t k;
    int c;

    for (c = 0; c < 256; c++) {
        if (c >= 0x20 && c < 0x7f) {
            snprintf(mdbp_char_names[c], sizeof mdbp_char_names[0], "'%c'", c);
        } else {
            snprintf(mdbp_char_names[c], sizeof mdbp_char_names[0], "'\\%03o'",
                     (unsigned) c);
        }
    }
    for (k = 0; k < sizeof escaped / sizeof escaped[0]; k++) {
        snprintf(mdbp_char_names[escaped[k].c], sizeof mdbp_char_names[0],
                 "%s", escaped[k].name);
    }
}

const char *mdbp_name_for_token_number(int token_number)
{
    int kind;

    if (token_number < 0) {
        return NULL;
    }
    if (token_number == 0) {
        return "$end";
    }
    if (token_number <= YYMAXUTOK) {
        kind = yytranslate[token_number];
        /* YYSYMBOL_YYUNDEF is what yytranslate answers for every number the
         * grammar does not declare, including undeclared character tokens. */
        if (kind != YYSYMBOL_YYUNDEF) {
            const char *name = mdbp_symbol_name(kind);

            if (name != NULL) {
                return name;
            }
        }
    }
    if (token_number > 0 && token_number < 256) {
        mdbp_once(&mdbp_char_names_once, mdbp_build_char_names);
        return mdbp_char_names[token_number];
    }
    return NULL;
}
