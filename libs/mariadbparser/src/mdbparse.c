/* src/mdbparse.c -- the CLI (PLAN.md S3.3): one record (--sql / --file /
 * --tokens / --tokens-json) or a whole corpus in one process (--jsonl).
 *
 * SINCE TASK 9.1 IT IS A CLIENT, AND NOTHING MORE.  Argument parsing, the
 * three input formats, --dump-tokens, the JSON verdict, batch streaming and
 * the exit-code mapping live here; every actual PARSE goes through
 * mdbp_parse_tokens() / mdbp_parse_sql() (include/mdbparse.h, implemented once in
 * src/mdbp_api.c), which an external caller reaches by exactly the same
 * names.  Two implementations of "parse one record" is the defect that
 * arrangement exists to prevent: one of them drifts, and it is the one nobody
 * measures.  The one thing that is still CLI-only is the lexing done for the
 * three static `--valid` modes (none/all/list, for corpus/lex-fixtures): the
 * library exposes no knob for them, on purpose, and their tokens are handed
 * to mdbp_parse_tokens() like any other vector.  `none` is now ALSO reachable
 * through the library, as its own function rather than a knob: since 2.1,
 * mdbp_lex_sql() lexes with no parser, which is exactly `--valid none` with
 * the default `--parsing-place` (plan `tokeniser-entry-points.md` D-3), and
 * `--lex-only` is how this CLI calls it.  Since 2.2, `--lex-only --max-tokens
 * N` calls mdbp_lex_sql_prefix() instead (task P1.1, plan `lex-sql-prefix.md`
 * D-9): the first N counted tokens, or the whole stream when it has that many
 * or fewer.  `all`, `list` and a non-default `--parsing-place` stay CLI-only.
 * `--parsing-place` belongs to the static
 * modes -- see the `--- lexing` section header below for what it is and why it
 * has to exist.  `--version-id N` (since 2.1) sets the release id versioned
 * comments are judged against through the public
 * mdbp_parser_set_version_id(); the static modes get the parser's id through
 * the internal mdbp_lex_set_version_id(), so every path that lexes honours
 * one id.
 *
 * Runs bison's parser over a token stream and prints a verdict plus the
 * derivation as JSON.  Since task 8.2 the stream can come from either end:
 * --sql/--file LEX the bytes with MariaDB's own lexer (sliced out of
 * sql/sql_lex.cc, src/mdbp_lex.h is the whole interface), and
 * --tokens/--tokens-json/--jsonl take a vector of terminal NAMES in the
 * grammar model's own spelling, exactly as before.
 *
 * Exit codes are the project convention (PLAN.md S3.3, replay.py:45):
 *   0 success -- INCLUDING a well-formed rejection, which is a measurement
 *   1 internal error
 *   2 usage, or unreadable input
 *   3 unsupported input (a name that is not a terminal, an unsupported
 *     sql_mode, or a flag a later phase owns)
 *   4 only with --verdict-exit, and only for "rejected"
 *
 * BATCH MODE DIVERGES ON ONE OF THEM, DELIBERATELY: under --jsonl an unknown
 * terminal name is a per-record verdict ("status": "unsupported") and NOT
 * exit 3, because one bad record out of 212,332 must not end the run or
 * relabel the other 212,331.  Only a malformed line, an unreadable file or a
 * bad "id" (2), or a failed allocation (1), stop a batch run; everything a
 * parse can conclude is a field in the object.
 */
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <time.h>

#include "mdbparse.h"      /* the library surface this CLI is a client of */
#include "mdbp_internal.h"
#include "mdbp_lex.h"

#define EXIT_SUCCESS_OK 0
#define EXIT_INTERNAL 1
#define EXIT_USAGE 2
#define EXIT_UNSUPPORTED 3
#define EXIT_REJECTED 4

/* The end-of-input symbol this CLI reports.  It is a constant (PLAN.md S3.3),
 * and it is "$end" rather than END_OF_INPUT -- which S3.3's illustrative
 * example shows, but which is an ordinary terminal appearing IN the token
 * vector, not an end marker.
 *
 * Why the constant is right HERE: "$end" is the grammar's own end-of-input
 * terminal -- token number 0, symbol number 0, "bison pseudo-terminal" in
 * corpus/grammar-model.json -- and src/mdbp_runtime.c's yylex returns exactly
 * that 0 once the token vector is exhausted.  So $end is by construction the
 * symbol that ends every parse this binary runs, and it could not report
 * anything else: it does not lex, it is handed an already-lexed vector.
 *
 * What this must NOT be read as: "the corpus always says $end".  The corpus's
 * eof_token is a LEXER field (tools/vendor/bison2ts/reflex.py:1371-1387,
 * surfaced by lexdump.py:341) recording which symbol ended the token stream,
 * and 62 of the 212,332 records in the corpus/expected jsonl files carry
 * "ABORT_SYM" instead (mined.jsonl 48, mysql_test.jsonl 14; counted
 * 2026-08-27) -- the lexer aborting on broken input, with the ABORT_SYM
 * token itself present in that record's `tokens`.  replay.py does not emit this field at all.  P4
 * diffs it, so a straight equality against a corpus record is sound only for
 * the "$end" records; those 62 need their own accounting. */
#define MDBP_EOF_TOKEN "$end"

/* --- a growable token vector --------------------------------------------- */

typedef struct {
    mdbp_token *items;
    size_t n;
    size_t cap;
} token_vec;

static int vec_push(token_vec *v, char *name, char *text, long start, long end)
{
    if (v->n == v->cap) {
        size_t want = v->cap ? v->cap * 2 : 64;
        mdbp_token *grown = (mdbp_token *) realloc(v->items, want * sizeof *grown);
        if (grown == NULL) {
            return -1;
        }
        v->items = grown;
        v->cap = want;
    }
    v->items[v->n].name = name;
    v->items[v->n].text = text;
    v->items[v->n].token_number = -1;
    v->items[v->n].byte_start = start;
    v->items[v->n].byte_end = end;
    v->n++;
    return 0;
}

/* Empties the vector but KEEPS its allocation: batch mode calls this between
 * records, so the 22,928-token record the corpus holds is paid for once
 * rather than once per record.  The token STRINGS are per record and are
 * freed here; only the mdbp_token array survives. */
static void vec_reset(token_vec *v)
{
    size_t i;

    for (i = 0; i < v->n; i++) {
        free((void *) v->items[i].name);
        free((void *) v->items[i].text);
    }
    v->n = 0;
}

static void vec_free(token_vec *v)
{
    vec_reset(v);
    free(v->items);
    v->items = NULL;
    v->cap = 0;
}

/* --- reading a whole stream ---------------------------------------------- */

/* Reads a whole stream.  On failure returns NULL and sets *err to the exit
 * code the failure deserves: an unreadable file is usage (2), a failed
 * allocation is an internal error (1).  Collapsing the two would report an
 * out-of-memory run as bad input. */
static char *slurp(const char *path, size_t *out_len, int *err)
{
    FILE *f = NULL;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 1 << 16;

    if (strcmp(path, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (f == NULL) {
            fprintf(stderr, "mdbparse: %s: %s\n", path, strerror(errno));
            *err = EXIT_USAGE;
            return NULL;
        }
    }
    buf = (char *) malloc(cap);
    if (buf == NULL) {
        if (f != stdin) {
            fclose(f);
        }
        *err = EXIT_INTERNAL;
        return NULL;
    }
    for (;;) {
        size_t got;

        if (len == cap) {
            char *grown = (char *) realloc(buf, cap * 2);
            if (grown == NULL) {
                free(buf);
                if (f != stdin) {
                    fclose(f);
                }
                *err = EXIT_INTERNAL;
                return NULL;
            }
            buf = grown;
            cap *= 2;
        }
        got = fread(buf + len, 1, cap - len, f);
        len += got;
        if (got == 0) {
            break;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "mdbparse: %s: %s\n", path, strerror(errno));
        free(buf);
        if (f != stdin) {
            fclose(f);
        }
        *err = EXIT_USAGE;
        return NULL;
    }
    if (f != stdin) {
        fclose(f);
    }
    buf[len] = '\0';       /* there is always room: len < cap after the loop */
    *out_len = len;
    return buf;
}

/* --- lexing: --sql / --file / --dump-tokens ------------------------------- */

/* Where valid(T) answers come from.  Every merge site the lexer guards with
 * valid() stands in for a `curr_sel->parsing_place` test (PLAN.md S1.8,
 * tools/vendor/thd.md), and NO_MATTER -- the value everywhere outside the five
 * grammar actions that set it -- makes all of them false, which is what
 * "none" answers and what 264 of the 281 lexer fixtures ask for.
 *
 * VALID_LIVE is the real one, and the default: the PARSER answers, from the
 * LR stack it is standing on at the moment the lexer asks
 * (mdbp_valid_live(), src/parser_glue.c, bison's own yy_lac()).  It is what
 * `tools/vendor/bison2ts/lexdump.py --valid replay` is, and it is the only
 * mode in which the lexer and the parser run TOGETHER: the parser pulls each
 * token as it needs it, exactly as MariaDB's server does.  The three static
 * modes stay because corpus/lex-fixtures pins single lexer branches
 * with them and `make lexcheck` must keep answering the same.
 *
 * A STATIC MODE RUNS THE LEXER WITH NO PARSER, AND THAT IS WHY
 * --parsing-place EXISTS (task P1.1).
 * -----------------------------------------------------------
 * `curr_sel->parsing_place` -- the field MariaDB's lexer reads at its five
 * feedback sites -- is written by MariaDB's own grammar ACTIONS, which
 * tools/stub.py now lifts verbatim (its `preserve_parsing_place` rule;
 * build/stub-map.json's `preserve` block enumerates every one of them).  On
 * the live path that is the whole story: the parser writes the field the
 * lexer reads, in the same object, at MariaDB's own moments.
 *
 * In a static mode there IS no parser while the lexer runs -- the record is
 * lexed to completion and the vector handed to mdbp_parse_tokens() afterwards
 * -- so nothing can write the field and it stays NO_MATTER, which is exactly
 * what 264 of the 281 lexer fixtures want.  The other 17 are testing a branch
 * that only fires when the parser has put the field somewhere else, so the
 * TEST supplies the parser's context directly: `--parsing-place NAME` writes
 * it once, before the first token is scanned, and MariaDB's own sliced lexer
 * then SPENDS it exactly as it would in a real parse.  Nothing here infers
 * the value or its timing; a declaration is a claim the test makes, and
 * tests/data/parsing-place-declarations.json makes each one cite the
 * sql_yacc.yy action line whose lifted write would have set it.
 *
 * It is refused with `--valid live` rather than ignored: there the parser
 * writes the field, and an option that silently lost to it would be a lie. */
typedef enum {
    VALID_NONE = 0,
    VALID_ALL,
    VALID_LIST,
    VALID_LIVE
} valid_source;

typedef struct {
    const char *sql_mode;
    const char *charset;         /* P3.1: --charset, "utf8mb4" by default */
    int prepare_mode;
    valid_source valid;
    const char *valid_symbols;   /* the --valid-symbols comma list */
    /* --parsing-place: the DECLARED parser context for a static mode, as an
     * `enum enum_parsing_place` value (src/stub.h).  NO_MATTER (0) by
     * default, which is the value the field has everywhere outside the
     * grammar actions that set it -- so a fixture that does not ask for one
     * is unaffected. */
    int parsing_place;
    /* --version-id, for the static modes (plan `tokeniser-entry-points.md`
     * D-13): the comment gate's release id, copied from the ONE parser this
     * process builds -- mdbp_parser_version_id() after any
     * mdbp_parser_set_version_id() -- so the static path lexes against
     * exactly the id the library path does, and this CLI never spells a
     * default of its own. */
    unsigned long version_id;
    /* --max-tokens N (task P1.1, plan `lex-sql-prefix.md` D-9): --lex-only
     * calls mdbp_lex_sql_prefix() instead of mdbp_lex_sql() when this is set.
     * `max_tokens` is meaningless when `max_tokens_given` is 0. */
    int max_tokens_given;
    unsigned long max_tokens;
} lex_config;

static int valid_answer(const char *name, void *user)
{
    const lex_config *cfg = (const lex_config *) user;
    const char *p;
    size_t want;

    /* VALID_LIVE never arrives here: that mode hands the lexer
     * mdbp_valid_live() instead of this function.  Answering 0 rather than
     * falling into the list walk keeps a future caller that forgets from
     * getting a silently wrong answer out of a NULL symbol list. */
    if (cfg == NULL || cfg->valid == VALID_NONE || cfg->valid == VALID_LIVE) {
        return 0;
    }
    if (cfg->valid == VALID_ALL) {
        return 1;
    }
    if (cfg->valid_symbols == NULL) {
        return 0;
    }
    want = strlen(name);
    for (p = cfg->valid_symbols; *p != '\0'; ) {
        const char *begin = p;
        size_t n;

        while (*p != '\0' && *p != ',') {
            p++;
        }
        n = (size_t) (p - begin);
        if (n == want && memcmp(begin, name, n) == 0) {
            return 1;
        }
        if (*p == ',') {
            p++;
        }
    }
    return 0;
}

/* One lexed token into the vector.  The NAME is copied (vec_reset frees it);
 * the TEXT is left NULL and the byte offsets carry the lexeme, because the
 * lexer's own copy of the query dies with the lexer and every consumer here
 * reads the bytes back out of `sql`.  Returns 0 or EXIT_INTERNAL. */
static int push_lexed(token_vec *v, const mdbp_token *tok)
{
    char *name = strdup(tok->name);

    if (name == NULL
        || vec_push(v, name, NULL, tok->byte_start, tok->byte_end) != 0) {
        free(name);
        return EXIT_INTERNAL;
    }
    return 0;
}

/* Pulls what is left of the stream into `v`.  Returns 0 or EXIT_INTERNAL. */
static int lex_drain(mdbp_lexer *lx, token_vec *v)
{
    for (;;) {
        mdbp_token tok;
        int rc = mdbp_lex_next(lx, &tok);

        if (rc < 0) {
            return EXIT_INTERNAL;
        }
        if (rc == 0) {
            return 0;
        }
        if (push_lexed(v, &tok) != 0) {
            return EXIT_INTERNAL;
        }
    }
}

/* Runs the sliced lexer over `sql` and fills `v` with one entry per token,
 * with valid(T) answered from `cfg` -- the three STATIC modes (none, all,
 * list).  `--valid live` does not come here; it cannot, because there the
 * token stream is a function of a parse that has to be running while the
 * lexer runs -- and that is mdbp_parse_sql() (include/mdbparse.h), the library
 * entry point this CLI calls for every `--valid live` record.  The three
 * static modes have no public knob and stay here, for corpus/lex-fixtures:
 * they lex WITHOUT a parse and hand the vector to mdbp_parse_tokens().
 *
 * `*eof_out` becomes "$end" or "ABORT_SYM" -- what the stream actually ended
 * on, which is the field the corpus calls eof_token.  Returns 0, or
 * EXIT_INTERNAL for an allocation failure. */
static int lex_record(const char *sql, size_t len, const lex_config *cfg,
                      token_vec *v, const char **eof_out)
{
    mdbp_lexer *lx;
    const char *eof;

    *eof_out = MDBP_EOF_TOKEN;
    lx = mdbp_lex_new(sql, len, cfg->sql_mode, cfg->charset, cfg->prepare_mode,
                      valid_answer, (void *) cfg);
    if (lx == NULL) {
        return EXIT_INTERNAL;
    }
    /* The declared parser context (see this section's header).  Written once,
     * before the first token is scanned, into the very field a lifted
     * grammar action would have written on the live path.  The value was
     * range-checked at argument time, so a failure here is impossible; it is
     * still checked, because an unreported one would silently lex the record
     * in the wrong context. */
    if (mdbp_lex_set_parsing_place(lx, cfg->parsing_place) != 0) {
        mdbp_lex_free(lx);
        return EXIT_INTERNAL;
    }
    /* --version-id reaches the static modes here, through the internal
     * setter (D-13); the library path gets the same id from the parser.  The
     * parser already accepted it, so a refusal here is impossible, and it is
     * checked for the same reason as the line above. */
    if (mdbp_lex_set_version_id(lx, cfg->version_id) != 0) {
        mdbp_lex_free(lx);
        return EXIT_INTERNAL;
    }
    if (lex_drain(lx, v) != 0) {
        mdbp_lex_free(lx);
        return EXIT_INTERNAL;
    }
    eof = mdbp_lex_eof_token(lx);
    if (eof != NULL) {
        *eof_out = eof;          /* a static string; it outlives the lexer */
    }
    mdbp_lex_free(lx);
    return 0;
}

/* --dump-tokens' escape rule, verbatim from lexdump.py's escape():
 *   backslash -> \\, TAB -> \t, LF -> \n, CR -> \r, any other byte < 0x20 or
 *   == 0x7F -> \xNN (lowercase hex); EVERY OTHER BYTE PASSES THROUGH.
 * lexdump.py reaches the same result by decoding and re-encoding with
 * surrogateescape; because the escaped set is ASCII-only and no ASCII byte can
 * occur inside a multi-byte UTF-8 sequence, the byte-level rule is exactly
 * equivalent -- which is why this may be, and is, done byte-wise. */
static void put_escaped(FILE *out, const char *bytes, size_t len)
{
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char c = (unsigned char) bytes[i];

        switch (c) {
        case '\\': fputs("\\\\", out); break;
        case '\t': fputs("\\t", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        default:
            if (c < 0x20 || c == 0x7F) {
                fprintf(out, "\\x%02x", c);
            } else {
                fputc((int) c, out);
            }
        }
    }
}

/* One token per line, `NAME<TAB>text<TAB>start:end`, byte-for-byte
 * lexdump.py.  `source` is the record's bytes when the token came from the
 * lexer (its `text` is NULL or points into the library's copy, and the
 * offsets are the lexeme); a tokens-in record has no source and carries its
 * own text instead. */
static void print_token_lines(const mdbp_token *items, size_t n,
                              const char *source, size_t source_len)
{
    size_t i;

    for (i = 0; i < n; i++) {
        const mdbp_token *tok = &items[i];

        fputs(tok->name, stdout);
        fputc('\t', stdout);
        if (source != NULL && tok->byte_start >= 0 && tok->byte_end >= tok->byte_start
            && (size_t) tok->byte_end <= source_len) {
            put_escaped(stdout, source + tok->byte_start,
                        (size_t) (tok->byte_end - tok->byte_start));
        } else if (tok->text != NULL) {
            put_escaped(stdout, tok->text, strlen(tok->text));
        }
        fprintf(stdout, "\t%ld:%ld\n", tok->byte_start, tok->byte_end);
    }
}

/* --dump-state-maps: the two 256-byte tables the binary is REALLY using, read
 * through mdbp_lex_state_maps() rather than by re-parsing build/state_maps.h,
 * so what is printed cannot drift from what the lexer indexes. */
static void print_state_maps(void)
{
    const unsigned char *state_map = NULL;
    const unsigned char *ident_map = NULL;
    int pass;

    mdbp_lex_state_maps(&state_map, &ident_map);
    for (pass = 0; pass < 2; pass++) {
        const unsigned char *map = pass ? ident_map : state_map;
        int i;

        fputs(pass ? "ident_map\t" : "state_map\t", stdout);
        for (i = 0; i < 256; i++) {
            fprintf(stdout, "%02x", map == NULL ? 0 : map[i]);
        }
        fputc('\n', stdout);
    }
}

/* --- the NAME<TAB>text[<TAB>start:end] format ---------------------------- */

static char *dup_range(const char *begin, const char *end)
{
    size_t n = (size_t) (end - begin);
    char *out = (char *) malloc(n + 1);

    if (out == NULL) {
        return NULL;
    }
    memcpy(out, begin, n);
    out[n] = '\0';
    return out;
}

/* "<start>:<end>", both non-negative decimal.  Returns 0 on success. */
static int parse_span(const char *s, long *start, long *end)
{
    char *rest = NULL;
    long a;
    long b;

    if (!isdigit((unsigned char) *s)) {
        return -1;
    }
    a = strtol(s, &rest, 10);
    if (rest == s || *rest != ':' || !isdigit((unsigned char) rest[1])) {
        return -1;
    }
    s = rest + 1;
    b = strtol(s, &rest, 10);
    if (rest == s || *rest != '\0') {
        return -1;
    }
    *start = a;
    *end = b;
    return 0;
}

/* Parses one record.  The first blank line, or the end of the input, ends it.
 * Returns 0 on success, EXIT_USAGE on a malformed line. */
static int read_tokens_text(char *text, token_vec *v)
{
    char *line = text;

    while (*line != '\0') {
        char *nl = strchr(line, '\n');
        char *stop = nl != NULL ? nl : line + strlen(line);
        char *fields[4];
        size_t nfields = 0;
        char *p;
        char *name;
        char *lexeme = NULL;
        long start = -1;
        long end = -1;

        if (stop > line && stop[-1] == '\r') {
            stop--;
        }
        if (stop == line) {
            break;              /* the blank line that ends the record */
        }
        /* Split on tabs: NAME, text, start:end -- at most three fields, so a
         * tab inside the lexeme text is a malformed line rather than a
         * silently mis-parsed one. */
        p = line;
        fields[nfields++] = p;
        while (p < stop) {
            if (*p == '\t') {
                *p = '\0';
                if (nfields == 3) {
                    fprintf(stderr,
                            "mdbparse: --tokens: more than three tab-separated "
                            "fields on a line (NAME<TAB>text[<TAB>start:end])\n");
                    return EXIT_USAGE;
                }
                fields[nfields++] = p + 1;
            }
            p++;
        }
        *stop = '\0';
        name = fields[0];
        if (*name == '\0') {
            fprintf(stderr, "mdbparse: --tokens: empty token name\n");
            return EXIT_USAGE;
        }
        if (nfields >= 2) {
            lexeme = fields[1];
        }
        if (nfields == 3) {
            if (parse_span(fields[2], &start, &end) != 0) {
                fprintf(stderr, "mdbparse: --tokens: '%s' is not <start>:<end>\n",
                        fields[2]);
                return EXIT_USAGE;
            }
        }
        {
            char *name_copy = dup_range(name, name + strlen(name));
            char *text_copy = lexeme != NULL
                ? dup_range(lexeme, lexeme + strlen(lexeme)) : NULL;

            if (name_copy == NULL || vec_push(v, name_copy, text_copy, start, end) != 0) {
                free(name_copy);
                free(text_copy);
                return EXIT_INTERNAL;
            }
        }
        if (nl == NULL) {
            break;
        }
        line = nl + 1;
    }
    return 0;
}

/* --- just enough JSON ---------------------------------------------------- */

typedef struct {
    const char *p;
    const char *end;
} jread;

static void j_ws(jread *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n'
                             || *j->p == '\r')) {
        j->p++;
    }
}

static int j_hex4(const char *p, unsigned *out)
{
    unsigned value = 0;
    int i;

    for (i = 0; i < 4; i++) {
        char c = p[i];
        value <<= 4;
        if (c >= '0' && c <= '9') {
            value |= (unsigned) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            value |= (unsigned) (c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            value |= (unsigned) (c - 'A' + 10);
        } else {
            return -1;
        }
    }
    *out = value;
    return 0;
}

static void utf8_put(char **out, unsigned cp)
{
    char *o = *out;

    if (cp < 0x80) {
        *o++ = (char) cp;
    } else if (cp < 0x800) {
        *o++ = (char) (0xC0 | (cp >> 6));
        *o++ = (char) (0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        *o++ = (char) (0xE0 | (cp >> 12));
        *o++ = (char) (0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char) (0x80 | (cp & 0x3F));
    } else {
        *o++ = (char) (0xF0 | (cp >> 18));
        *o++ = (char) (0x80 | ((cp >> 12) & 0x3F));
        *o++ = (char) (0x80 | ((cp >> 6) & 0x3F));
        *o++ = (char) (0x80 | (cp & 0x3F));
    }
    *out = o;
}

/* A JSON string at j->p (which must point at the opening quote).  Returns a
 * malloc'd UTF-8 C string, or NULL on a malformed one.  When `len_out` is not
 * NULL it also receives the DECODED BYTE LENGTH: a record's "sql" may carry a
 * NUL byte through a \u0000 escape, and the lexer is handed bytes and a
 * length, not a C string (MariaDB's own query buffer is bytes + length too). */
static char *j_string_n(jread *j, size_t *len_out)
{
    const char *p;
    const char *scan;
    char *out;
    char *o;

    if (j->p >= j->end || *j->p != '"') {
        return NULL;
    }
    p = j->p + 1;
    /* Bound the allocation by THIS string, not by the rest of the buffer: at
     * N tokens the latter costs O(N * filesize), which P3's batch reader would
     * inherit.  The scan below finds the closing quote under the same rule the
     * decode loop uses (a backslash consumes the next byte, and a hex digit of
     * a \uXXXX escape can never be a quote), so it stops in the same place; if
     * there is no closing quote it stops at j->end and the decode loop rejects
     * the string.  Every source byte yields at most one output byte except
     * \uXXXX, which is 6 source bytes for at most 4 output bytes, so the
     * scanned length is a safe bound either way. */
    scan = p;
    while (scan < j->end && *scan != '"') {
        scan += (*scan == '\\' && scan + 1 < j->end) ? 2 : 1;
    }
    out = (char *) malloc((size_t) (scan - p) + 1);
    if (out == NULL) {
        return NULL;
    }
    o = out;
    while (p < j->end && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p >= j->end) {
                free(out);
                return NULL;
            }
            switch (*p) {
            case '"': *o++ = '"'; p++; break;
            case '\\': *o++ = '\\'; p++; break;
            case '/': *o++ = '/'; p++; break;
            case 'b': *o++ = '\b'; p++; break;
            case 'f': *o++ = '\f'; p++; break;
            case 'n': *o++ = '\n'; p++; break;
            case 'r': *o++ = '\r'; p++; break;
            case 't': *o++ = '\t'; p++; break;
            case 'u': {
                unsigned cp = 0;
                if (j->end - p < 5 || j_hex4(p + 1, &cp) != 0) {
                    free(out);
                    return NULL;
                }
                p += 5;
                if (cp >= 0xD800 && cp <= 0xDBFF && j->end - p >= 6
                    && p[0] == '\\' && p[1] == 'u') {
                    unsigned low = 0;
                    if (j_hex4(p + 2, &low) == 0 && low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        p += 6;
                    }
                }
                utf8_put(&o, cp);
                break;
            }
            default:
                free(out);
                return NULL;
            }
        } else {
            *o++ = *p++;
        }
    }
    if (p >= j->end) {
        free(out);
        return NULL;
    }
    j->p = p + 1;               /* past the closing quote */
    *o = '\0';
    if (len_out != NULL) {
        *len_out = (size_t) (o - out);
    }
    return out;
}

static char *j_string(jread *j)
{
    return j_string_n(j, NULL);
}

static int j_skip_value(jread *j);

static int j_skip_container(jread *j, char open, char close)
{
    int depth = 0;

    while (j->p < j->end) {
        if (*j->p == '"') {
            char *s = j_string(j);
            if (s == NULL) {
                return -1;
            }
            free(s);
            continue;
        }
        if (*j->p == open) {
            depth++;
        } else if (*j->p == close) {
            depth--;
            if (depth == 0) {
                j->p++;
                return 0;
            }
        }
        j->p++;
    }
    return -1;
}

static int j_skip_value(jread *j)
{
    j_ws(j);
    if (j->p >= j->end) {
        return -1;
    }
    if (*j->p == '"') {
        char *s = j_string(j);
        if (s == NULL) {
            return -1;
        }
        free(s);
        return 0;
    }
    if (*j->p == '{') {
        return j_skip_container(j, '{', '}');
    }
    if (*j->p == '[') {
        return j_skip_container(j, '[', ']');
    }
    while (j->p < j->end && *j->p != ',' && *j->p != '}' && *j->p != ']'
           && *j->p != ' ' && *j->p != '\n' && *j->p != '\t' && *j->p != '\r') {
        j->p++;
    }
    return 0;
}

/* `["NAME", ...]` or `[{"name": ..., "text": ...}, ...]`, read from a jread
 * positioned at the array's '[' and left just past its ']'.  Unknown object
 * keys are skipped, so a corpus record's token objects read here unchanged.
 * `what` names the caller in every diagnostic ("--tokens-json", or
 * "--jsonl: line 124"), which is how batch mode gets the line number into its
 * messages without a second copy of this reader.
 * Returns 0, EXIT_USAGE on malformed JSON, EXIT_INTERNAL on a failed
 * allocation. */
static int read_token_array(jread *j_in, token_vec *v, const char *what)
{
    jread j = *j_in;

    j_ws(&j);
    if (j.p >= j.end || *j.p != '[') {
        fprintf(stderr, "mdbparse: %s: expected a JSON array\n", what);
        return EXIT_USAGE;
    }
    j.p++;
    j_ws(&j);
    if (j.p < j.end && *j.p == ']') {
        j.p++;
        goto done;
    }
    for (;;) {
        char *name = NULL;
        char *lexeme = NULL;

        j_ws(&j);
        if (j.p >= j.end) {
            goto bad;
        }
        if (*j.p == '"') {
            name = j_string(&j);
            if (name == NULL) {
                goto bad;
            }
        } else if (*j.p == '{') {
            j.p++;
            j_ws(&j);
            if (j.p < j.end && *j.p == '}') {
                j.p++;
            } else {
                for (;;) {
                    char *key;

                    j_ws(&j);
                    key = j_string(&j);
                    if (key == NULL) {
                        free(name);
                        free(lexeme);
                        goto bad;
                    }
                    j_ws(&j);
                    if (j.p >= j.end || *j.p != ':') {
                        free(key);
                        free(name);
                        free(lexeme);
                        goto bad;
                    }
                    j.p++;
                    j_ws(&j);
                    if (strcmp(key, "name") == 0 || strcmp(key, "text") == 0) {
                        char **slot = strcmp(key, "name") == 0 ? &name : &lexeme;
                        if (j.p < j.end && *j.p == '"') {
                            char *value = j_string(&j);
                            if (value == NULL) {
                                free(key);
                                free(name);
                                free(lexeme);
                                goto bad;
                            }
                            free(*slot);
                            *slot = value;
                        } else if (j_skip_value(&j) != 0) {
                            free(key);
                            free(name);
                            free(lexeme);
                            goto bad;
                        }
                    } else if (j_skip_value(&j) != 0) {
                        free(key);
                        free(name);
                        free(lexeme);
                        goto bad;
                    }
                    free(key);
                    j_ws(&j);
                    if (j.p < j.end && *j.p == ',') {
                        j.p++;
                        continue;
                    }
                    if (j.p < j.end && *j.p == '}') {
                        j.p++;
                        break;
                    }
                    free(name);
                    free(lexeme);
                    goto bad;
                }
            }
            if (name == NULL) {
                free(lexeme);
                fprintf(stderr,
                        "mdbparse: %s: a token object has no \"name\"\n", what);
                return EXIT_USAGE;
            }
        } else {
            goto bad;
        }
        if (vec_push(v, name, lexeme, -1, -1) != 0) {
            free(name);
            free(lexeme);
            return EXIT_INTERNAL;
        }
        j_ws(&j);
        if (j.p < j.end && *j.p == ',') {
            j.p++;
            continue;
        }
        if (j.p < j.end && *j.p == ']') {
            j.p++;
            break;
        }
        goto bad;
    }
done:
    *j_in = j;
    return 0;
bad:
    fprintf(stderr, "mdbparse: %s: malformed JSON\n", what);
    return EXIT_USAGE;
}

/* --tokens-json: the whole input is ONE token array and nothing else. */
static int read_tokens_json(const char *text, size_t len, token_vec *v)
{
    jread j;
    int rc;

    j.p = text;
    j.end = text + len;
    rc = read_token_array(&j, v, "--tokens-json");
    if (rc != 0) {
        return rc;
    }
    j_ws(&j);
    if (j.p != j.end) {
        fprintf(stderr, "mdbparse: --tokens-json: malformed JSON\n");
        return EXIT_USAGE;
    }
    return 0;
}

/* --- one --jsonl record --------------------------------------------------- */

/* One input line of batch mode: either a JSON object with a "tokens" array
 * (every other key skipped, so a corpus/expected jsonl line is valid input
 * byte for byte) or a bare JSON array of token names.
 *
 * On return: *blank is 1 for an empty or whitespace-only line, which produces
 * no output object at all (this is what the oracle's own --stdin-jsonl does);
 * *id_out is NULL or a malloc'd copy of the record's "id" (the caller owns it
 * on every path, success or failure).
 *
 * Returns 0, EXIT_USAGE for a malformed line or an "id" that is present and
 * is not a string, EXIT_INTERNAL for a failed allocation.  An "id" that is
 * present and not a string is refused rather than nulled: batch output is
 * aligned to batch input by `id`, and P4 diffs it record by record, so a
 * silently dropped one would misalign the comparison instead of failing it. */
static int read_jsonl_record(const char *line, size_t len, token_vec *v,
                             char **id_out, unsigned long lineno, int *blank,
                             char **sql_out, size_t *sql_len_out)
{
    char what[64];
    jread j;
    int seen_tokens = 0;
    int seen_sql = 0;
    int rc;

    snprintf(what, sizeof what, "--jsonl: line %lu", lineno);
    *id_out = NULL;
    *blank = 0;
    *sql_out = NULL;
    *sql_len_out = 0;
    j.p = line;
    j.end = line + len;

    j_ws(&j);
    if (j.p >= j.end) {
        *blank = 1;
        return 0;
    }
    if (*j.p == '[') {
        rc = read_token_array(&j, v, what);
        if (rc != 0) {
            return rc;
        }
    } else if (*j.p == '{') {
        j.p++;
        j_ws(&j);
        if (j.p < j.end && *j.p == '}') {
            j.p++;
        } else {
            for (;;) {
                char *key;
                int is_tokens;
                int is_id;
                int is_sql;

                j_ws(&j);
                key = j_string(&j);
                if (key == NULL) {
                    goto bad;
                }
                is_tokens = strcmp(key, "tokens") == 0;
                is_id = strcmp(key, "id") == 0;
                is_sql = strcmp(key, "sql") == 0;
                free(key);
                j_ws(&j);
                if (j.p >= j.end || *j.p != ':') {
                    goto bad;
                }
                j.p++;
                j_ws(&j);
                if (is_tokens) {
                    if (seen_tokens) {
                        vec_reset(v);   /* a duplicated key: the last wins */
                    }
                    rc = read_token_array(&j, v, what);
                    if (rc != 0) {
                        return rc;
                    }
                    seen_tokens = 1;
                } else if (is_sql) {
                    char *value;
                    size_t value_len = 0;

                    if (j.p >= j.end || *j.p != '"') {
                        fprintf(stderr,
                                "mdbparse: %s: \"sql\" is present but is not a "
                                "JSON string\n", what);
                        return EXIT_USAGE;
                    }
                    value = j_string_n(&j, &value_len);
                    if (value == NULL) {
                        goto bad;
                    }
                    free(*sql_out);       /* a duplicated key: the last wins */
                    *sql_out = value;
                    *sql_len_out = value_len;
                    seen_sql = 1;
                } else if (is_id) {
                    char *value;

                    if (j.p >= j.end || *j.p != '"') {
                        fprintf(stderr,
                                "mdbparse: %s: \"id\" is present but is not a "
                                "JSON string\n", what);
                        return EXIT_USAGE;
                    }
                    value = j_string(&j);
                    if (value == NULL) {
                        goto bad;
                    }
                    free(*id_out);
                    *id_out = value;
                } else if (j_skip_value(&j) != 0) {
                    goto bad;
                }
                j_ws(&j);
                if (j.p < j.end && *j.p == ',') {
                    j.p++;
                    continue;
                }
                if (j.p < j.end && *j.p == '}') {
                    j.p++;
                    break;
                }
                goto bad;
            }
        }
        /* Exactly one of "sql" and "tokens".  Both, or neither, is a usage
         * error naming the line, like every other malformed record here: a
         * record that carries both does not say which one the run measured. */
        if (seen_tokens && seen_sql) {
            fprintf(stderr,
                    "mdbparse: %s: the record has both \"sql\" and \"tokens\"; "
                    "exactly one is required\n", what);
            return EXIT_USAGE;
        }
        if (!seen_tokens && !seen_sql) {
            fprintf(stderr,
                    "mdbparse: %s: the record has neither a \"sql\" string nor "
                    "a \"tokens\" array\n", what);
            return EXIT_USAGE;
        }
    } else {
        goto bad;
    }
    j_ws(&j);
    if (j.p != j.end) {
        goto bad;
    }
    return 0;
bad:
    fprintf(stderr, "mdbparse: %s: malformed JSON\n", what);
    return EXIT_USAGE;
}

/* --- JSON output ---------------------------------------------------------- */

static void json_string(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char) *s;

        switch (c) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b", out); break;
        case '\f': fputs("\\f", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (c < 0x20) {
                fprintf(out, "\\u%04x", c);
            } else {
                fputc((int) c, out);
            }
        }
    }
    fputc('"', out);
}

/* --- the verdict ---------------------------------------------------------- */

typedef struct {
    /* NULL -> "id": null, which is every single-record verdict; batch mode
     * fills it from the input record (tools/schemas/verdict.json types
     * it ["string", "null"] for exactly this reason). */
    const char *id;
    int accepted;
    const char *status;
    int have_derivation;
    const int *derivation;
    size_t nderivation;
    size_t tokens_consumed;
    int have_error;
    size_t at_index;
    const char *error_token;
    int error_state;            /* < 0 -> null */
    long byte_pos;              /* < 0 -> null */
    const char *const *expected;
    size_t nexpected;
    int have_expected;
    long wall_us;
    /* What the LEXER ended the stream on ("$end" or "ABORT_SYM"); NULL means
     * MDBP_EOF_TOKEN, which is what a tokens-in record always reports. */
    const char *eof_token;
    /* --dump-tokens in batch mode: the record's own token stream. */
    int have_tokens;
    const mdbp_token *tokens;
    size_t ntokens;
} verdict;

static void print_verdict(const verdict *v)
{
    size_t i;

    fputs("{\"id\": ", stdout);
    if (v->id == NULL) {
        fputs("null", stdout);
    } else {
        json_string(stdout, v->id);
    }
    fputs(", \"accepted\": ", stdout);
    fputs(v->accepted ? "true" : "false", stdout);
    fputs(", \"status\": ", stdout);
    json_string(stdout, v->status);
    fputs(", \"derivation\": ", stdout);
    if (!v->have_derivation) {
        fputs("null", stdout);
    } else {
        fputc('[', stdout);
        for (i = 0; i < v->nderivation; i++) {
            fprintf(stdout, "%s%d", i ? ", " : "", v->derivation[i]);
        }
        fputc(']', stdout);
    }
    fprintf(stdout, ", \"tokens_consumed\": %zu", v->tokens_consumed);
    fputs(", \"eof_token\": ", stdout);
    json_string(stdout, v->eof_token != NULL ? v->eof_token : MDBP_EOF_TOKEN);
    fputs(", \"error\": ", stdout);
    if (!v->have_error) {
        fputs("null", stdout);
    } else {
        fprintf(stdout, "{\"at_index\": %zu, \"token\": ", v->at_index);
        json_string(stdout, v->error_token);
        if (v->error_state >= 0) {
            fprintf(stdout, ", \"state\": %d", v->error_state);
        } else {
            fputs(", \"state\": null", stdout);
        }
        if (v->byte_pos >= 0) {
            fprintf(stdout, ", \"byte_pos\": %ld", v->byte_pos);
        } else {
            fputs(", \"byte_pos\": null", stdout);
        }
        fputs(", \"expected_sample\": ", stdout);
        if (!v->have_expected) {
            fputs("null", stdout);
        } else {
            fputc('[', stdout);
            for (i = 0; i < v->nexpected; i++) {
                if (i) {
                    fputs(", ", stdout);
                }
                json_string(stdout, v->expected[i]);
            }
            fputc(']', stdout);
        }
        fputc('}', stdout);
    }
    if (v->have_tokens) {
        fputs(", \"tokens\": [", stdout);
        for (i = 0; i < v->ntokens; i++) {
            if (i) {
                fputs(", ", stdout);
            }
            json_string(stdout, v->tokens[i].name);
        }
        fputs("], \"spans\": [", stdout);
        for (i = 0; i < v->ntokens; i++) {
            fprintf(stdout, "%s[%ld, %ld]", i ? ", " : "",
                    v->tokens[i].byte_start, v->tokens[i].byte_end);
        }
        fputc(']', stdout);
    }
    fprintf(stdout, ", \"wall_us\": %ld}\n", v->wall_us);
}

/* --- --lex-only output ---------------------------------------------------- */

/* mdbp_status spelled the way every verdict object here spells it. */
static const char *lex_status_name(mdbp_status st)
{
    switch (st) {
    case MDBP_OK: return "ok";
    case MDBP_UNSUPPORTED: return "unsupported";
    default: return "internal";
    }
}

/* One mdbp_lex_sql()/mdbp_lex_sql_prefix() result as a JSON object (plan
 * `tokeniser-entry-points.md` Contract, CLI; plan `lex-sql-prefix.md` D-9 for
 * `with_more`/`more`).  `with_id` is 1 in batch mode, where every object
 * carries the record's "id" (null when the record has none); a single
 * record's object has no "id" key at all.  There is no verdict to print: the
 * call parses nothing.
 *
 * `with_more` is 0 for a call with no `--max-tokens`: the object is BYTE FOR
 * BYTE what it always printed (D-9), `eof_token` falling back to
 * MDBP_EOF_TOKEN on NULL as before, and no "more" key at all.  `with_more` is
 * 1 for `--max-tokens`: `eof_token` prints null on NULL rather than falling
 * back (nothing ended a cut stream, D-5), and a "more" key follows it. */
static void print_lex_object(int with_id, const char *id, mdbp_status st,
                             const mdbp_result *res, int with_more, int more)
{
    size_t i;

    fputc('{', stdout);
    if (with_id) {
        fputs("\"id\": ", stdout);
        if (id == NULL) {
            fputs("null", stdout);
        } else {
            json_string(stdout, id);
        }
        fputs(", ", stdout);
    }
    fputs("\"status\": ", stdout);
    json_string(stdout, lex_status_name(st));
    fputs(", \"tokens\": [", stdout);
    for (i = 0; i < res->ntokens; i++) {
        if (i) {
            fputs(", ", stdout);
        }
        json_string(stdout, res->tokens[i].name);
    }
    fputs("], \"spans\": [", stdout);
    for (i = 0; i < res->ntokens; i++) {
        fprintf(stdout, "%s[%ld, %ld]", i ? ", " : "",
                res->tokens[i].byte_start, res->tokens[i].byte_end);
    }
    fputs("], \"eof_token\": ", stdout);
    if (with_more) {
        if (res->eof_token != NULL) {
            json_string(stdout, res->eof_token);
        } else {
            fputs("null", stdout);
        }
        fprintf(stdout, ", \"more\": %s}\n", more ? "true" : "false");
    } else {
        json_string(stdout, res->eof_token != NULL ? res->eof_token : MDBP_EOF_TOKEN);
        fputs("}\n", stdout);
    }
}

/* The batch object for a --jsonl record that carries no "sql" string -- a
 * bare token array, or an object with only "tokens".  There is nothing to
 * lex, so it is a per-record "unsupported", and the batch goes on, exactly as
 * an unknown terminal name does on the parse path. */
static void print_lex_needs_sql(const char *id)
{
    fputs("{\"id\": ", stdout);
    if (id == NULL) {
        fputs("null", stdout);
    } else {
        json_string(stdout, id);
    }
    fputs(", \"status\": \"unsupported\", \"message\": "
          "\"--lex-only needs an sql record\"}\n", stdout);
}

/* --- --help ---------------------------------------------------------------- */

static void usage(FILE *out)
{
    fputs(
"mdbparse -- MariaDB's LALR grammar, extracted by script, as a C parser.\n"
"\n"
"WHAT A VERDICT MEANS.  This recognises the GRAMMAR-ONLY language: everything\n"
"MariaDB's LALR automaton accepts BEFORE any C action gets a chance to object.\n"
"Every action body was replaced, mechanically, by a stub that records the\n"
"reduction (PLAN.md S2.1), so this accepts a SUPERSET of what mariadbd accepts.\n"
"A token stream it accepts is not guaranteed to be valid SQL; a token stream it\n"
"rejects is guaranteed to be invalid.  MariaDB grammar variant only -- the\n"
"Oracle variant is out of scope, and --sql-mode ORACLE/MSSQL exits 3 rather\n"
"than being silently ignored.\n"
"\n"
"TWO WAYS IN.  --sql/--file lex the SQL with MariaDB's own lexer, sliced out\n"
"of sql/sql_lex.cc by script (PLAN.md P5b).  --tokens/--tokens-json/--jsonl\n"
"take a vector of terminal NAMES in the grammar's own spelling instead:\n"
"SELECT_SYM, IDENT_QUOTED, \"'('\" (character literals keep their quotes),\n"
"normally ending in the terminal END_OF_INPUT.\n"
"\n"
"usage: mdbparse --sql TEXT | --file FILE|- | --tokens FILE|-\n"
"              | --tokens-json FILE|- | --jsonl FILE|- [options]\n"
"\n"
"  --sql TEXT             lex + parse one statement\n"
"  --file FILE|-          the same, from a file ('-' = stdin).  Input must be\n"
"                         valid UTF-8 through either channel (the connection\n"
"                         charset is utf8mb4); undecodable bytes are exit 2.\n"
"                         --sql and --file exclude each other and --tokens,\n"
"                         --tokens-json and --jsonl\n"
"  --dump-tokens          single-record: print ONLY the token lines,\n"
"                         NAME<TAB>text<TAB>start:end, with byte offsets and\n"
"                         text escaped so one token is always one line.\n"
"                         batch (--jsonl): add \"tokens\" and \"spans\" to each\n"
"                         verdict object\n"
"  --valid live|none|all|list\n"
"                         where valid(T) answers come from -- the substitute\n"
"                         for MariaDB's curr_sel->parsing_place, which parser\n"
"                         ACTIONS set and this parser's stubs cannot (PLAN.md\n"
"                         S1.8).  DEFAULT live: the parser answers, from the\n"
"                         LR stack it is on when the lexer asks, using bison's\n"
"                         own yy_lac() under %define parse.lac full.  live is\n"
"                         the mode that corresponds to `lexdump.py --valid\n"
"                         replay`, and in it the parser and the lexer run\n"
"                         together, one pulling the other, as the server does.\n"
"                         none: nothing is valid (parsing_place == NO_MATTER).\n"
"                         all: everything is.  list: the fixed set below.\n"
"                         Ignored by --tokens/--tokens-json and by --jsonl\n"
"                         records that carry a \"tokens\" array: there is no\n"
"                         lexer on that path\n"
"  --valid-symbols A,B    the fixed set for --valid list\n"
"  --parsing-place NAME   THE DECLARED PARSER CONTEXT, for the static --valid\n"
"                         modes only.  A static mode runs the lexer with NO\n"
"                         PARSER: the record is lexed to completion and the\n"
"                         vector handed to the parser afterwards, so nothing\n"
"                         can write MariaDB's curr_sel->parsing_place and it\n"
"                         stays NO_MATTER.  This writes it once, before the\n"
"                         first token is scanned, so a test can state the\n"
"                         parser context its expected token stream assumes\n"
"                         instead of implying it.  MariaDB's own sliced lexer\n"
"                         then SPENDS the value exactly as it does in a real\n"
"                         parse.  NAME is an enum_parsing_place value\n"
"                         (sql/sql_priv.h): NO_MATTER (the default), IN_HAVING,\n"
"                         SELECT_LIST, IN_WHERE, IN_ON, IN_GROUP_BY,\n"
"                         IN_ORDER_BY, IN_UPDATE_ON_DUP_KEY, IN_PART_FUNC,\n"
"                         BEFORE_OPT_LIST, AFTER_LIST, FOR_LOOP_BOUND,\n"
"                         IN_RETURNING.  REFUSED (exit 2) with --valid live,\n"
"                         where the parser's own lifted actions write the\n"
"                         field (tools/stub.py's preserve_parsing_place\n"
"                         rule; build/stub-map.json enumerates every one)\n"
"  --lex-only             LEX, DO NOT PARSE, through the library's\n"
"                         mdbp_lex_sql(): the whole input as one token stream,\n"
"                         any number of statements, and no verdict.  It lexes\n"
"                         with NO parser, which is --valid none with the\n"
"                         default --parsing-place.  With --sql/--file it\n"
"                         prints {\"status\", \"tokens\", \"spans\",\n"
"                         \"eof_token\"}, or only the token lines with\n"
"                         --dump-tokens.  With --jsonl, one such object per\n"
"                         record, with \"id\"; a record with no \"sql\" string\n"
"                         gets \"status\": \"unsupported\" and the batch goes\n"
"                         on.  REFUSED (exit 2) with --tokens, --tokens-json,\n"
"                         --parsing-place, or any --valid other than none\n"
"  --max-tokens N         with lex-only mode: mdbp_lex_sql_prefix() instead\n"
"                         of mdbp_lex_sql() -- at most N counted tokens\n"
"                         (END_OF_INPUT does not count; ABORT_SYM does), or\n"
"                         the whole stream when it has N counted tokens or\n"
"                         fewer.  The object gains \"more\": true|false, and\n"
"                         \"eof_token\" prints null when \"more\" is true.\n"
"                         N is digits only; a number too long saturates,\n"
"                         which means no limit.  Given twice, the last\n"
"                         value wins.  REFUSED (exit 2) outside lex-only mode\n"
"  --dump-state-maps      print the two 256-byte tables the lexer is using,\n"
"                         as state_map<TAB><512 hex> / ident_map<TAB><512 hex>,\n"
"                         and exit 0\n"
"  --tokens FILE|-        NAME<TAB>text[<TAB>start:end] per line; a bare NAME\n"
"                         line is allowed; the first blank line, or the end of\n"
"                         the input, ends the record\n"
"  --tokens-json FILE|-   [\"NAME\", ...] or [{\"name\": ..., \"text\": ...}, ...]\n"
"                         (a FILE, unlike replay.py's inline --tokens-json)\n"
"  --jsonl FILE|-         BATCH: one record per line, streamed, one process for\n"
"                         the whole file.  A record is a JSON object with a\n"
"                         \"tokens\" array or a \"sql\" string -- exactly one of\n"
"                         the two, and every other key is ignored, so a\n"
"                         corpus/expected/*.jsonl line is valid input verbatim\n"
"                         -- or a bare [\"NAME\", ...] array.  One verdict object\n"
"                         per non-blank line, on stdout, in input order, with\n"
"                         \"id\" taken from the record.  A BATCH RUN EXITS 0\n"
"                         WHATEVER THE PER-RECORD VERDICTS ARE: a rejection, an\n"
"                         unknown terminal name (\"status\": \"unsupported\") and\n"
"                         an internal error are fields of the object, not exit\n"
"                         codes.  Only a malformed line, an unreadable file or\n"
"                         a non-string \"id\" (2), or a failed allocation (1),\n"
"                         end the run.  Mutually exclusive with --tokens,\n"
"                         --tokens-json and --verdict-exit\n"
"  --json                 write the verdict object to stdout (the default)\n"
"  --quiet                write no stdout object\n"
"  --stats                one summary line on stderr (in batch mode: records,\n"
"                         the four verdict counts, the summed parse time and\n"
"                         the process's own peak RSS)\n"
"  --derivation           report the rule-number array (the default)\n"
"  --no-derivation        report \"derivation\": null\n"
"  --sql-mode LIST        comma-separated; empty by default.  ORACLE or MSSQL\n"
"                         exits 3 rather than looking honoured; ANSI_QUOTES,\n"
"                         PIPES_AS_CONCAT, HIGH_NOT_PRECEDENCE, IGNORE_SPACE\n"
"                         and NO_BACKSLASH_ESCAPES reach the lexer; any other\n"
"                         element is a server mode with no lexical effect and\n"
"                         is accepted and inert\n"
"                         except ANSI, POSTGRESQL, DB2, MAXDB, MYSQL40 and\n"
"                         MYSQL323, which are expanded as the server expands\n"
"                         them\n"
"  --charset NAME         the connection charset; utf8mb4 by default.  Only\n"
"                         utf8mb4 and its alias utf8mb4_general_ci are\n"
"                         accepted; any other name (utf8, utf8mb3, latin1,\n"
"                         ...) exits 3 rather than looking honoured\n"
"  --version-id N         the MariaDB release that versioned comments -- an\n"
"                         executable /*!NNNNNN ... */, MariaDB's own\n"
"                         /*M!NNNNNN ... */ and the reversed /*!!NNNNNN ... */\n"
"                         -- are judged against, as a release id:\n"
"                         major*10000 + minor*100 + patch, so 110803 for\n"
"                         11.8.3.  The default is the pinned MariaDB tree's\n"
"                         own release id.  It decides only which of those\n"
"                         comments are read as SQL, never the grammar.  Every\n"
"                         path that lexes honours it -- --valid live, the\n"
"                         static --valid modes and the lex-only path, for\n"
"                         --sql, --file and --jsonl alike; --tokens and\n"
"                         --tokens-json lex nothing and it changes nothing\n"
"                         there.  N that is not a decimal integer (digits\n"
"                         only) exits 2; an N the library refuses as a\n"
"                         release id exits 3\n"
"  --prepare-mode         the default: '?' is PARAM_MARKER\n"
"  --no-prepare-mode      '?' stays the character token\n"
"  --verdict-exit         exit 4 on a rejection instead of 0 (off by default;\n"
"                         the corpus sweep never uses it, and it is refused\n"
"                         with --jsonl: a per-record verdict is not a process\n"
"                         exit code)\n"
"  --help                 this text\n"
"\n"
"Not implemented here; it exits 3 naming the phase that owns it:\n"
"  --stub-map (P4)\n"
"\n"
"Exit codes: 0 success, INCLUDING a well-formed rejection (the verdict is in\n"
"the JSON, so a sweep can tell a rejection from a crash) | 1 internal error |\n"
"2 usage or unreadable input | 3 unsupported input | 4 see --verdict-exit.\n",
        out);
}

/* --- batch mode ------------------------------------------------------------ */

/* One process, one parse loop, for a whole corpus file.  Nothing is spawned
 * per record; the input is STREAMED with getline (mysql_test.jsonl is ~189 MB
 * and its longest line 977,744 bytes, so a slurp is not an option), and the
 * growable things -- getline's line buffer, this file's token vector, and the
 * token array and derivation buffer the one mdbp_parser owns -- are reused
 * across records, so the run's memory converges on the largest record instead
 * of growing with the record COUNT.  stdout stays fully buffered and is flushed once, at the end: an
 * fflush per record would show up as a syscall per record over 212,332 of
 * them.
 *
 * Returns the process exit code: 0 when every non-blank line produced an
 * object (whatever those objects say), 2 for a malformed line / unreadable
 * input / a bad "id", 1 for an allocation failure -- and ENOMEM is an
 * ALLOCATION failure wherever it surfaces, including from the fopen() and the
 * getline() below, which is why neither of them reports it as "unreadable
 * input".  --stats is a summary of a completed run, so it is printed on the
 * exit-0 path only: a run that stopped on line N has no total to report. */
static int run_batch(const char *path, mdbp_parser *parser, int want_derivation,
                     int quiet, int stats, const lex_config *cfg,
                     int dump_tokens, int lex_only)
{
    FILE *f = NULL;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t got;
    unsigned long lineno = 0;
    token_vec tokens = { NULL, 0, 0 };
    mdbp_result res;
    char *id = NULL;
    char *sql = NULL;
    size_t sql_len = 0;
    size_t n_records = 0;
    size_t n_accepted = 0;
    size_t n_rejected = 0;
    size_t n_unsupported = 0;
    size_t n_internal = 0;
    long total_wall_us = 0;
    int rc = EXIT_SUCCESS_OK;
    int more = 0;   /* mdbp_lex_sql_prefix()'s *more output (--max-tokens) */

    if (strcmp(path, "-") == 0) {
        f = stdin;
    } else {
        f = fopen(path, "rb");
        if (f == NULL) {
            int e = errno;

            /* An open that fails for want of MEMORY (fopen allocates the FILE
             * and its buffer) is an allocation failure that prevents producing
             * output at all -- exit 1, like every other allocation failure in
             * this mode -- not an unreadable file.  ENOENT / EACCES / EISDIR
             * and friends stay exit 2. */
            if (e == ENOMEM) {
                fprintf(stderr, "mdbparse: --jsonl: %s: out of memory\n", path);
                return EXIT_INTERNAL;
            }
            fprintf(stderr, "mdbparse: %s: %s\n", path, strerror(e));
            return EXIT_USAGE;
        }
    }

    /* ONE parser for the whole file (main() built it): the library reuses its
     * derivation buffer and its token array across records, which is what
     * makes a 212,332-record run pay for the largest record rather than for
     * every record (include/mdbparse.h's ownership rule is exactly this bargain).
     * Nothing is spawned, nothing is re-initialised, per line. */

    /* errno is cleared before EVERY getline() so that the post-loop check can
     * tell "the line buffer could not be grown" (ENOMEM) from "end of file";
     * everything in the loop body -- clock_gettime, fwrite, ... -- is free to
     * set errno on paths that succeed. */
    while (errno = 0, (got = getline(&line, &line_cap, f)) != -1) {
        size_t len = (size_t) got;
        int blank = 0;
        verdict v;
        struct timespec t0;
        struct timespec t1;
        long wall_us = 0;
        mdbp_status st = MDBP_OK;
        const mdbp_token *rec_tokens = NULL;
        size_t rec_ntokens = 0;
        int parsed = 0;         /* --valid live: mdbp_parse_sql() already ran */

        lineno++;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            len--;
        }
        vec_reset(&tokens);
        free(id);
        id = NULL;
        free(sql);
        sql = NULL;
        sql_len = 0;
        rc = read_jsonl_record(line, len, &tokens, &id, lineno, &blank,
                               &sql, &sql_len);
        if (rc != 0) {
            if (rc == EXIT_INTERNAL) {
                fprintf(stderr, "mdbparse: --jsonl: line %lu: out of memory\n",
                        lineno);
            }
            goto done;
        }
        if (blank) {
            continue;           /* no object at all, exactly like the oracle */
        }

        if (lex_only) {
            /* --lex-only: mdbp_lex_sql() per record, or mdbp_lex_sql_prefix()
             * with --max-tokens (task P1.1, plan `lex-sql-prefix.md` D-9), and
             * nothing is parsed.  A record that carries no SQL has nothing to
             * lex; it is a per-record "unsupported" and the batch goes on.
             * Undecodable bytes stay the usage error they are on every other
             * --jsonl path, from the same predicate the library applies. */
            if (sql == NULL) {
                n_records++;
                n_unsupported++;
                if (!quiet) {
                    print_lex_needs_sql(id);
                }
                continue;
            }
            if (!mdbp_input_is_utf8(sql, sql_len)) {
                fprintf(stderr,
                        "mdbparse: --jsonl: line %lu: \"sql\" is not valid "
                        "UTF-8 (the connection charset is utf8mb4)\n", lineno);
                rc = EXIT_USAGE;
                goto done;
            }
            if (cfg->max_tokens_given) {
                st = mdbp_lex_sql_prefix(parser, sql, sql_len,
                                         (size_t) cfg->max_tokens, &res, &more);
            } else {
                st = mdbp_lex_sql(parser, sql, sql_len, &res);
            }
            if (st == MDBP_INTERNAL && res.tokens == NULL) {
                fprintf(stderr, "mdbparse: --jsonl: line %lu: out of memory\n",
                        lineno);
                rc = EXIT_INTERNAL;
                goto done;
            }
            n_records++;
            if (st == MDBP_OK) {
                n_accepted++;
            } else if (st == MDBP_UNSUPPORTED) {
                n_unsupported++;
            } else {
                n_internal++;
            }
            if (!quiet) {
                print_lex_object(1, id, st, &res, cfg->max_tokens_given, more);
            }
            continue;
        }

        memset(&v, 0, sizeof v);
        v.id = id;
        v.status = "ok";
        v.error_state = -1;
        v.byte_pos = -1;
        v.eof_token = MDBP_EOF_TOKEN;

        if (sql != NULL) {
            /* The record carried SQL, so this run lexes it.  One process for
             * the whole file: the lexer is created and freed per record, but
             * nothing is forked and the token vector is reused. */
            if (!mdbp_input_is_utf8(sql, sql_len)) {
                fprintf(stderr,
                        "mdbparse: --jsonl: line %lu: \"sql\" is not valid "
                        "UTF-8 (the connection charset is utf8mb4)\n", lineno);
                rc = EXIT_USAGE;
                goto done;
            }
            if (cfg->valid == VALID_LIVE) {
                /* ONE run through the library: mdbp_parse_sql() has the
                 * parser pull each token as the lexer makes it, which is the
                 * only way valid(T) can be answered from the live LR stack.
                 * The token stream it reports is the parser's, not ours. */
                clock_gettime(CLOCK_MONOTONIC, &t0);
                st = mdbp_parse_sql(parser, sql, sql_len, &res);
                clock_gettime(CLOCK_MONOTONIC, &t1);
                wall_us = (long) ((t1.tv_sec - t0.tv_sec) * 1000000L
                                  + (t1.tv_nsec - t0.tv_nsec) / 1000L);
                parsed = 1;
                if (st == MDBP_INTERNAL && res.tokens == NULL) {
                    /* include/mdbparse.h: a NULL token stream on MDBP_INTERNAL is
                     * "the stream never got built", i.e. this process cannot
                     * make progress -- as opposed to "this record has no
                     * verdict", which is a per-record object below. */
                    fprintf(stderr,
                            "mdbparse: --jsonl: line %lu: out of memory\n",
                            lineno);
                    rc = EXIT_INTERNAL;
                    goto done;
                }
                rec_tokens = res.tokens;
                rec_ntokens = res.ntokens;
                v.eof_token = res.eof_token;
            } else {
                rc = lex_record(sql, sql_len, cfg, &tokens, &v.eof_token);
                if (rc != 0) {
                    fprintf(stderr,
                            "mdbparse: --jsonl: line %lu: out of memory\n",
                            lineno);
                    goto done;
                }
            }
        }
        if (!parsed) {
            rec_tokens = tokens.items;
            rec_ntokens = tokens.n;
        }
        if (dump_tokens) {
            v.have_tokens = 1;
            v.tokens = rec_tokens;
            v.ntokens = rec_ntokens;
        }

        if (!parsed) {
            clock_gettime(CLOCK_MONOTONIC, &t0);
            st = mdbp_parse_tokens(parser, tokens.items, tokens.n, &res);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            wall_us = (long) ((t1.tv_sec - t0.tv_sec) * 1000000L
                              + (t1.tv_nsec - t0.tv_nsec) / 1000L);
        }

        if (st == MDBP_UNSUPPORTED) {
            /* THE record class this mode exists to survive: a corpus record
             * naming a terminal this grammar does not have.  Single-record
             * mode exits 3 on one; here it is a verdict, the run continues,
             * and the process exit code does not move.  No wall_us either:
             * nothing was parsed, so there is nothing to bill. */
            v.accepted = 0;
            v.status = "unsupported";
            v.have_derivation = 0;
            v.have_error = 1;
            v.at_index = res.error_token_index;
            v.error_token = res.error_token;
            v.byte_pos = res.error_byte_pos;
            n_unsupported++;
        } else {
            total_wall_us += wall_us;

            v.wall_us = wall_us;
            v.tokens_consumed = res.tokens_consumed;
            v.have_derivation = want_derivation;
            v.derivation = res.derivation;
            v.nderivation = res.derivation_len;

            if (st == MDBP_INTERNAL) {
                v.accepted = 0;
                v.status = "internal";
                v.have_derivation = 0;
                n_internal++;
            } else if (res.accepted) {
                v.accepted = 1;
                n_accepted++;
            } else {
                v.accepted = 0;
                v.have_error = 1;
                v.at_index = res.error_token_index;
                v.error_token = res.error_token;
                v.error_state = res.error_state;
                v.byte_pos = res.error_byte_pos;
                v.expected = res.expected;
                v.nexpected = res.nexpected;
                v.have_expected = res.expected != NULL;
                n_rejected++;
            }
        }
        n_records++;
        if (!quiet) {
            print_verdict(&v);
        }
    }
    if (ferror(f) || (!feof(f) && errno == ENOMEM)) {
        int e = errno;

        /* getline() stopped short.  A stream error is unreadable input (2),
         * but a failure to GROW the line buffer is an allocation failure (1) --
         * and glibc's getdelim() reports that one with errno == ENOMEM without
         * setting the stream's error flag, hence the !feof() arm: without it a
         * record too big to buffer would silently truncate the run to exit 0. */
        if (e == ENOMEM) {
            fprintf(stderr, "mdbparse: --jsonl: line %lu: out of memory\n",
                    lineno + 1);
            rc = EXIT_INTERNAL;
        } else {
            fprintf(stderr, "mdbparse: %s: %s\n", path, strerror(e));
            rc = EXIT_USAGE;
        }
        goto done;
    }

    if (stats) {
        struct rusage usage_self;
        long rss_peak_kb = 0;

        if (getrusage(RUSAGE_SELF, &usage_self) == 0) {
            rss_peak_kb = usage_self.ru_maxrss;   /* KiB on Linux */
        }
        /* Once, so the objects are on stdout before the summary is on
         * stderr; not per record. */
        fflush(stdout);
        if (lex_only) {
            /* No verdicts to count: n_accepted counted the lexed records. */
            fprintf(stderr,
                    "mdbparse: records=%zu lexed=%zu unsupported=%zu "
                    "internal=%zu rss_peak_kb=%ld\n",
                    n_records, n_accepted, n_unsupported, n_internal,
                    rss_peak_kb);
        } else {
            fprintf(stderr,
                    "mdbparse: records=%zu accepted=%zu rejected=%zu "
                    "unsupported=%zu internal=%zu wall_us=%ld rss_peak_kb=%ld\n",
                    n_records, n_accepted, n_rejected, n_unsupported,
                    n_internal, total_wall_us, rss_peak_kb);
        }
    }

done:
    free(line);
    free(id);
    free(sql);
    vec_free(&tokens);
    if (f != stdin) {
        fclose(f);
    }
    return rc;
}

/* --- main ------------------------------------------------------------------ */

struct later_phase {
    const char *flag;
    const char *owner;
};

/* --sql, --file and --dump-tokens left this table in task 8.2: the lexer
 * links now.  --stub-map stays: P4 resolves rule numbers to sql_yacc.yy lines
 * in Python, and this binary has no map to answer from. */
static const struct later_phase LATER_PHASES[] = {
    { "--stub-map", "P4 (rule -> sql_yacc.yy:LINE, resolved in Python)" },
};

/* --version-id's N (plan `tokeniser-entry-points.md` P3).  A decimal integer
 * is ASCII digits and nothing else -- no sign, no space, no base prefix --
 * and anything else is a usage error (exit 2), decided here.  Whether the
 * number is a release id is the LIBRARY's question, answered by
 * mdbp_parser_set_version_id() (exit 3 when it refuses), so no range is
 * checked here.  A number too long for unsigned long is still a decimal
 * integer: it saturates to ULONG_MAX, which no release id reaches, so the
 * library refuses it like any other number out of range.  Returns 0, or -1
 * when `s` is not a decimal integer. */
static int parse_decimal(const char *s, unsigned long *out)
{
    unsigned long value = 0;
    int saturated = 0;
    const char *p;

    if (*s == '\0') {
        return -1;
    }
    for (p = s; *p != '\0'; p++) {
        unsigned long digit;

        if (*p < '0' || *p > '9') {
            return -1;
        }
        digit = (unsigned long) (*p - '0');
        if (saturated || value > (ULONG_MAX - digit) / 10) {
            saturated = 1;
        } else {
            value = value * 10 + digit;
        }
    }
    *out = saturated ? ULONG_MAX : value;
    return 0;
}

int main(int argc, char **argv)
{
    const char *tokens_path = NULL;
    const char *tokens_json_path = NULL;
    const char *jsonl_path = NULL;
    const char *sql_text = NULL;
    const char *sql_file = NULL;
    const char *sql_mode = "";
    const char *charset = "utf8mb4";
    lex_config cfg;
    int dump_tokens = 0;
    int dump_state_maps = 0;
    const char *lexed = NULL;   /* the bytes --sql/--file handed the lexer */
    size_t lexed_len = 0;
    const char *eof_token = MDBP_EOF_TOKEN;
    int want_derivation = 1;
    int quiet = 0;
    int stats = 0;
    int verdict_exit = 0;
    int i;
    size_t k;
    char *raw = NULL;
    size_t raw_len = 0;
    token_vec tokens = { NULL, 0, 0 };
    mdbp_options opts;
    mdbp_parser *parser = NULL;
    mdbp_result res;
    mdbp_status st = MDBP_OK;
    const mdbp_token *rec_tokens = NULL;   /* the record's token stream */
    size_t rec_ntokens = 0;
    verdict v;
    struct timespec t0;
    struct timespec t1;
    int rc = EXIT_INTERNAL;   /* slurp() sets it on every failure path */
    int parsed = 0;           /* mdbp_parse_sql() already ran (--valid live) */
    int status = EXIT_SUCCESS_OK;
    long wall_us = 0;
    char mode_found[16];
    char charset_found[64];
    int parsing_place_given = 0;   /* --parsing-place was named on the line */
    int valid_given = 0;           /* --valid was named on the line */
    int lex_only = 0;              /* --lex-only: mdbp_lex_sql(), no parse */
    const char *version_id_text = NULL;   /* --version-id N, as given */
    unsigned long version_id = 0;         /* ... and as a number */
    int max_tokens_given = 0;      /* --max-tokens: mdbp_lex_sql_prefix() */
    unsigned long max_tokens = 0;
    int more = 0;                  /* mdbp_lex_sql_prefix()'s *more output */

    memset(&cfg, 0, sizeof cfg);
    cfg.sql_mode = "";
    cfg.charset = "utf8mb4";
    /* PLAN.md S1.8 / tools/vendor/thd.md S3: --prepare-mode defaults ON. */
    cfg.prepare_mode = 1;
    /* PLAN.md S1.8: the parser-coupled answer is the RIGHT one, so it is the
     * default for --sql, --file and --jsonl's "sql" form.  The static modes
     * are opt-in and exist for the fixtures that pin one lexer branch. */
    cfg.valid = VALID_LIVE;
    cfg.valid_symbols = NULL;
    /* Task P1.1: NO_MATTER unless a static-mode caller declares otherwise. */
    cfg.parsing_place = (int) NO_MATTER;

    if (argc < 2) {
        fputs("mdbparse: no arguments; try --help\n", stderr);
        return EXIT_USAGE;
    }

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        for (k = 0; k < sizeof LATER_PHASES / sizeof LATER_PHASES[0]; k++) {
            size_t flag_len = strlen(LATER_PHASES[k].flag);
            if (strcmp(arg, LATER_PHASES[k].flag) == 0
                || (strncmp(arg, LATER_PHASES[k].flag, flag_len) == 0
                    && arg[flag_len] == '=')) {
                fprintf(stderr,
                        "mdbparse: %s is not implemented yet; %s owns it\n",
                        LATER_PHASES[k].flag, LATER_PHASES[k].owner);
                return EXIT_UNSUPPORTED;
            }
        }
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(stdout);
            return EXIT_SUCCESS_OK;
        } else if (strcmp(arg, "--tokens") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --tokens needs a value\n", stderr);
                return EXIT_USAGE;
            }
            tokens_path = argv[i];
        } else if (strcmp(arg, "--tokens-json") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --tokens-json needs a value\n", stderr);
                return EXIT_USAGE;
            }
            tokens_json_path = argv[i];
        } else if (strcmp(arg, "--jsonl") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --jsonl needs a value\n", stderr);
                return EXIT_USAGE;
            }
            jsonl_path = argv[i];
        } else if (strcmp(arg, "--sql") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --sql needs a value\n", stderr);
                return EXIT_USAGE;
            }
            sql_text = argv[i];
        } else if (strcmp(arg, "--file") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --file needs a value\n", stderr);
                return EXIT_USAGE;
            }
            sql_file = argv[i];
        } else if (strcmp(arg, "--dump-tokens") == 0) {
            dump_tokens = 1;
        } else if (strcmp(arg, "--dump-state-maps") == 0) {
            dump_state_maps = 1;
        } else if (strcmp(arg, "--lex-only") == 0) {
            lex_only = 1;
        } else if (strcmp(arg, "--valid") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --valid needs a value\n", stderr);
                return EXIT_USAGE;
            }
            valid_given = 1;
            if (strcmp(argv[i], "none") == 0) {
                cfg.valid = VALID_NONE;
            } else if (strcmp(argv[i], "all") == 0) {
                cfg.valid = VALID_ALL;
            } else if (strcmp(argv[i], "list") == 0) {
                cfg.valid = VALID_LIST;
            } else if (strcmp(argv[i], "live") == 0) {
                cfg.valid = VALID_LIVE;
            } else {
                fprintf(stderr,
                        "mdbparse: --valid must be none, all, live or list, "
                        "not '%s'\n", argv[i]);
                return EXIT_USAGE;
            }
        } else if (strcmp(arg, "--valid-symbols") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --valid-symbols needs a value\n", stderr);
                return EXIT_USAGE;
            }
            cfg.valid_symbols = argv[i];
        } else if (strcmp(arg, "--parsing-place") == 0) {
            int place;

            if (++i >= argc) {
                fputs("mdbparse: --parsing-place needs a value\n", stderr);
                return EXIT_USAGE;
            }
            place = mdbp_parsing_place_by_name(argv[i]);
            if (place < 0) {
                fprintf(stderr,
                        "mdbparse: --parsing-place: '%s' is not an "
                        "enum_parsing_place value (sql/sql_priv.h; "
                        "src/stub.h carries the list)\n", argv[i]);
                return EXIT_USAGE;
            }
            cfg.parsing_place = place;
            parsing_place_given = 1;
        } else if (strcmp(arg, "--sql-mode") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --sql-mode needs a value\n", stderr);
                return EXIT_USAGE;
            }
            sql_mode = argv[i];
        } else if (strcmp(arg, "--charset") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --charset needs a value\n", stderr);
                return EXIT_USAGE;
            }
            charset = argv[i];
        } else if (strcmp(arg, "--version-id") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --version-id needs a value\n", stderr);
                return EXIT_USAGE;
            }
            if (parse_decimal(argv[i], &version_id) != 0) {
                fprintf(stderr,
                        "mdbparse: --version-id: '%s' is not a decimal "
                        "integer (digits only)\n", argv[i]);
                return EXIT_USAGE;
            }
            version_id_text = argv[i];
        } else if (strcmp(arg, "--max-tokens") == 0) {
            if (++i >= argc) {
                fputs("mdbparse: --max-tokens needs a value\n", stderr);
                return EXIT_USAGE;
            }
            if (parse_decimal(argv[i], &max_tokens) != 0) {
                fprintf(stderr,
                        "mdbparse: --max-tokens: '%s' is not a decimal "
                        "integer (digits only)\n", argv[i]);
                return EXIT_USAGE;
            }
            max_tokens_given = 1;
        } else if (strcmp(arg, "--json") == 0) {
            quiet = 0;
        } else if (strcmp(arg, "--quiet") == 0) {
            quiet = 1;
        } else if (strcmp(arg, "--stats") == 0) {
            stats = 1;
        } else if (strcmp(arg, "--derivation") == 0) {
            want_derivation = 1;
        } else if (strcmp(arg, "--no-derivation") == 0) {
            want_derivation = 0;
        } else if (strcmp(arg, "--prepare-mode") == 0) {
            cfg.prepare_mode = 1;
        } else if (strcmp(arg, "--no-prepare-mode") == 0) {
            cfg.prepare_mode = 0;
        } else if (strcmp(arg, "--verdict-exit") == 0) {
            verdict_exit = 1;
        } else {
            fprintf(stderr, "mdbparse: unknown argument '%s'; try --help\n", arg);
            return EXIT_USAGE;
        }
    }

    cfg.sql_mode = sql_mode;
    cfg.charset = charset;
    if (dump_state_maps) {
        /* A diagnostic: it needs no input and answers before any of the
         * mutual-exclusion rules below could refuse one. */
        print_state_maps();
        return EXIT_SUCCESS_OK;
    }
    if (mdbp_sql_mode_refused(sql_mode, mode_found, sizeof mode_found)) {
        fprintf(stderr,
                "mdbparse: --sql-mode %s is unsupported: this is the MariaDB "
                "grammar variant only (PLAN.md S2.4)\n", mode_found);
        return EXIT_UNSUPPORTED;
    }
    if (mdbp_charset_refused(charset, charset_found, sizeof charset_found)) {
        fprintf(stderr,
                "mdbparse: --charset %s is unsupported: only utf8mb4 (and its "
                "alias utf8mb4_general_ci) is accepted (PLAN.md D-8)\n",
                charset_found);
        return EXIT_UNSUPPORTED;
    }
    if (sql_text != NULL && sql_file != NULL) {
        fputs("mdbparse: --sql and --file are mutually exclusive\n", stderr);
        return EXIT_USAGE;
    }
    if ((sql_text != NULL || sql_file != NULL)
        && (tokens_path != NULL || tokens_json_path != NULL
            || jsonl_path != NULL)) {
        fputs("mdbparse: --sql and --file are mutually exclusive with "
              "--tokens, --tokens-json and --jsonl\n", stderr);
        return EXIT_USAGE;
    }
    if (tokens_path != NULL && tokens_json_path != NULL) {
        fputs("mdbparse: --tokens and --tokens-json are mutually exclusive\n",
              stderr);
        return EXIT_USAGE;
    }
    if (jsonl_path != NULL && (tokens_path != NULL || tokens_json_path != NULL)) {
        fputs("mdbparse: --jsonl is mutually exclusive with --tokens and "
              "--tokens-json\n", stderr);
        return EXIT_USAGE;
    }
    /* --lex-only (plan `tokeniser-entry-points.md` P2).  It lexes through
     * mdbp_lex_sql(), which has exactly one context -- no parser -- so every
     * flag that names a different context, or input that is not SQL, is
     * refused rather than ignored: an option that silently lost would look
     * honoured.  An explicit `--valid none` names the context it already has,
     * and is accepted. */
    if (lex_only && (tokens_path != NULL || tokens_json_path != NULL)) {
        fputs("mdbparse: --lex-only lexes SQL; it is mutually exclusive with "
              "--tokens and --tokens-json\n", stderr);
        return EXIT_USAGE;
    }
    if (lex_only && valid_given && cfg.valid != VALID_NONE) {
        fputs("mdbparse: --lex-only lexes with no parser, which is --valid "
              "none; any other --valid names a context it does not have\n",
              stderr);
        return EXIT_USAGE;
    }
    if (lex_only && parsing_place_given) {
        fputs("mdbparse: --lex-only lexes with no parser, so parsing_place is "
              "NO_MATTER throughout; --parsing-place cannot be honoured\n",
              stderr);
        return EXIT_USAGE;
    }
    /* --max-tokens (task P1.1, plan `lex-sql-prefix.md` D-9): meaningless
     * without --lex-only, which is the one call it changes -- refused rather
     * than silently ignored, the same rule every --lex-only-only flag above
     * follows. */
    if (max_tokens_given && !lex_only) {
        fputs("mdbparse: --max-tokens is only valid with --lex-only\n", stderr);
        return EXIT_USAGE;
    }
    if (parsing_place_given && cfg.valid == VALID_LIVE) {
        /* Task P1.1.  In `--valid live` the PARSER writes parsing_place -- the
         * lifted sql_yacc.yy actions do, into the very SELECT_LEX the lexer
         * reads -- so a declared value would be overwritten by the first
         * action that runs and the flag would look honoured while doing
         * nothing.  Refused rather than ignored, which is the same rule
         * --sql-mode ORACLE follows. */
        fputs("mdbparse: --parsing-place is for the static --valid modes "
              "(none, all, list): in --valid live the parser's own lifted "
              "actions write parsing_place, so a declared value would be "
              "silently overwritten\n", stderr);
        return EXIT_USAGE;
    }
    if (jsonl_path != NULL && verdict_exit) {
        fputs("mdbparse: --jsonl and --verdict-exit are mutually exclusive: a "
              "per-record verdict cannot be a process exit code (a batch run "
              "exits 0 whatever its records say)\n", stderr);
        return EXIT_USAGE;
    }
    /* The last USAGE check, and deliberately still above the first
     * allocation: a run with no input named must answer "try --help", not
     * whatever a starved allocator has to say. */
    if (jsonl_path == NULL && tokens_path == NULL && tokens_json_path == NULL
        && sql_text == NULL && sql_file == NULL) {
        fputs("mdbparse: one of --sql, --file, --tokens, --tokens-json or "
              "--jsonl is required; try --help\n", stderr);
        return EXIT_USAGE;
    }

    /* ONE parser for this process, and the ONLY way this CLI reaches the
     * grammar: every parse below goes through mdbp_parse_tokens() or
     * mdbp_parse_sql() (include/mdbparse.h).  The refused sql_modes and charsets
     * were already reported above, off the same rules mdbp_parser_new()
     * applies, so a NULL here is an allocation failure and nothing else. */
    mdbp_options_init(&opts);
    opts.sql_mode = cfg.sql_mode;
    opts.charset = cfg.charset;
    opts.prepare_mode = cfg.prepare_mode;
    opts.want_derivation = want_derivation;
    parser = mdbp_parser_new(&opts);
    if (parser == NULL) {
        /* Batch mode names the mode and the file in EVERY diagnostic it emits
         * (tests/test_mdbparse_batch.py pins that convention against a
         * memory-starved run); a bare message here would be its one
         * exception. */
        if (jsonl_path != NULL) {
            fprintf(stderr, "mdbparse: --jsonl: %s: out of memory\n",
                    jsonl_path);
        } else {
            fputs("mdbparse: out of memory creating the parser\n", stderr);
        }
        return EXIT_INTERNAL;
    }
    /* --version-id (plan `tokeniser-entry-points.md` P3, D-13): the library
     * decides whether N is a release id, and it answers through the same
     * call any caller makes.  A refusal is unsupported input, exit 3, like a
     * refused --sql-mode or --charset; it is reported before any input is
     * read.  The static --valid modes then take the id the PARSER holds, so
     * every lexing path in this process lexes against one id, and without
     * --version-id that id is the library's own default. */
    if (version_id_text != NULL) {
        mdbp_status vst = mdbp_parser_set_version_id(parser, version_id);

        if (vst != MDBP_OK) {
            fprintf(stderr,
                    "mdbparse: --version-id %s is unsupported: a release id is "
                    "major*10000 + minor*100 + patch, from %lu to %lu\n",
                    version_id_text, MDBP_LEX_VERSION_ID_MIN,
                    MDBP_LEX_VERSION_ID_MAX);
            mdbp_parser_free(parser);
            return vst == MDBP_UNSUPPORTED ? EXIT_UNSUPPORTED : EXIT_INTERNAL;
        }
    }
    cfg.version_id = mdbp_parser_version_id(parser);
    cfg.max_tokens_given = max_tokens_given;
    cfg.max_tokens = max_tokens;

    if (jsonl_path != NULL) {
        status = run_batch(jsonl_path, parser, want_derivation, quiet, stats,
                           &cfg, dump_tokens, lex_only);
        mdbp_parser_free(parser);
        return status;
    }
    if (sql_text != NULL || sql_file != NULL) {
        if (sql_file != NULL) {
            raw = slurp(sql_file, &raw_len, &rc);
            if (raw == NULL) {
                if (rc == EXIT_INTERNAL) {
                    fputs("mdbparse: out of memory reading the input\n", stderr);
                }
                mdbp_parser_free(parser);
                return rc;
            }
            lexed = raw;
            lexed_len = raw_len;
        } else {
            lexed = sql_text;
            lexed_len = strlen(sql_text);
        }
        /* PLAN.md D-9: the connection charset is utf8mb4, so undecodable
         * bytes are exit 2 through either channel -- what lexdump.py does.
         * The library refuses the same bytes (as unsupported input) off this
         * same predicate; the CLI asks first only so it can report them as the
         * usage error they are here. */
        if (!mdbp_input_is_utf8(lexed, lexed_len)) {
            fputs("mdbparse: the input is not valid UTF-8 (the connection "
                  "charset is utf8mb4)\n", stderr);
            free(raw);
            mdbp_parser_free(parser);
            return EXIT_USAGE;
        }
        if (lex_only) {
            /* --lex-only: one mdbp_lex_sql() call and no parse at all, or
             * mdbp_lex_sql_prefix() with --max-tokens (task P1.1, plan
             * `lex-sql-prefix.md` D-9). */
            if (cfg.max_tokens_given) {
                st = mdbp_lex_sql_prefix(parser, lexed, lexed_len,
                                         (size_t) cfg.max_tokens, &res, &more);
            } else {
                st = mdbp_lex_sql(parser, lexed, lexed_len, &res);
            }
            if (st == MDBP_INTERNAL && res.tokens == NULL) {
                fputs("mdbparse: out of memory lexing the input\n", stderr);
                free(raw);
                mdbp_parser_free(parser);
                return EXIT_INTERNAL;
            }
            if (dump_tokens) {
                print_token_lines(res.tokens, res.ntokens, lexed, lexed_len);
            } else if (!quiet) {
                print_lex_object(0, NULL, st, &res, cfg.max_tokens_given, more);
            }
            if (stats) {
                fprintf(stderr, "mdbparse: status=%s tokens=%zu\n",
                        lex_status_name(st), res.ntokens);
            }
            free(raw);
            mdbp_parser_free(parser);
            return st == MDBP_OK ? EXIT_SUCCESS_OK
                 : st == MDBP_UNSUPPORTED ? EXIT_UNSUPPORTED : EXIT_INTERNAL;
        }
        if (cfg.valid == VALID_LIVE) {
            /* The parse IS the lexing here: mdbp_parse_sql() has the parser
             * pull each token as the lexer makes it, so the token stream and
             * the verdict come from ONE run.  `parsed` records that, so the
             * verdict below must not run a second parse over the same tokens
             * -- it would answer the same, and running it would make "one
             * run" false. */
            clock_gettime(CLOCK_MONOTONIC, &t0);
            st = mdbp_parse_sql(parser, lexed, lexed_len, &res);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            wall_us = (long) ((t1.tv_sec - t0.tv_sec) * 1000000L
                              + (t1.tv_nsec - t0.tv_nsec) / 1000L);
            parsed = 1;
            if (st == MDBP_INTERNAL && res.tokens == NULL) {
                /* include/mdbparse.h: a NULL token stream on MDBP_INTERNAL means
                 * the stream never got built.  A failure IN the parse keeps
                 * its tokens and becomes the "internal" verdict below. */
                fputs("mdbparse: out of memory lexing the input\n", stderr);
                free(raw);
                mdbp_parser_free(parser);
                return EXIT_INTERNAL;
            }
            rec_tokens = res.tokens;
            rec_ntokens = res.ntokens;
            eof_token = res.eof_token;
        } else {
            rc = lex_record(lexed, lexed_len, &cfg, &tokens, &eof_token);
            if (rc != 0) {
                fputs("mdbparse: out of memory lexing the input\n", stderr);
                free(raw);
                vec_free(&tokens);
                mdbp_parser_free(parser);
                return rc;
            }
            rec_tokens = tokens.items;
            rec_ntokens = tokens.n;
        }
        if (dump_tokens) {
            /* Single-record --dump-tokens: the token lines are the WHOLE of
             * stdout, so `diff <(lexdump.py --sql X) <(mdbparse --sql X
             * --dump-tokens)` is empty.  No verdict -- and under `--valid
             * live` the parse that answered valid(T) has already happened,
             * because the token stream could not have been produced without
             * it; its verdict is simply not printed. */
            print_token_lines(rec_tokens, rec_ntokens, lexed, lexed_len);
            free(raw);
            vec_free(&tokens);
            mdbp_parser_free(parser);
            return EXIT_SUCCESS_OK;
        }
    } else {
        raw = slurp(tokens_path != NULL ? tokens_path : tokens_json_path,
                    &raw_len, &rc);
        if (raw == NULL) {
            if (rc == EXIT_INTERNAL) {
                fputs("mdbparse: out of memory reading the input\n", stderr);
            }
            mdbp_parser_free(parser);
            return rc;
        }
        rc = tokens_path != NULL
            ? read_tokens_text(raw, &tokens)
            : read_tokens_json(raw, raw_len, &tokens);
        if (rc != 0) {
            free(raw);
            vec_free(&tokens);
            mdbp_parser_free(parser);
            return rc;
        }
        if (dump_tokens) {
            print_token_lines(tokens.items, tokens.n, NULL, 0);
            free(raw);
            vec_free(&tokens);
            mdbp_parser_free(parser);
            return EXIT_SUCCESS_OK;
        }
        rec_tokens = tokens.items;
        rec_ntokens = tokens.n;
    }

    /* The ONE parse of the tokens-in path.  Under `--valid live` it already
     * happened above, in the same run that produced the tokens. */
    if (!parsed) {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        st = mdbp_parse_tokens(parser, tokens.items, tokens.n, &res);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        wall_us = (long) ((t1.tv_sec - t0.tv_sec) * 1000000L
                          + (t1.tv_nsec - t0.tv_nsec) / 1000L);
    }

    memset(&v, 0, sizeof v);
    v.status = "ok";
    v.error_state = -1;
    v.byte_pos = -1;
    v.eof_token = eof_token;

    /* The library decides "unsupported input" -- a token name this grammar
     * has no terminal for -- and it decides it for the WHOLE stream, at the
     * same index and with the same meaning on both paths: on the tokens-in
     * path before the parse, and under `--valid live` after it, where the
     * parse stopped at the offending token because there was nothing to hand
     * the automaton.  Exit 3, exactly as replay.py reports it.  (Batch mode
     * diverges on the exit code, deliberately; see the top of this file.) */
    if (st == MDBP_UNSUPPORTED) {
        v.accepted = 0;
        v.status = "unsupported";
        v.have_derivation = 0;
        v.have_error = 1;
        v.at_index = res.error_token_index;
        v.error_token = res.error_token;
        v.byte_pos = res.error_byte_pos;
        if (!quiet) {
            print_verdict(&v);
        }
        if (stats) {
            fprintf(stderr,
                    "mdbparse: status=unsupported accepted=false tokens=%zu "
                    "at_index=%zu token=%s\n",
                    rec_ntokens, res.error_token_index, res.error_token);
        }
        free(raw);
        vec_free(&tokens);
        mdbp_parser_free(parser);
        return EXIT_UNSUPPORTED;
    }

    v.wall_us = wall_us;
    v.tokens_consumed = res.tokens_consumed;
    v.have_derivation = want_derivation;
    v.derivation = res.derivation;
    v.nderivation = res.derivation_len;

    if (st == MDBP_INTERNAL) {
        v.accepted = 0;
        v.status = "internal";
        v.have_derivation = 0;
        status = EXIT_INTERNAL;
    } else if (res.accepted) {
        v.accepted = 1;
        status = EXIT_SUCCESS_OK;
    } else {
        /* A rejection is a measurement, not a failure: exit 0 unless
         * --verdict-exit asked otherwise. */
        v.accepted = 0;
        v.have_error = 1;
        v.at_index = res.error_token_index;
        v.error_token = res.error_token;
        v.error_state = res.error_state;
        v.byte_pos = res.error_byte_pos;
        v.expected = res.expected;
        v.nexpected = res.nexpected;
        v.have_expected = res.expected != NULL;
        status = verdict_exit ? EXIT_REJECTED : EXIT_SUCCESS_OK;
    }

    if (!quiet) {
        print_verdict(&v);
    }
    if (stats) {
        fprintf(stderr,
                "mdbparse: status=%s accepted=%s tokens=%zu consumed=%zu "
                "reductions=%zu wall_us=%ld\n",
                v.status, v.accepted ? "true" : "false", rec_ntokens,
                v.tokens_consumed, res.derivation_len, wall_us);
    }

    mdbp_parser_free(parser);
    free(raw);
    vec_free(&tokens);
    return status;
}
