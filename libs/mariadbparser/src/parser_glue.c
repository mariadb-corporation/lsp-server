/* src/parser_glue.c -- the ONE translation unit that contains bison's
 * generated parser, and the only place that reads its tables.
 *
 * WHY IT #includes build/parser.c INSTEAD OF LINKING IT
 * ----------------------------------------------------
 * The verdict object has to carry `error.state` (the LR state on top of the
 * parser's own stack when the error was reported, in build/grammar.output's
 * numbering) and `error.expected_sample`, because P4 diffs both against
 * replay.py's error object.  Neither is reachable from outside yyparse:
 * bison's `yyerror(ctx, msg)` hook is handed no state, and the state stack
 * (`yyss`/`yyssp`) and the action tables (`yypact`/`yycheck`/`yytable`) are
 * function locals and file-static respectively.
 *
 * PLAN.md task 3.1 lists three routes.  Route (i), `%define parse.error
 * custom`, needs a different bison invocation, which drags P1's whole
 * evidence set (extract.json, regen.json, convergence.json, trajectory.jsonl)
 * along with it and puts `automaton_differences == 0` at risk for an
 * error-reporting detail.  This is route (ii): one glue TU that #includes
 * build/parser.c, so the tables are in scope, plus a two-line interception of
 * the `yyerror` CALL SITE -- which sits inside yyparse, where `yyssp` is a
 * live local:
 *
 *     yyerrlab:
 *       ...
 *           yyerror (ctx, YY_("syntax error"));         <- build/parser.c
 *
 * The function-like macro below rewrites exactly that call (and the identical
 * one in yyexhaustedlab) into a call that also passes `*yyssp`.  Bison's
 * generated file itself is untouched -- the house rule is that no artefact in
 * build/ is ever hand-edited, and none is: this is our own file changing how
 * OUR OWN hook is called.  `*yyssp` rather than `yystate` because the phase
 * asks for "the state on top of the parser's own stack" and because yysetstate
 * keeps the two equal.
 *
 * The real `yyerror` is defined below the macro-free line, so it is a
 * definition and not a macro expansion; it exists so that stub.h's
 * declaration has a definition and so that any future caller outside yyparse
 * still records something sane.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

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

/* --- valid(T): the SECOND call site inside yyparse ------------------------
 *
 * WHAT IS BEING SOLVED.  MariaDB's lexer rewrites tokens from
 * `curr_sel->parsing_place`, a field parser ACTIONS set; every action body is
 * a stub here, so that field can never be set and PLAN.md S1.8's substitute is
 * valid(T) -- "is terminal T shiftable in the parser's current LR state, after
 * any number of reductions".  evidence/P5a/valid-symbol.json settled how to
 * answer it: bison's own `yy_lac()`, under `%define parse.lac full` (which
 * tools/extract.sh passes as `-Dparse.lac=full`; without it the four
 * names below do not exist and this file does not compile -- a deliberate
 * tripwire).  The same evidence shows why the CHEAP answer is wrong: at an
 * INCONSISTENT state carrying a default reduction, the action-row walk
 * mdbp_collect_expected() does below answers a strict subset ({F} where LAC
 * answers {C,E,F}), and bison's own verbose message names LAC's set.
 *
 * HOW IT IS REACHED.  yy_lac() is `static`, and its four arguments -- yyesa,
 * yyes, yyes_capacity, yyssp -- are LOCALS of yyparse.  #including the
 * generated parser makes yy_lac visible; it does not make those locals
 * reachable.  This is the same problem the yyerror interception above solves
 * for `*yyssp`, and it gets the same solution applied to the skeleton's OTHER
 * in-yyparse call site, the one a lexer actually needs:
 *
 *     yychar = yylex (&yylval, ctx);            <- build/parser.c
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
 * is no parser stack to ask, and the same one lexdump.py's ReplayValid gives
 * after its cursor derails.
 *
 * yy_lac() only READS the state stack; its scratch is the yyes array
 * and its capacity, which it may reallocate and which are passed by address so yyparse sees the
 * new values (and frees them on the way out).  It does not touch
 * `yy_lac_established`, so the parser's own LAC bookkeeping is unaffected --
 * evidence/P5a/valid-symbol.json's control run measured exactly that: every
 * stream reached the same verdict with the probe compiled out.
 *
 * PLAN.md P2.1: classified per_parse (evidence/P7/globals.json), so it is
 * `_Thread_local` -- one binding per thread instead of one shared across
 * every thread's concurrent parse.  No signature change, no struct member
 * added: the type and every access below are unchanged, only the storage
 * duration is. */
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

/* -I build puts this on the include path.  It is the generated parser; it is
 * compiled here and nowhere else, so exactly one object file contains it. */
#include "parser.c"

#undef yyerror
#undef yylex

/* build/yy_stub.yy's prologue defines these five (tools/stub.py's
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
     * shiftable" rather than guessed at: the lexer's five sites all read the
     * predicate as a gate, so a false answer is the conservative one (it
     * spells the token the way `--valid none` would), and yyparse will hit
     * the same exhaustion on its own next LAC check and end the parse with
     * bison's own memory-exhausted code, which the CLI reports as
     * "status": "internal".  Nothing is silently absorbed. */
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
 * and were therefore left out.  evidence/P5a/valid-symbol.json settled this
 * for valid(T) already ({F} where LAC answers {C,E,F}); evidence/P8/
 * expected-baseline.json measures what it cost `expected`: over 1,605 caret
 * positions cut out of corpus statements, 811 of them answered a strict
 * subset of the LAC set, 192,517 shiftable terminals missing in total, and
 * `CREATE ` answered one terminal where twenty-four are shiftable.  For the
 * consumer that reads this field as a completion candidate set that is not a
 * smaller answer, it is a WRONG one: a missing suggestion is
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
 * measured per-keystroke cost to learn a number the fill pass already knows.
 * At most YYNTOKENS pointers are over-allocated for one rejection, which the
 * next mdbp_ctx_reset() gives back.
 *
 * EXHAUSTION IS NOT A SHORT ANSWER.  mdbp_valid_live() treats YYENOMEM as
 * "not shiftable" because there it is a per-terminal gate and a false answer
 * is the conservative one.  HERE the same policy would be a defect: a set
 * missing the terminals whose LAC checks ran out of memory looks exactly like
 * a complete set, and a caller has no way to tell.  So exhaustion makes the
 * WHOLE set could-not-compute -- ctx->expected stays NULL, which the CLI
 * renders as `"expected_sample": null` -- and nothing is guessed at.  The
 * same holds for the two other ways the set cannot be computed: a state
 * outside the automaton (every MDBP_UNSUPPORTED record, state -1) and no live
 * LAC binding (no parse is running, so there is no stack to ask).
 *
 * A COMPUTED EMPTY SET IS NOT THE SAME ANSWER.  If every terminal is
 * genuinely unshiftable the allocation is kept and nexpected is 0, so the
 * CLI renders `[]` -- "computed, nothing applies" -- and the caller can tell
 * the two apart.  include/mdbparse.h states that contract; this is where it is
 * produced. */
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
     * afterwards -- `--valid live` drains the rest of the token stream once
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
 * The rule is reflex.py's char_token_name(), which is bison's: the five
 * escapes it spells with a backslash, printable ASCII as itself, everything
 * else as three-digit octal.  0 never reaches here (it is "$end"). */
static char mdbp_char_names[256][8];

/* PLAN.md P4.1 deliverable 1.  This table is written exactly once, lazily, on
 * whichever thread first asks for a character token's name -- and before this
 * guard it was a plain `if (!built) { build(); built = 1; }`, which two
 * threads entering mdbp_name_for_token_number() at the same moment run BOTH,
 * each writing all 256 entries while the other reads them.  The reader could
 * therefore be handed a half-written string, and the flag's unsynchronised
 * load/store is itself a data race.
 *
 * C11 call_once() is the fix, and it is the mechanism rather than a
 * hand-rolled atomic flag for two reasons: it is what the phase prescribes
 * for the C side, and it establishes the happens-before edge a
 * double-checked flag does not -- every thread that returns from call_once()
 * sees the completed table, not merely a set flag.  `once_flag` lives in
 * libc (glibc >= 2.34), so nothing new is linked: `library_links_only_libc`
 * stays true.
 *
 * mdbp_char_names itself remains a shared, process-wide table.  That is the
 * point: after this one initialisation it is READ-ONLY for the rest of the
 * process, which is exactly the contract include/mdbparse.h section 3 now
 * states. */
static once_flag mdbp_char_names_once = ONCE_FLAG_INIT;

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
        call_once(&mdbp_char_names_once, mdbp_build_char_names);
        return mdbp_char_names[token_number];
    }
    return NULL;
}
