/* include/mdbparse.h -- the public surface of this library, and the only header a
 * caller needs.  It includes <stddef.h> and nothing else; every other header
 * in src/ is internal and stays internal.
 *
 * This library is MariaDB's own LALR grammar, extracted from
 * sql/sql_yacc.yy BY SCRIPT, together with MariaDB's own lexer, sliced from
 * sql/sql_lex.cc by the same route.  Nothing here is a hand port, which is
 * the whole point: the grammar and the lexer are MariaDB's own text.
 * libs/mariadbparser/UPSTREAM.md records the pinned MariaDB commit, how
 * generated/ was produced from it, and how upstream changes are applied.
 *
 * ---------------------------------------------------------------------------
 * 1.  WHAT AN ACCEPTANCE MEANS: A SUPERSET, NOT SQL VALIDATION
 * ---------------------------------------------------------------------------
 * This recognises the GRAMMAR-ONLY language: everything MariaDB's LALR
 * automaton accepts BEFORE any C action gets a chance to object.  Every one
 * of the grammar's action bodies was replaced, mechanically, by a stub that
 * records the reduction, so all the semantic checking MariaDB does inside
 * those actions -- name resolution, option conflicts, clause combinations a
 * rule accepts but the server then refuses -- is simply not here.
 *
 * Therefore:  a string this library ACCEPTS is not guaranteed to be valid
 * SQL; a string it REJECTS is guaranteed to be invalid.  The accepted
 * language is a strict superset of what `mariadbd` accepts.  Use this to rule
 * statements OUT, never to certify them in.
 *
 * ---------------------------------------------------------------------------
 * 2.  THE MariaDB GRAMMAR VARIANT ONLY
 * ---------------------------------------------------------------------------
 * sql/sql_yacc.yy carries two grammars behind its own %ifdef preprocessor.
 * Only the MariaDB variant is extracted and only it is supported:
 * MDBP_VARIANT_MARIADB is the sole member of mdbp_variant, and
 * mdbp_parser_new() answers NULL for any other value rather than quietly
 * parsing MariaDB rules while the caller believes it asked for something
 * else.  For the same reason an sql_mode naming ORACLE or MSSQL is REFUSED
 * (mdbp_parser_new() answers NULL): those modes change the language, this
 * library cannot honour them, and a mode that cannot be honoured must not
 * look honoured.  Every other sql_mode element is one of the five this
 * lexer implements directly (ANSI_QUOTES, PIPES_AS_CONCAT,
 * HIGH_NOT_PRECEDENCE, IGNORE_SPACE, NO_BACKSLASH_ESCAPES), one of the six
 * composite names ANSI, POSTGRESQL, DB2, MAXDB, MYSQL40 and MYSQL323 --
 * expanded into some of those five bits the same way the server's own
 * expand_sql_mode() expands them (since 2.1.1) -- or a server mode with no
 * lexical effect (TRADITIONAL included), which is accepted and inert.
 *
 * ---------------------------------------------------------------------------
 * 3.  THREAD SAFETY: ONE PARSER PER THREAD, AND THEY ARE INDEPENDENT
 * ---------------------------------------------------------------------------
 * Say the whole of it, because both halves matter and the tempting
 * half-truths sit on either side.
 *
 * WHAT YOU MAY DO.  Two threads may each hold their OWN mdbp_parser and call
 * mdbp_parse_sql()/mdbp_parse_tokens() on them AT THE SAME TIME, with no
 * lock of yours between them.  Distinct parser objects are independent: no
 * verdict, derivation, token stream or error record of one is affected by
 * what another is doing.  unittest/mariadbparser/parser_t.cc
 * (parsers_on_many_threads_agree) checks this by comparing every threaded
 * result against the single-threaded answer for the same statement.
 *
 * WHAT YOU MAY NOT DO.  ONE mdbp_parser MUST NEVER BE USED BY TWO THREADS AT
 * THE SAME TIME.  A parser owns the buffers every result points into (see
 * mdbp_result below: the caller frees nothing, and a result stays valid only
 * until the next call on the SAME parser), so a concurrent second call
 * rewrites what the first call's reader is still reading.  That is not a
 * race this library can absorb; it is the caller giving one object to two
 * users.
 *
 * MOVING A PARSER BETWEEN THREADS IS ALLOWED, and this is the property a
 * thread-pool consumer needs: a parser may be created on one thread and used
 * on another, and afterwards on a third, provided (a) the handoff
 * establishes a happens-before edge -- a mutex, a channel or queue with its
 * own lock, a thread join, any normal synchronisation -- and (b) no two
 * threads use it concurrently, as above.  Nothing that outlives a call is
 * kept in thread-local storage: every per-thread binding this library uses
 * is set from the parser object on entry to a parse and restored on exit, so
 * the durable state is all in the mdbp_parser you are holding and it travels
 * with the object.  An LSP server may therefore hand a parser to whichever
 * worker is free, exactly as MariaDB's own thread pool hands a THD to
 * whichever thread is free.
 *
 * This is deliberately NOT spelled "thread-safe", a phrase readers hear as
 * permitting concurrent use of one object, which is the one thing above that
 * is forbidden.
 *
 * WHAT IS STILL SHARED.  Two tables are process-wide: the cache of the
 * character tokens' names, and MariaDB's own keyword tables, whose per-entry
 * lengths this library fills in the way the server's own lex_init() does.
 * Both are WRITTEN ONCE AND READ-ONLY THEREAFTER.  The first call that needs
 * one may initialise it lazily, but does so EXACTLY ONCE however many
 * threads arrive together: each initialisation sits behind a once-guard
 * (pthread_once() / InitOnceExecuteOnce()), so every thread that proceeds sees the
 * finished table rather than a half-written one.  There is no init entry
 * point for you to call and none is needed; correct first use is the
 * library's problem, not yours.
 *
 * ONE PROCESS-WIDE RULE REMAINS, and it is the ordinary one: an mdbp_parser
 * must be freed by mdbp_parser_free() while no thread is using it, and after
 * that no result it produced may be read.
 *
 * ---------------------------------------------------------------------------
 * 4.  NO TREE IN v1 -- AND WHY THE DERIVATION IS PUBLIC INSTEAD
 * ---------------------------------------------------------------------------
 * v1 returns no parse tree.  It returns the DERIVATION: bison rule numbers in
 * reduce order, exactly as the automaton reduced them.  That is not a
 * consolation prize -- a derivation plus the grammar's rule table (bison's
 * own, built from generated/yy_stub.yy) reconstructs the parse tree, so
 * nothing is lost, and the shape of a future tree API is not prejudged by
 * this one.  A tree can therefore be ADDED later without breaking anything
 * declared in this header.
 */
#ifndef MDBPARSE_H
#define MDBPARSE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Semantic version of this SURFACE.  mdbp_version_string() returns the same
 * three numbers as "MAJOR.MINOR.PATCH", built from the macros below, so a
 * caller can check the header it compiled against against the library it
 * linked.  The pkg-config Version: and the shared object's soname are
 * derived from these same three macros and from nothing else.
 *
 * VERSION 2 IS AN ABI BREAK, and there are two of them in one:
 *
 *   - mdbp_options GREW A FIELD (`charset`, appended -- never inserted).
 *     A caller that hand-transcribes this struct, or that
 *     passes a v1-sized object, hands mdbp_parser_new() something shorter
 *     than it reads.
 *   - THE THREAD-SAFETY CONTRACT CHANGED (section 3 above): distinct parser
 *     objects on distinct threads are now independent and need no external
 *     lock.
 *
 * So the soname moved with the major: this library is libmariadbparser.so.2
 * (its real name carries all three numbers below).  A consumer that loads it
 * by explicit path therefore fails DETERMINISTICALLY at load time instead of
 * reading past the end of a struct, and a consumer compiled against this
 * header tests
 *
 *     #if MDBP_VERSION_MAJOR >= 2
 *
 * for the new contract rather than parsing prose.  Version 2.0 added no
 * function and removed none.
 *
 * VERSION 2.1 ADDED THREE FUNCTIONS, mdbp_lex_sql(),
 * mdbp_parser_set_version_id() and mdbp_parser_version_id(), and changed
 * nothing that already existed: no struct grew, no function changed its
 * meaning, and a new parser lexes exactly as a 2.0 one did, so the soname
 * stays libmariadbparser.so.2.  The three new functions carry the symbol
 * version MDBPARSER_2.1 and everything older keeps MDBPARSER_2.0.  A caller
 * that needs them tests
 *
 *     #if MDBP_VERSION_MAJOR == 2 && MDBP_VERSION_MINOR >= 1
 *
 * at compile time, and one that loads the library at run time looks each
 * symbol up rather than assuming it, because a 2.0 library does not have
 * them.
 *
 * VERSION 2.1.1 CHANGED two behaviours and added no function, so no symbol
 * version node was added and the soname stays libmariadbparser.so.2: the six
 * composite sql_mode names (ANSI, POSTGRESQL, DB2, MAXDB, MYSQL40, MYSQL323)
 * are now expanded into the lexer flags they carry, the same way the
 * server's own expand_sql_mode() expands them, instead of being accepted and
 * left inert; and the ABORT_SYM token produced after an unclosed plain
 * comment (opened with a slash-star, never closed) now ends at the end of
 * the input, instead of a truncated span left over from a stale union read.
 * Nothing else changed.
 *
 * VERSION 2.2 ADDED ONE FUNCTION, mdbp_lex_sql_prefix(), and changed nothing
 * that already existed: no struct grew, no function changed its meaning, and
 * a new parser lexes exactly as a 2.1.1 one did, so the soname stays
 * libmariadbparser.so.2.  The new function carries the symbol version
 * MDBPARSER_2.2 and everything older keeps its existing version.  A caller
 * that needs it tests
 *
 *     #if MDBP_VERSION_MAJOR == 2 && MDBP_VERSION_MINOR >= 2
 *
 * at compile time, and one that loads the library at run time looks the
 * symbol up rather than assuming it, because a 2.1 library does not have
 * it. */
#define MDBP_VERSION_MAJOR 2
#define MDBP_VERSION_MINOR 2
#define MDBP_VERSION_PATCH 0

const char *mdbp_version_string(void);

/* The parser object: opaque, and the owner of everything a result points at.
 * Create one, parse as many records through it as you like, free it. */
typedef struct mdbp_parser mdbp_parser;

/* The grammar variant.  Section 2 above: MariaDB only, on purpose. */
typedef enum {
    MDBP_VARIANT_MARIADB = 0
} mdbp_variant;

/* The status of a CALL, which is not the verdict on the input.
 *
 *   MDBP_OK           the library did its job.  On the two parse calls,
 *                     mdbp_parse_tokens() and mdbp_parse_sql(), `accepted`
 *                     is the verdict, and A REJECTION IS A SUCCESS: MDBP_OK
 *                     with accepted == 0 and the error record filled in.
 *                     On mdbp_lex_sql() and mdbp_lex_sql_prefix() nothing is
 *                     parsed, so there is no verdict and `accepted` is
 *                     always 0.
 *   MDBP_UNSUPPORTED  input this library refuses (see mdbp_parse_tokens,
 *                     mdbp_parse_sql, mdbp_lex_sql, mdbp_lex_sql_prefix and
 *                     mdbp_parser_set_version_id).  On the two parse
 *                     calls a token name this grammar has no terminal for is
 *                     such a refusal; mdbp_lex_sql() and mdbp_lex_sql_prefix()
 *                     never refuse a token name.  No verdict.
 *   MDBP_INTERNAL     the library could not finish: an allocation failed, or
 *                     the automaton exhausted its own memory.  No verdict.
 */
typedef enum {
    MDBP_OK = 0,
    MDBP_UNSUPPORTED = 1,
    MDBP_INTERNAL = 2
} mdbp_status;

/* One token.
 *
 * `name` is the terminal's name in the grammar's own spelling: "SELECT_SYM",
 * "IDENT_QUOTED", "'('" -- character literals keep their quotes.
 *
 * `token_number` is IGNORED ON INPUT (mdbp_parse_tokens() resolves it from
 * `name`) and filled in on output.
 *
 * `byte_start`/`byte_end` are byte offsets into the statement; they are -1
 * when the input did not carry them.
 *
 * `text` IS NOT NUL-TERMINATED on the lexed path.  A token produced by
 * mdbp_parse_sql(), mdbp_lex_sql() or mdbp_lex_sql_prefix() points into the
 * library's own copy of the statement, so its length is `byte_end - byte_start`
 * and the byte after it is the next byte of the statement, not a NUL.  Never
 * call strlen() on it.  (A token you hand to mdbp_parse_tokens() is yours; if
 * you make it a C string it stays one, because the library passes your
 * pointer straight back.) */
typedef struct mdbp_token {
    const char *name;
    const char *text;
    int token_number;
    long byte_start;
    long byte_end;
} mdbp_token;

/* What a parser is built with.  Always initialise with mdbp_options_init()
 * and then overwrite the fields you care about: that is what keeps a caller
 * source-compatible when a later version adds a field.
 *
 *   sql_mode        comma-separated list, e.g. "ANSI_QUOTES,IGNORE_SPACE";
 *                   NULL means "".  Copied by mdbp_parser_new(), so it need
 *                   not outlive the call.  ORACLE and MSSQL are refused; the
 *                   six composite names are expanded as the server expands
 *                   them (since 2.1.1) -- see section 2 above.
 *   prepare_mode    1 (the default): '?' is the PARAM_MARKER terminal, as in
 *                   a prepared statement.  0: '?' stays a character token.
 *   variant         MDBP_VARIANT_MARIADB, the only accepted value.
 *   want_derivation 1 (the default): report the derivation.  0: do not
 *                   record it at all, which is cheaper and makes
 *                   mdbp_result.derivation NULL.
 *   charset         the connection charset the statement is lexed in, e.g.
 *                   "utf8mb4"; NULL means "utf8mb4", exactly as sql_mode ==
 *                   NULL means "".  Case-insensitive.  ONE value is accepted
 *                   today -- utf8mb4, and its alias utf8mb4_general_ci -- and
 *                   every other name is REFUSED (mdbp_parser_new() answers
 *                   NULL), including utf8, utf8mb3 and latin1: an alias
 *                   silently treated as utf8mb4 would lex differently while
 *                   looking honoured, which is the failure mode this refusal
 *                   exists to prevent.  Copied by mdbp_parser_new(), so it
 *                   need not outlive the call.
 *
 *                   A second charset, when this library ever
 *                   grows one, is added by GENERATING that charset's own
 *                   state_map, ident_map and ctype tables and SELECTING them
 *                   per parse -- never by converting the input into utf8mb4
 *                   first.  Three reasons a converter must never be reached
 *                   for: (1) MariaDB's own server does not convert client SQL
 *                   before lexing it -- it lexes each connection charset with
 *                   that charset's own tables -- so a converting
 *                   implementation would diverge from the server truth this
 *                   library exists to reproduce; (2) transcoded bytes would
 *                   always run utf8mb4's lexing rules, never the requested
 *                   charset's, while looking like that charset had been
 *                   honoured -- exactly the failure mode above, just
 *                   moved from the name to the bytes; (3) this library
 *                   extracts MariaDB's own tables by script so
 *                   that a charset's lexing behaviour is MariaDB's, never a
 *                   reimplementation -- a converter would be exactly the
 *                   hand-written logic the whole extraction exists to avoid.
 */
typedef struct mdbp_options {
    const char *sql_mode;
    int prepare_mode;
    mdbp_variant variant;
    int want_derivation;
    const char *charset;      /* since 2.0: appended, never inserted */
} mdbp_options;

/* Writes the defaults: sql_mode NULL, prepare_mode 1,
 * variant MDBP_VARIANT_MARIADB, want_derivation 1, charset "utf8mb4".  Does
 * nothing when `opts` is NULL. */
void mdbp_options_init(mdbp_options *opts);

/* The result of one parse.  A CALLER-ALLOCATED OUT-PARAMETER -- put an
 * mdbp_result on your stack and pass its address.
 *
 * EVERY POINTER INSIDE IT IS OWNED BY THE PARSER.  `derivation`, `tokens` and
 * `expected` are the parser's own buffers; `eof_token`, `error_token` and
 * `message` are static strings or point into those buffers.  They stay valid
 * until the next call that fills a result on the same parser
 * (mdbp_parse_tokens(), mdbp_parse_sql(), mdbp_lex_sql() or
 * mdbp_lex_sql_prefix()), or until
 * mdbp_parser_free().  THE CALLER FREES NOTHING.  That is what lets a batch
 * caller push hundreds of thousands of records through one parser without
 * allocating per record.  Copy anything you need to keep.
 *
 * (The one thing the parser does not own: on the mdbp_parse_tokens() path the
 * `name` and `text` pointers inside the returned tokens are the ones YOU
 * passed in -- the library resolves token numbers into its own array but does
 * not copy your strings.  Keep them alive while you read the result.)
 *
 * Field by field:
 *
 *   status              same value the call returned.
 *   accepted            1 iff the automaton accepted.  Meaningful only when
 *                       status == MDBP_OK.
 *   derivation          bison rule numbers in reduce order; NULL when the
 *                       parser was built with want_derivation == 0 or the
 *                       parse recorded none.  When status is MDBP_INTERNAL
 *                       this is the PREFIX recorded before the failure -- it
 *                       is a diagnostic, not a derivation of anything, and
 *                       must not be read as one.
 *   tokens/ntokens      the token stream the parse ran over, with
 *                       token_number filled in.  On the mdbp_parse_sql() path
 *                       this is the WHOLE lexed stream, including whatever
 *                       follows a rejection.
 *   tokens_consumed     how many tokens the automaton took before it stopped.
 *   eof_token           what ended the token stream: "$end" normally, or
 *                       "ABORT_SYM" when the lexer aborted on broken input.
 *                       Always "$end" on the mdbp_parse_tokens() path.  NULL
 *                       on an mdbp_lex_sql_prefix() result whose stream was
 *                       cut (`*more` 1): nothing ended it.
 *   error_seen          1 iff the error record below is meaningful: a
 *                       rejection (MDBP_OK, accepted == 0) or a refused token
 *                       (MDBP_UNSUPPORTED).  0 for MDBP_INTERNAL.
 *   error_token_index   index into `tokens` of the offending token; equal to
 *                       `ntokens` when the failure is at end of input.
 *   error_token         its name, or "$end" at end of input.
 *   error_state         the LR state on top of the automaton's stack when the
 *                       error was reported; -1 when unknown (which includes
 *                       every MDBP_UNSUPPORTED, where no automaton ran on it).
 *   error_byte_pos      byte offset of that token in the statement, or -1.
 *   expected/nexpected  EVERY TERMINAL THAT CAN STILL BE SHIFTED from the
 *                       parser stack as it stood when the error was reported
 *                       -- after any number of reductions, which is bison's
 *                       own yy_lac() under `%define parse.lac full`, the same
 *                       question `valid(T)` asks -- sorted by name (strcmp
 *                       order, so the array is bsearch-able).  It is NOT the
 *                       failing state's action row: at a state carrying a
 *                       default reduction the row is a strict subset of what
 *                       the grammar accepts next, and this field answers the
 *                       larger, true set.  A terminal being in it does not
 *                       promise the rest of the statement can be completed,
 *                       only that this one token can be shifted next.
 *
 *                       NULL AND EMPTY ARE DIFFERENT ANSWERS, and a caller
 *                       that conflates them will report "nothing applies"
 *                       where the honest answer is "unknown":
 *                         NULL            the set could not be computed.
 *                                         Three ways: no automaton ran on
 *                                         this record (every
 *                                         MDBP_UNSUPPORTED, where
 *                                         error_state is -1 too), the
 *                                         shiftability check itself ran out
 *                                         of memory (a SHORT set is never
 *                                         reported in its place), or an
 *                                         allocation failed.
 *                         nexpected == 0  computed, and nothing at all can be
 *                                         shifted here.  `expected` is
 *                                         non-NULL and reading zero entries
 *                                         from it is correct.
 *                       Both are parser-owned like every other pointer here.
 *   message             a short static description of why status is not
 *                       MDBP_OK.  NULL when status == MDBP_OK -- including
 *                       for a rejection, which is a success.
 */
typedef struct mdbp_result {
    mdbp_status status;
    int accepted;
    const int *derivation;
    size_t derivation_len;
    const mdbp_token *tokens;
    size_t ntokens;
    size_t tokens_consumed;
    const char *eof_token;
    int error_seen;
    size_t error_token_index;
    const char *error_token;
    int error_state;
    long error_byte_pos;
    const char *const *expected;
    size_t nexpected;
    const char *message;
} mdbp_result;

/* Creates a parser.  `opts` may be NULL, which means the defaults
 * mdbp_options_init() writes; it is not retained (the sql_mode and charset
 * strings are copied).
 *
 * Returns NULL for exactly four reasons, and the caller cannot tell them
 * apart because none of them is recoverable:
 *   - opts->variant is not MDBP_VARIANT_MARIADB (section 2 above);
 *   - opts->sql_mode names ORACLE or MSSQL, which this library refuses rather
 *     than appearing to honour (section 2 above);
 *   - opts->charset names anything but utf8mb4 or its alias
 *     utf8mb4_general_ci, refused for the same reason (see mdbp_options
 *     above);
 *   - an allocation failed. */
mdbp_parser *mdbp_parser_new(const mdbp_options *opts);

/* Frees the parser and everything any result it produced still points at.
 * Does nothing when `p` is NULL. */
void mdbp_parser_free(mdbp_parser *p);

/* Parses an already-tokenised statement: a vector of terminal NAMES in the
 * grammar's own spelling, normally ending in the terminal END_OF_INPUT.
 * `token_number` is ignored on input.  `tokens` may be NULL when
 * `ntokens` is 0.
 *
 * MDBP_UNSUPPORTED when a token's `name` is not a terminal of this grammar;
 * `error_token_index` is that token, `error_token` its name.  MDBP_INTERNAL
 * when an allocation failed or the automaton exhausted its memory.  Otherwise
 * MDBP_OK, with `accepted` carrying the verdict. */
mdbp_status mdbp_parse_tokens(mdbp_parser *p, const mdbp_token *tokens,
                              size_t ntokens, mdbp_result *out);

/* Lexes `sql` (`len` bytes; it need not be NUL-terminated, and a NUL byte
 * inside it is data) with MariaDB's own lexer and parses the result.  The
 * parser DRIVES the lexer, one token at a time, exactly as the server does,
 * so that the lexer's context-sensitive spellings are decided against the
 * automaton's live stack.  There is no knob that makes this call weaker.  The
 * two weaker calls are separate functions, mdbp_lex_sql() and
 * mdbp_lex_sql_prefix() below, and they exist for a caller that wants the
 * tokens of a whole document, or its first few, without a verdict, such as
 * an editor; they lex with no parser at all, so their spellings are the
 * no-parser ones, never a half-way mix.
 *
 * The statement must be valid UTF-8: the connection charset is utf8mb4, and
 * bytes that are not decodable are input this library REFUSES
 * (MDBP_UNSUPPORTED) rather than lexing as something else.  MDBP_UNSUPPORTED
 * is also the answer when the lexer produces a token name this grammar has no
 * terminal for (MariaDB's lexer can emit character tokens the grammar never
 * declares), at `error_token_index`, whatever the parse had already
 * concluded.
 *
 * MDBP_INTERNAL splits on one observable: if the token stream could not be
 * produced at all (an allocation failure inside the lexer), `tokens` is NULL
 * and `ntokens` is 0; if the stream was produced and the automaton itself
 * failed, `tokens`/`ntokens` still describe the whole stream.  A caller
 * streaming many records can use that to tell "this record has no verdict"
 * from "this process can no longer make progress". */
mdbp_status mdbp_parse_sql(mdbp_parser *p, const char *sql, size_t len,
                           mdbp_result *out);

/* Lexes `sql` (`len` bytes; it need not be NUL-terminated, and a NUL byte
 * inside it is data) with MariaDB's own lexer and DOES NOT PARSE IT.  Since
 * 2.1.  It is for a caller that wants the tokens of a whole document -- an
 * editor that colours a buffer holding any number of statements -- and has
 * no use for a verdict.  It does not split statements either: the stream
 * runs straight across every ';' token, and a caller that wants statements
 * splits on those tokens itself.
 *
 * THE CONTEXT IS "NO PARSER".  MariaDB's lexer spells a few tokens from the
 * state of the parser that drives it.  Here nothing drives it, so it lexes
 * in the one context a caller without a parser can honestly claim: every
 * "can the parser shift this?" question is answered no, and parsing_place --
 * the field MariaDB's grammar actions write and its lexer reads -- is
 * NO_MATTER from the first byte to the last, because no action ever runs.
 * This is exactly the stream `mdbparse --lex-only` prints.
 *
 * WHERE THE STREAM CAN DIFFER FROM mdbp_parse_sql()'s for the same bytes.
 * Two mechanisms, and no other:
 *   - the parsing_place sites.  Where a parser's actions have set
 *     parsing_place, MariaDB's lexer spells VALUES, VALUE and '(' from it:
 *     VALUES may become VALUE_SYM or skip its look-ahead, VALUE may become
 *     VALUES, and '(' may become LEFT_PAREN_LIKE, LEFT_PAREN_WITH or
 *     LEFT_PAREN_ALT.  With no parser the lexer takes its NO_MATTER branch
 *     every time: VALUE stays VALUE_SYM, '(' stays '(', and VALUES looks one
 *     token ahead, so VALUES LESS and VALUES IN become the single tokens
 *     VALUES_LESS_SYM and VALUES_IN_SYM.  PARTITION, SELECT and UNION only
 *     clear the field, so a difference they cause shows at a later token.
 *   - the one site that asks whether the parser can shift a token.  A '('
 *     spelled LEFT_PAREN_LIKE, LEFT_PAREN_WITH or LEFT_PAREN_ALT is kept only
 *     when the parser can shift it, and is otherwise a plain '('.  With no
 *     parser the answer is always no.
 *
 * IT NEVER REFUSES A TOKEN NAME.  A lexed token this grammar has no
 * terminal for (MariaDB's lexer can emit character tokens the grammar never
 * declares) gets token_number -1, and every later token that is a terminal
 * still gets its number: one stray character must not blank a whole
 * document.  The call still returns MDBP_OK with the whole stream.
 *
 * The statement must be valid UTF-8, exactly as for mdbp_parse_sql():
 * anything else is MDBP_UNSUPPORTED with `tokens` NULL.  MDBP_INTERNAL for a
 * NULL parser, for a NULL `out` (returned, and `out` is not touched), or for
 * an allocation failure (`tokens` NULL).
 *
 * The result, field by field:
 *   status              the returned value.
 *   accepted            0: nothing was parsed, so there is no verdict.
 *   derivation          NULL, and derivation_len 0.
 *   tokens/ntokens      the whole stream, END_OF_INPUT included, each with
 *                       token_number resolved, or -1 for a name that is not
 *                       a terminal (see above).
 *   tokens_consumed     0.
 *   eof_token           "$end", or "ABORT_SYM" when the lexer aborted on
 *                       broken input, exactly as on mdbp_parse_sql().
 *   error_seen          0; error_token_index 0, error_token NULL,
 *                       error_state -1, error_byte_pos -1.
 *   expected/nexpected  NULL and 0.
 *   message             NULL on MDBP_OK.
 * Ownership is the rule every result follows (mdbp_result above): the parser
 * owns it, and it stays valid until the next call that fills a result on the
 * same parser. */
mdbp_status mdbp_lex_sql(mdbp_parser *p, const char *sql, size_t len,
                         mdbp_result *out);

/* At most `max_tokens` counted tokens of what mdbp_lex_sql() returns for the
 * same parser and bytes, or all of them; it does not parse either.  Since
 * 2.2.
 *
 * THE RULE.  Let `L` be mdbp_lex_sql(p, sql, len)'s tokens for the same
 * parser `p` and bytes, and `C` be `L` without a final END_OF_INPUT -- the
 * COUNTED tokens: every token but END_OF_INPUT counts, and ABORT_SYM counts,
 * because it carries text and a span an editor colours; END_OF_INPUT
 * is added on top only when the stream really ends within the limit.  If `C`
 * has `max_tokens` elements or fewer, this returns all of `L` (its
 * END_OF_INPUT included, when it has one) and `*more` is 0.  Otherwise it
 * returns the first `max_tokens` of `C` and `*more` is 1.  Names, text,
 * spans and token numbers are identical to `L`'s, element by element.  So
 * `max_tokens == 0` returns no counted token (and END_OF_INPUT alone when
 * `C` is empty).  Example, `SELECT 1; SELECT 2`: `max_tokens` 2 returns
 * SELECT_SYM NUM and `*more` 1; `max_tokens` 5 returns the whole stream,
 * END_OF_INPUT included, and `*more` 0.
 *
 * THE CONTEXT IS mdbp_lex_sql()'s: no parser, the parser's version id and
 * sql_mode, and the same "-1 for a name that is not a terminal" rule --
 * because the tokens are the same tokens (the rule above).
 *
 * THE WHOLE INPUT IS UTF-8-CHECKED AND COPIED; ONLY THE LEXING STOPS EARLY.
 * A document with an invalid byte anywhere is MDBP_UNSUPPORTED with
 * `tokens` NULL, even when the first `max_tokens` tokens are valid: the two
 * calls agree on every input.  The lexer itself stops after the first
 * counted token beyond the limit (one token of look-ahead, needed to answer
 * `*more`), which is where the saving is.
 *
 * `*more` (when `more` is not NULL): 1 or 0 by the rule above on
 * MDBP_OK, 0 on every other status -- written on every return, so a caller
 * that presets it to a sentinel can tell a call that never wrote it.  `more`
 * may be NULL; it is then not written.
 *
 * The result, field by field: as mdbp_lex_sql()'s above, except `tokens`/
 * `ntokens` are the prefix the rule above gives, and `eof_token` is NULL
 * exactly when `*more` is 1 -- nothing ended the stream.  `tokens` is
 * non-NULL on MDBP_OK, including when `ntokens` is 0.  Statuses: MDBP_OK;
 * MDBP_UNSUPPORTED for invalid UTF-8 anywhere in the `len` bytes (`tokens`
 * NULL); MDBP_INTERNAL for a NULL parser, a NULL `out` (returned, `out` not
 * touched), or an allocation failure (`tokens` NULL).  Ownership is the rule
 * every result follows (mdbp_result above), and that list now names this
 * function too. */
mdbp_status mdbp_lex_sql_prefix(mdbp_parser *p, const char *sql, size_t len,
                                size_t max_tokens, mdbp_result *out,
                                int *more);

/* THE RELEASE THAT VERSIONED COMMENTS ARE JUDGED AGAINST.  Since 2.1.
 *
 * MariaDB's lexer reads some comments as SQL.  A comment that opens with
 * '/' '*' '!' (an executable comment), or with '/' '*' 'M' '!' (MariaDB's
 * own form), and whose opening is followed by five or six digits, names a
 * release: the lexer reads its text as SQL, or skips it like any other
 * comment, depending on the release the lexer believes it is.  This sets
 * that release, for this parser only.
 *
 * IT SETS ONLY THAT COMMENT TEST.  The grammar is still the one extracted
 * from the pinned MariaDB tree, whatever id you give: this does not make the
 * library accept another release's SQL, it only decides which versioned
 * comments are read as SQL.
 *
 * The value is a RELEASE ID, the number the server itself compares:
 * major*10000 + minor*100 + patch.  110803 is 11.8.3, so a caller whose
 * users run MariaDB 11.8.3 calls mdbp_parser_set_version_id(p, 110803).
 *
 * What the id changes, for an id N (the lexer reads five digits, and a
 * sixth when one follows):
 *   - '/' '*' '!' followed by five or six digits is read as SQL when that
 *     number is <= N, except that a number from 50700 to 99999 (a MySQL 5.7
 *     or later comment) is never read, whatever N is;
 *   - '/' '*' 'M' '!' followed by five or six digits is read as SQL when that
 *     number is <= N, and the 50700 to 99999 exception does not apply to it;
 *   - the REVERSED form, '/' '*' '!' '!' followed by five or six digits, is
 *     read as SQL when N is < that number, and the exception does not apply
 *     to it either.
 * An executable comment with no digits after its opening is read as SQL
 * whatever the id is.
 *
 * The id applies to mdbp_parse_sql(), mdbp_lex_sql() and
 * mdbp_lex_sql_prefix().  It has no effect on mdbp_parse_tokens(), which
 * lexes nothing.  It is part of the parser
 * object, so it moves with the parser to another thread (section 3), and a
 * parser keeps it until it is set again.
 *
 * The accepted range is 10000 to 999999, the numbers the server's five- and
 * six-digit test can mean.  MDBP_OK: the id is stored.  MDBP_UNSUPPORTED: the
 * id is outside that range, and the parser keeps the id it had.
 * MDBP_INTERNAL: `p` is NULL. */
mdbp_status mdbp_parser_set_version_id(mdbp_parser *p, unsigned long version_id);

/* The parser's release id; see mdbp_parser_set_version_id() above.  On a new
 * parser it is the DEFAULT: the pinned MariaDB tree's release id, the release
 * whose grammar and lexer this library carries.  This header deliberately
 * does not write that number down -- ask a new parser for it, and it cannot
 * go stale.  0 for a NULL parser, which is never a release id. */
unsigned long mdbp_parser_version_id(const mdbp_parser *p);

#ifdef __cplusplus
}
#endif

#endif /* MDBPARSE_H */
