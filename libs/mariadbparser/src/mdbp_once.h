/* src/mdbp_once.h -- run a function exactly once per process, portably.
 *
 * The library's process-wide tables (the token-name table, the character
 * token names, MariaDB's keyword lengths) are written once, lazily, and are
 * read-only afterwards.  Every thread that returns from mdbp_once() sees the
 * finished table, which is the guarantee a double-checked flag does not give.
 *
 * POSIX: pthread_once().  Windows: InitOnceExecuteOnce().  C11 call_once() is
 * not used because macOS's libc has no <threads.h>. */
#ifndef MDBP_ONCE_H
#define MDBP_ONCE_H

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef INIT_ONCE mdbp_once_t;
#define MDBP_ONCE_INIT INIT_ONCE_STATIC_INIT

typedef void (*mdbp_once_fn)(void);

static BOOL CALLBACK mdbp_once_trampoline(PINIT_ONCE once, PVOID param,
                                          PVOID *context)
{
    (void) once;
    (void) context;
    (*(mdbp_once_fn *) param)();
    return TRUE;
}

static inline void mdbp_once(mdbp_once_t *once, mdbp_once_fn fn)
{
    InitOnceExecuteOnce(once, mdbp_once_trampoline, (PVOID) &fn, NULL);
}

#else

#include <pthread.h>

typedef pthread_once_t mdbp_once_t;
#define MDBP_ONCE_INIT PTHREAD_ONCE_INIT

static inline void mdbp_once(mdbp_once_t *once, void (*fn)(void))
{
    pthread_once(once, fn);
}

#endif

#endif /* MDBP_ONCE_H */
