/* src/mdbp_api.c -- include/mdbparse.h's implementation, and THE one
 * implementation of "parse one record".
 *
 * Everything above it is a client: src/mdbparse.cc (the CLI) reads arguments
 * and input and writes JSON through the public API only, so every actual
 * parse it performs comes through mdbp_parse_tokens() / mdbp_parse_sql()
 * below.  Its `--lex-only` comes through mdbp_lex_sql() or
 * mdbp_lex_sql_prefix(), the two calls here that lex without parsing.  Two
 * implementations of "parse one record" is the defect this file exists to
 * prevent -- one of them always drifts, and the one that drifts is the one
 * nobody measures.
 *
 * WHAT LIVES HERE AND WHY.  The parser object owns:
 *   - the parse context (src/mdbp_internal.h), initialised ONCE and re-pointed
 *     per record, because its derivation buffer's capacity is the thing that
 *     makes a large batch run cost one growth curve rather than one per
 *     record;
 *   - the token array a result reports, grown and reused for the same reason;
 *   - the LEXER of the last mdbp_parse_sql(), mdbp_lex_sql() or
 *     mdbp_lex_sql_prefix() call, deliberately NOT freed when that call
 *     returns: a lexed token's `text`
 *     points into the lexer's own copy of the statement, and mdbparse.h
 *     promises the result stays valid until the next call that fills a
 *     result.  So the lexer's lifetime IS that promise.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mdbparse.h"
#include "mdbp_internal.h"
#include "mdbp_lex.h"

/* The end-of-input symbol.  Token number 0, bison's own end-of-input
 * pseudo-terminal, and what ends every parse that is handed a token vector. */
#define MDBP_END_TOKEN "$end"

#define MDBP_STRINGIFY_(x) #x
#define MDBP_STRINGIFY(x) MDBP_STRINGIFY_(x)
/* Built from the macros rather than written out, so the string and the three
 * numbers cannot disagree. */
#define MDBP_VERSION_STRING            \
    MDBP_STRINGIFY(MDBP_VERSION_MAJOR) \
    "." MDBP_STRINGIFY(MDBP_VERSION_MINOR) "." MDBP_STRINGIFY(MDBP_VERSION_PATCH)

struct mdbp_parser {
    int prepare_mode;
    int want_derivation;
    /* The sql_mode list, converted ONCE (mdbp_lex_sql_mode_bits()) and
     * handed to every lexer this parser opens. */
    unsigned long long sql_mode_bits;
    char *charset;               /* an owned copy; never NULL */
    /* The release id versioned comments are judged against:
     * MDBP_LEX_DEFAULT_VERSION_ID until mdbp_parser_set_version_id() stores
     * another, always inside MDBP_LEX_VERSION_ID_MIN..MAX.  It lives HERE,
     * in the object, so it travels with the parser when a caller hands it
     * to another thread. */
    unsigned long version_id;

    mdbp_ctx ctx;                /* initialised once, reset per record */

    /* The token array the result reports.  Grown, never shrunk: a batch
     * caller pays for the largest record it has seen, once. */
    mdbp_token *tokens;
    size_t ntokens;
    size_t tokens_cap;

    /* The lexer of the last mdbp_parse_sql(), mdbp_lex_sql() or
     * mdbp_lex_sql_prefix(); see the file comment. */
    mdbp_lexer *lx;

    /* The stack depth per reduction of the last result's derivation, or
     * NULL; see mdbp_derivation_depths(). */
    const int *depths;
};

const char *mdbp_version_string(void)
{
    return MDBP_VERSION_STRING;
}

void mdbp_options_init(mdbp_options *opts)
{
    if (opts == NULL) {
        return;
    }
    opts->sql_mode = NULL;
    opts->prepare_mode = 1;
    opts->variant = MDBP_VARIANT_MARIADB;
    opts->want_derivation = 1;
    opts->charset = "utf8mb4";
}

/* --- the two refusals, in one place each --------------------------------- */

/* ORACLE and MSSQL change the language and this library cannot honour them,
 * so a list naming either is refused rather than silently ignored.
 * Case-insensitive, element by element; `found` receives the canonical
 * spelling of the offender so a caller can name it.
 *
 * mdbp_parser_new() refuses through this function; the lexer's own refusal
 * (src/mdbp_lex.h) is only a defensive backstop.  The rule is stated once,
 * here, or one statement of it drifts. */
int mdbp_sql_mode_refused(const char *list, char *found, size_t found_size)
{
    const char *p = list == NULL ? "" : list;

    while (*p != '\0') {
        const char *begin = p;
        size_t n;

        while (*p != '\0' && *p != ',') {
            p++;
        }
        n = (size_t) (p - begin);
        if (n == 6 && strncasecmp(begin, "ORACLE", 6) == 0) {
            snprintf(found, found_size, "ORACLE");
            return 1;
        }
        if (n == 5 && strncasecmp(begin, "MSSQL", 5) == 0) {
            snprintf(found, found_size, "MSSQL");
            return 1;
        }
        if (*p == ',') {
            p++;
        }
    }
    return 0;
}

/* The charset parameter, modelled directly on mdbp_sql_mode_refused()
 * above.  Exactly one name is accepted -- utf8mb4 -- plus the alias
 * utf8mb4_general_ci, which resolves to the SAME table (src/mdbp_lex.cc), so
 * accepting it is not accepting a second behaviour.  Every other name is
 * refused BY NAME, including utf8, utf8mb3 and latin1: those lex differently,
 * and an alias that silently changed behaviour is exactly the failure mode
 * refusing by name prevents.  `found` gets the offending spelling as the caller
 * gave it (there is no fixed list to canonicalise against, unlike ORACLE/
 * MSSQL above).  NULL means the default and is never refused. */
int mdbp_charset_refused(const char *name, char *found, size_t found_size)
{
    if (name == NULL) {
        return 0;
    }
    if (strcasecmp(name, "utf8mb4") == 0
        || strcasecmp(name, "utf8mb4_general_ci") == 0) {
        return 0;
    }
    snprintf(found, found_size, "%s", name);
    return 1;
}

/* The connection charset is utf8mb4, so a statement has to be valid UTF-8
 * whichever channel it arrives through, and undecodable bytes are refused
 * rather than lexed as something else.  The check is strict: no overlong
 * forms, no surrogates, nothing above U+10FFFF.
 *
 * Shared by mdbp_parse_sql() and the two lexing calls for the same reason
 * mdbp_sql_mode_refused() is one function: one rule, one implementation. */
int mdbp_input_is_utf8(const char *s, size_t len)
{
    const unsigned char *p = (const unsigned char *) s;
    size_t i = 0;

    if (s == NULL) {
        return len == 0;
    }
    while (i < len) {
        unsigned char c = p[i];
        size_t need;
        unsigned lo;
        unsigned hi;
        size_t k;

        if (c < 0x80) {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xDF) {
            need = 1; lo = 0x80; hi = 0xBF;
        } else if (c == 0xE0) {
            need = 2; lo = 0xA0; hi = 0xBF;
        } else if (c >= 0xE1 && c <= 0xEC) {
            need = 2; lo = 0x80; hi = 0xBF;
        } else if (c == 0xED) {
            need = 2; lo = 0x80; hi = 0x9F;   /* no surrogates */
        } else if (c >= 0xEE && c <= 0xEF) {
            need = 2; lo = 0x80; hi = 0xBF;
        } else if (c == 0xF0) {
            need = 3; lo = 0x90; hi = 0xBF;
        } else if (c >= 0xF1 && c <= 0xF3) {
            need = 3; lo = 0x80; hi = 0xBF;
        } else if (c == 0xF4) {
            need = 3; lo = 0x80; hi = 0x8F;   /* <= U+10FFFF */
        } else {
            return 0;
        }
        if (i + need >= len) {
            return 0;           /* the sequence runs off the end */
        }
        if (p[i + 1] < lo || p[i + 1] > hi) {
            return 0;
        }
        for (k = 2; k <= need; k++) {
            if (p[i + k] < 0x80 || p[i + k] > 0xBF) {
                return 0;
            }
        }
        i += need + 1;
    }
    return 1;
}

/* --- the parser object ---------------------------------------------------- */

mdbp_parser *mdbp_parser_new(const mdbp_options *opts)
{
    mdbp_options defaults;
    mdbp_parser *p;
    const char *mode;
    const char *charset;
    char refused[16];
    char charset_refused[64];

    mdbp_options_init(&defaults);
    if (opts == NULL) {
        opts = &defaults;
    }
    if (opts->variant != MDBP_VARIANT_MARIADB) {
        return NULL;
    }
    mode = opts->sql_mode != NULL ? opts->sql_mode : "";
    if (mdbp_sql_mode_refused(mode, refused, sizeof refused)) {
        return NULL;
    }
    charset = opts->charset != NULL ? opts->charset : "utf8mb4";
    if (mdbp_charset_refused(charset, charset_refused, sizeof charset_refused)) {
        return NULL;
    }
    p = (mdbp_parser *) calloc(1, sizeof *p);
    if (p == NULL) {
        return NULL;
    }
    /* mdbp_sql_mode_refused() above has already refused what the lexer
     * would, so -1 here is the lexer's defensive backstop only. */
    if (mdbp_lex_sql_mode_bits(mode, &p->sql_mode_bits) != 0) {
        free(p);
        return NULL;
    }
    p->charset = (char *) malloc(strlen(charset) + 1);
    if (p->charset == NULL) {
        free(p);
        return NULL;
    }
    memcpy(p->charset, charset, strlen(charset) + 1);
    p->prepare_mode = opts->prepare_mode ? 1 : 0;
    p->want_derivation = opts->want_derivation ? 1 : 0;
    /* The one default, src/mdbp_lex.h's, and not a number of our own. */
    p->version_id = MDBP_LEX_DEFAULT_VERSION_ID;
    /* Once, here: mdbp_ctx_init() memsets the struct, so calling it per record
     * would leak the derivation buffer (src/mdbp_internal.h). */
    mdbp_ctx_init(&p->ctx, NULL, 0, p->want_derivation);
    return p;
}

/* --- the comment gate's release id ---------------------------------------- */

/* The range is checked BEFORE anything is stored, so a refused id leaves the
 * parser's id exactly as it was.  The bounds are src/mdbp_lex.h's, the
 * same two macros the lexer's own setter checks: one range, one place. */
mdbp_status mdbp_parser_set_version_id(mdbp_parser *p, unsigned long version_id)
{
    if (p == NULL) {
        return MDBP_INTERNAL;
    }
    if (version_id < MDBP_LEX_VERSION_ID_MIN || version_id > MDBP_LEX_VERSION_ID_MAX) {
        return MDBP_UNSUPPORTED;
    }
    p->version_id = version_id;
    return MDBP_OK;
}

unsigned long mdbp_parser_version_id(const mdbp_parser *p)
{
    return p == NULL ? 0 : p->version_id;
}

void mdbp_parser_free(mdbp_parser *p)
{
    if (p == NULL) {
        return;
    }
    if (p->lx != NULL) {
        mdbp_lex_free(p->lx);
    }
    mdbp_ctx_free(&p->ctx);
    free(p->tokens);
    free(p->charset);
    free(p);
}

/* --- the result ----------------------------------------------------------- */

static void result_init(mdbp_result *out)
{
    memset(out, 0, sizeof *out);
    out->status = MDBP_OK;
    out->eof_token = MDBP_END_TOKEN;
    out->error_state = -1;
    out->error_byte_pos = -1;
}

static mdbp_status result_fail(mdbp_result *out, mdbp_status status,
                               const char *message)
{
    out->status = status;
    out->accepted = 0;
    out->message = message;
    return status;
}

/* Room for `want` tokens, and for at least one even when `want` is 0.
 *
 * The minimum of one is load-bearing, not defensive: mdbparse.h tells a
 * caller that an MDBP_INTERNAL carrying a NULL `tokens` means the token
 * stream never got built, so `tokens` must be non-NULL whenever it DID get
 * built -- including for an empty statement. */
static int tokens_reserve(mdbp_parser *p, size_t want)
{
    size_t cap = p->tokens_cap;
    mdbp_token *grown;

    if (want == 0) {
        want = 1;
    }
    if (cap >= want) {
        return 0;
    }
    cap = cap ? cap : 64;
    while (cap < want) {
        if (cap > (size_t) -1 / 2) {
            return -1;
        }
        cap *= 2;
    }
    grown = (mdbp_token *) realloc(p->tokens, cap * sizeof *grown);
    if (grown == NULL) {
        return -1;
    }
    p->tokens = grown;
    p->tokens_cap = cap;
    return 0;
}

/* Starts a record: drops what the previous call left behind that this one
 * would otherwise let a caller read after it has become a lie. */
static void parse_begin(mdbp_parser *p)
{
    if (p->lx != NULL) {
        mdbp_lex_free(p->lx);
        p->lx = NULL;
    }
    p->ntokens = 0;
    mdbp_ctx_reset(&p->ctx, NULL, 0);
}

/* The lexer for one mdbp_parse_sql(), mdbp_lex_sql() or mdbp_lex_sql_prefix()
 * call, built from this parser's settings -- sql_mode, charset, prepare mode
 * and the comment gate's release id -- in ONE place, so the three calls that
 * lex cannot come to disagree about which of the parser's settings reach
 * their lexer.  `valid` is the one thing that differs among them
 * (mdbp_valid_live for a parse, NULL for no parser at all -- both lexing-only
 * calls' context).  Sets p->lx, which is NULL again when there is no lexer to
 * lex with.
 *
 * mdbp_parser_new() already refused the two sql_modes and every charset the
 * lexer would refuse, so a NULL lexer is an allocation failure.
 * mdbp_parser_set_version_id() never stores an id mdbp_lex_set_version_id()
 * would refuse, so that refusal cannot happen; it is still checked, because
 * an unreported one would lex the record against the default id while the
 * caller believes it set another.  Returns NULL, or the message of the
 * MDBP_INTERNAL the caller reports. */
static const char *lexer_open(mdbp_parser *p, const char *sql, size_t len,
                              mdbp_valid_fn valid)
{
    p->lx = mdbp_lex_new(sql, len, p->sql_mode_bits, p->charset,
                         p->prepare_mode, valid, NULL);
    if (p->lx == NULL) {
        return "out of memory lexing";
    }
    if (mdbp_lex_set_version_id(p->lx, p->version_id) != 0) {
        mdbp_lex_free(p->lx);
        p->lx = NULL;
        return "the lexer refused the parser's version id";
    }
    return NULL;
}

/* How resolve_token_numbers() treats a name this grammar has no terminal for.
 * The two parse calls STOP there: the name is their refusal, and nothing past
 * it is resolved.  mdbp_lex_sql() and mdbp_lex_sql_prefix() CONTINUE: the
 * name keeps token_number == -1 and every later terminal still gets its
 * number, because a tokeniser must not blank a whole document over one stray
 * character. */
typedef enum {
    RESOLVE_STOP_AT_UNKNOWN = 0,
    RESOLVE_PAST_UNKNOWN
} resolve_mode;

/* Terminal name -> token number for every token, in order.  Returns the index
 * of the first name this grammar has no terminal for, or `n` when they all
 * resolve.  Under RESOLVE_STOP_AT_UNKNOWN, tokens after the offender keep
 * token_number == -1: they were never resolved, and saying so is better than
 * leaving whatever the caller happened to pass in.  Under RESOLVE_PAST_UNKNOWN
 * every name is looked up, and only the ones that are not terminals keep -1
 * (the caller has already set -1 on every token: live_push() does).  A token
 * whose number is already set -- live_pull() writes back the one it looked
 * up for the automaton -- is not looked up a second time. */
static size_t resolve_token_numbers(mdbp_token *tokens, size_t n,
                                    resolve_mode mode)
{
    size_t first_unknown = n;
    size_t i;

    for (i = 0; i < n; i++) {
        int number;

        if (tokens[i].token_number >= 0) {
            continue;
        }
        number = mdbp_token_number(tokens[i].name);
        if (number < 0) {
            if (first_unknown == n) {
                first_unknown = i;
            }
            if (mode == RESOLVE_STOP_AT_UNKNOWN) {
                return first_unknown;
            }
            continue;
        }
        tokens[i].token_number = number;
    }
    return first_unknown;
}

static mdbp_status result_unsupported_token(mdbp_parser *p, size_t at,
                                            mdbp_result *out)
{
    out->tokens = p->tokens;
    out->ntokens = p->ntokens;
    out->error_seen = 1;
    out->error_token_index = at;
    out->error_token = p->tokens[at].name;
    out->error_byte_pos = p->tokens[at].byte_start;
    return result_fail(out, MDBP_UNSUPPORTED,
                       "the token name is not a terminal of this grammar");
}

/* The verdict, from the context the parse just ran over.  `prc` is bison's
 * own code: 0 accepted, 1 rejected, 2 memory exhausted. */
static mdbp_status result_from_parse(mdbp_parser *p, int prc, mdbp_result *out)
{
    mdbp_ctx *ctx = &p->ctx;

    out->tokens = p->tokens;
    out->ntokens = p->ntokens;
    out->tokens_consumed = ctx->cursor;
    out->derivation = ctx->nderivation > 0 ? ctx->derivation : NULL;
    out->derivation_len = ctx->nderivation;
    p->depths = ctx->nderivation > 0 ? ctx->derivation_depth : NULL;

    if (ctx->internal_error || prc == 2) {
        /* No verdict: the automaton (or its derivation buffer) ran out of
         * memory.  `tokens` stays set, which is exactly how mdbparse.h says a
         * caller tells this apart from "the token stream never got built",
         * and so does the reduction PREFIX -- reporting what was actually
         * recorded is more use to whoever has to diagnose the failure than
         * zeroing it, and mdbparse.h says in terms that it is a prefix and
         * not a derivation of anything. */
        return result_fail(out, MDBP_INTERNAL,
                           "the parse ran out of memory");
    }
    if (prc == 0) {
        out->accepted = 1;
        return MDBP_OK;
    }

    /* A rejection IS a success. */
    {
        size_t at = ctx->lookahead_index;

        if (at > p->ntokens) {
            at = p->ntokens;
        }
        out->accepted = 0;
        out->error_seen = 1;
        out->error_token_index = at;
        out->error_token = at < p->ntokens ? p->tokens[at].name
                                           : MDBP_END_TOKEN;
        out->error_state = ctx->error_state;
        out->error_byte_pos = at < p->ntokens ? p->tokens[at].byte_start : -1;
        out->expected = (const char *const *) ctx->expected;
        out->nexpected = ctx->nexpected;
    }
    return MDBP_OK;
}

/* --- mdbp_parse_tokens ---------------------------------------------------- */

mdbp_status mdbp_parse_tokens(mdbp_parser *p, const mdbp_token *tokens,
                              size_t ntokens, mdbp_result *out)
{
    size_t at;
    size_t i;
    int prc;

    if (out == NULL) {
        return MDBP_INTERNAL;
    }
    result_init(out);
    if (p != NULL) {
        p->depths = NULL;
    }
    if (p == NULL || (tokens == NULL && ntokens > 0)) {
        return result_fail(out, MDBP_INTERNAL, "no parser, or no tokens");
    }
    parse_begin(p);
    if (tokens_reserve(p, ntokens) != 0) {
        return result_fail(out, MDBP_INTERNAL,
                           "out of memory copying the token vector");
    }
    /* The vector is COPIED, not borrowed, because token_number is resolved
     * from `name` and the caller's array is const.  The strings themselves are
     * not copied: mdbparse.h says so, and copying every token's strings is a
     * cost a batch caller would pay on every record for nothing. */
    for (i = 0; i < ntokens; i++) {
        p->tokens[i] = tokens[i];
        p->tokens[i].token_number = -1;
    }
    p->ntokens = ntokens;

    at = resolve_token_numbers(p->tokens, p->ntokens, RESOLVE_STOP_AT_UNKNOWN);
    if (at < p->ntokens) {
        return result_unsupported_token(p, at, out);
    }

    mdbp_ctx_reset(&p->ctx, p->tokens, p->ntokens);
    prc = mdbp_run_parser(&p->ctx);
    return result_from_parse(p, prc, out);
}

/* --- mdbp_parse_sql ------------------------------------------------------- */

/* The parser DRIVES the lexer: valid(T) -- "can this terminal be shifted from
 * where the automaton is standing right now" -- has an answer only while a
 * parse is in progress, and the token the lexer spells from that answer is the
 * token the parse then consumes.  Lex-everything-then-parse cannot express
 * that, and MariaDB's server does not do it either.  So the context is armed
 * with this pull source and the automaton asks for each token as it needs it.
 */
struct live_source {
    mdbp_parser *p;
    int failed;
};

static int live_push(mdbp_parser *p, const mdbp_token *tok)
{
    if (tokens_reserve(p, p->ntokens + 1) != 0) {
        return -1;
    }
    /* The lexer's `name` is a static string and its `text` points into the
     * lexer's own copy of the statement, which this parser keeps alive until
     * its next call -- so neither is copied. */
    p->tokens[p->ntokens] = *tok;
    p->tokens[p->ntokens].token_number = -1;
    p->ntokens++;
    return 0;
}

static int live_pull(void *user, mdbp_token *out)
{
    struct live_source *src = (struct live_source *) user;
    mdbp_parser *p = src->p;
    mdbp_token tok;
    int number;
    int rc = mdbp_lex_next(p->lx, &tok);

    if (rc < 0 || (rc > 0 && live_push(p, &tok) != 0)) {
        src->failed = 1;
        return -1;
    }
    if (rc == 0) {
        return 0;                /* the stream ended: bison's $end */
    }
    number = mdbp_token_number(tok.name);
    if (number < 0) {
        /* A token this grammar has no terminal for.  The parse stops here
         * (there is nothing to hand the automaton) and the caller's own
         * resolution pass below turns it into MDBP_UNSUPPORTED at the same
         * index the token-vector path would report.  The REST of the stream is
         * still lexed, so `tokens` is the whole stream either way. */
        return -1;
    }
    /* Written back into the stored copy too, so resolve_token_numbers()
     * does not look the same name up again. */
    p->tokens[p->ntokens - 1].token_number = number;
    *out = tok;
    out->token_number = number;
    return 1;
}

/* Pulls what is left of the stream into the parser's token array, stopping
 * before a counted token once `max_tokens` of them are already pushed:
 * every token but END_OF_INPUT counts, and END_OF_INPUT is always pushed,
 * whatever the count.  Writes `more` (when not NULL): 1 when the limit
 * stopped the loop with input still unlexed, 0 when the loop reached the
 * stream's own end.  A rejected parse returns with input still unread;
 * lexing the rest of it (with valid(T) answering 0 for everything, because
 * the binding died with the parse) is what makes `tokens` the WHOLE stream
 * and not a prefix.  mdbp_parse_sql()'s tail passes (size_t) -1, a limit no
 * stream reaches, and NULL: it always lexes the rest of the stream.
 *
 * It is also the whole of mdbp_lex_sql()'s and mdbp_lex_sql_prefix()'s
 * lexing, through the one shared body (lex_sql_impl, below) both call:
 * there, nothing has been pulled yet, so "what is left" is the entire
 * stream, cut at `max_tokens` for mdbp_lex_sql_prefix().  One pull loop for
 * all three, so they cannot come to disagree about how a stream is collected.
 * Returns 0, or -1 on an allocation failure. */
static int lex_drain(mdbp_parser *p, size_t max_tokens, int *more)
{
    size_t counted = 0;

    for (;;) {
        mdbp_token tok;
        int rc = mdbp_lex_next(p->lx, &tok);

        if (rc < 0) {
            return -1;
        }
        if (rc == 0) {
            if (more != NULL) {
                *more = 0;
            }
            return 0;
        }
        if (strcmp(tok.name, "END_OF_INPUT") != 0) {
            if (counted >= max_tokens) {
                if (more != NULL) {
                    *more = 1;
                }
                return 0;
            }
            counted++;
        }
        if (live_push(p, &tok) != 0) {
            return -1;
        }
    }
}

mdbp_status mdbp_parse_sql(mdbp_parser *p, const char *sql, size_t len,
                           mdbp_result *out)
{
    struct live_source src;
    const char *eof;
    const char *failed;
    size_t at;
    int prc;

    if (out == NULL) {
        return MDBP_INTERNAL;
    }
    result_init(out);
    if (p != NULL) {
        p->depths = NULL;
    }
    if (p == NULL) {
        return result_fail(out, MDBP_INTERNAL, "no parser");
    }
    if (!mdbp_input_is_utf8(sql, len)) {
        return result_fail(out, MDBP_UNSUPPORTED,
                           "the statement is not valid UTF-8 (the connection "
                           "charset is utf8mb4)");
    }
    parse_begin(p);

    failed = lexer_open(p, sql, len, mdbp_valid_live);
    if (failed != NULL) {
        return result_fail(out, MDBP_INTERNAL, failed);
    }
    if (tokens_reserve(p, 0) != 0) {
        return result_fail(out, MDBP_INTERNAL, "out of memory lexing");
    }
    /* THE JOIN.  MariaDB's lexer reads `parsing_place` off the
     * SELECT_LEX its THD points at; MariaDB's grammar actions write it.  Both
     * halves are now MariaDB's own text here -- the lexer through the slice,
     * the actions through tools/stub.py's `preserve_parsing_place` rule
     * -- so the only thing left to do is make them the same object.  From
     * here until the next parse_begin(), a lifted action's write lands in
     * `lx->sel` and `Lex_input_stream::lex_token` reads it back.  There is no
     * inference left anywhere: not of the value, not of its timing.
     *
     * mdbp_ctx_reset() (in parse_begin above) has already pointed it back at
     * the context's own storage, so this never outlives the lexer. */
    p->ctx.current_select = mdbp_lex_select(p->lx);

    src.p = p;
    src.failed = 0;
    mdbp_ctx_set_source(&p->ctx, live_pull, &src);
    prc = mdbp_run_parser(&p->ctx);
    if (lex_drain(p, (size_t) -1, NULL) != 0) {
        src.failed = 1;
    }
    mdbp_ctx_set_source(&p->ctx, NULL, NULL);

    if (src.failed) {
        /* NULL `tokens`: the stream was never completed, which mdbparse.h
         * makes the observable difference between "this record has no verdict"
         * and "this process can no longer make progress". */
        p->ntokens = 0;
        return result_fail(out, MDBP_INTERNAL, "out of memory lexing");
    }
    eof = mdbp_lex_eof_token(p->lx);
    if (eof != NULL) {
        out->eof_token = eof;    /* a static string; it outlives the lexer */
    }

    /* Every token is resolved, including the ones lexed AFTER the parse
     * ended: an unknown terminal name anywhere in the stream is the verdict,
     * whatever the automaton had already concluded. */
    at = resolve_token_numbers(p->tokens, p->ntokens, RESOLVE_STOP_AT_UNKNOWN);
    if (at < p->ntokens) {
        return result_unsupported_token(p, at, out);
    }
    return result_from_parse(p, prc, out);
}

/* --- mdbp_lex_sql / mdbp_lex_sql_prefix ------------------------------------ */

/* The lexer with NO parser in it, shared by mdbp_lex_sql() and
 * mdbp_lex_sql_prefix(): mdbp_lex_sql() passes `max_tokens` (size_t) -1 --
 * a limit no document reaches -- and `more` NULL, so every branch below that
 * exists only for mdbp_lex_sql_prefix() is inert for it;
 * mdbp_lex_sql_prefix() passes its own two arguments straight through.
 *
 * `valid` is NULL, which src/mdbp_lex.h defines as "always 0", so the one
 * valid() site (mdbp_downgrade_left_paren in src/mdbp_lex.cc) always answers
 * a plain '('.  No parse runs and the parse context is never pointed at the
 * lexer's SELECT_LEX, so no grammar action can write `parsing_place`: it
 * stays the NO_MATTER mdbp_lex_new() gives it, from the first byte to the
 * last.  That is deliberately NOT a rejected parse's lex_drain() tail, which
 * keeps whatever the last action wrote.
 *
 * The result is the header's field-by-field list: result_init() already
 * wrote every field that list fixes (accepted 0, no derivation, no error
 * record, error_state and error_byte_pos -1, eof_token "$end", message NULL),
 * so what is left to set is the stream and what ended it -- `out->eof_token`
 * stays NULL instead when lex_drain() stopped on the limit rather than
 * the stream's own end, which `more` (written here, not just by lex_drain()
 * itself, so it is set on every return, not only the ones that reach the
 * loop) also reports. */
static mdbp_status lex_sql_impl(mdbp_parser *p, const char *sql, size_t len,
                                size_t max_tokens, mdbp_result *out, int *more)
{
    const char *eof;
    const char *failed;
    int cut = 0;

    if (more != NULL) {
        *more = 0;               /* written on every return */
    }
    if (out == NULL) {
        return MDBP_INTERNAL;
    }
    result_init(out);
    if (p != NULL) {
        p->depths = NULL;
    }
    if (p == NULL) {
        return result_fail(out, MDBP_INTERNAL, "no parser");
    }
    if (!mdbp_input_is_utf8(sql, len)) {
        return result_fail(out, MDBP_UNSUPPORTED,
                           "the statement is not valid UTF-8 (the connection "
                           "charset is utf8mb4)");
    }
    parse_begin(p);

    /* The same lexer mdbp_parse_sql() opens, minus the parser: valid is
     * NULL.  The comment gate's release id reaches it the same way. */
    failed = lexer_open(p, sql, len, NULL);
    if (failed != NULL) {
        return result_fail(out, MDBP_INTERNAL, failed);
    }
    if (tokens_reserve(p, 0) != 0 || lex_drain(p, max_tokens, &cut) != 0) {
        /* NULL `tokens`: the stream was never completed. */
        p->ntokens = 0;
        return result_fail(out, MDBP_INTERNAL, "out of memory lexing");
    }
    if (cut) {
        out->eof_token = NULL;   /* nothing ended the stream */
    } else {
        eof = mdbp_lex_eof_token(p->lx);
        if (eof != NULL) {
            out->eof_token = eof;    /* a static string; it outlives the lexer */
        }
    }
    if (more != NULL) {
        *more = cut;
    }

    /* Never a refusal.  The first unknown index is of no use here. */
    (void) resolve_token_numbers(p->tokens, p->ntokens, RESOLVE_PAST_UNKNOWN);
    out->tokens = p->tokens;
    out->ntokens = p->ntokens;
    return MDBP_OK;
}

/* Lexes `sql` (`len` bytes) with MariaDB's own lexer and does not parse it:
 * see include/mdbparse.h for the full contract.  `lex_sql_impl()` above is the
 * whole of the work; this passes the two arguments that make it lex the whole
 * document (no limit, and nowhere to write whether one was hit). */
mdbp_status mdbp_lex_sql(mdbp_parser *p, const char *sql, size_t len,
                         mdbp_result *out)
{
    return lex_sql_impl(p, sql, len, (size_t) -1, out, NULL);
}

/* At most `max_tokens` counted tokens of what mdbp_lex_sql() returns for the
 * same parser and bytes, or all of it (its END_OF_INPUT included, when it has
 * one) when the document has that many counted tokens or fewer.  Since 2.2.
 * See include/mdbparse.h for the full contract. */
mdbp_status mdbp_lex_sql_prefix(mdbp_parser *p, const char *sql, size_t len,
                                size_t max_tokens, mdbp_result *out, int *more)
{
    return lex_sql_impl(p, sql, len, max_tokens, out, more);
}

const int *mdbp_derivation_depths(const mdbp_parser *p)
{
    return p != NULL ? p->depths : NULL;
}
