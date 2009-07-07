/* threads.c: Thread Abstraction Functions
 *
 * Copyright (c) 1999, 2000 the icecast team <team@icecast.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the Free
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
 #include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/types.h>

#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#else
#include <windows.h>
#include <winbase.h>
#endif

#ifdef HAVE_FTIME
#include <sys/timeb.h>
#endif

#include <signal.h>

#include <timing/timing.h>
#include <thread/thread.h>
#include <avl/avl.h>
#ifdef THREAD_DEBUG
#include <log/log.h>
#endif

#ifdef _WIN32
#define __FUNCTION__ __FILE__
#endif

#ifdef THREAD_DEBUG
#define CATMODULE "thread"
#define LOG_ERROR(y) log_write(_logid, 1, CATMODULE "/", __FUNCTION__, y)
#define LOG_ERROR1(y, z1) log_write(_logid, 1, CATMODULE "/", __FUNCTION__, y, z1)
#define LOG_ERROR3(y, z1, z2, z3) log_write(_logid, 1, CATMODULE "/", __FUNCTION__, y, z1, z2, z3)
#define LOG_ERROR7(y, z1, z2, z3, z4, z5, z6, z7) log_write(_logid, 1, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4, z5, z6, z7)

#define LOG_WARN(y) log_write(_logid, 2, CATMODULE "/", __FUNCTION__, y)
#define LOG_WARN3(y, z1, z2, z3) log_write(_logid, 2, CATMODULE "/", __FUNCTION__, y, z1, z2, z3)
#define LOG_WARN5(y, z1, z2, z3, z4, z5) log_write(_logid, 2, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4, z5)
#define LOG_WARN7(y, z1, z2, z3, z4, z5, z6, z7) log_write(_logid, 2, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4, z5, z6, z7)

#define LOG_INFO(y) log_write(_logid, 3, CATMODULE "/", __FUNCTION__, y)
#define LOG_INFO4(y, z1, z2, z3, z4) log_write(_logid, 3, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4)
#define LOG_INFO5(y, z1, z2, z3, z4, z5) log_write(_logid, 3, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4, z5)

#define LOG_DEBUG(y) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y)
#define LOG_DEBUG1(y, z1) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y, z1)
#define LOG_DEBUG2(y, z1, z2) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y, z1, z2)
#define LOG_DEBUG3(y, z1, z2, z3) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y, z1, z2, z3)
#define LOG_DEBUG4(y, z1, z2, z3, z4) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4)
#define LOG_DEBUG5(y, z1, z2, z3, z4, z5) log_write(_logid, 4, CATMODULE "/", __FUNCTION__, y, z1, z2, z3, z4, z5)
#endif

/* thread starting structure */
typedef struct thread_start_tag {
    /* the real start routine and arg */
    void *(*start_routine)(void *);
    void *arg;

    /* the other stuff we need to make sure this thread is inserted into
    ** the thread tree
    */
    thread_type *thread;
    pthread_t sys_thread;
} thread_start_t;

static long _next_thread_id = 0;
static int _initialized = 0;
static avl_tree *_threadtree = NULL;
static int abort_on_mutex_timeout;


#ifdef THREAD_DEBUG

/* this is x86 specific, but gets a very precise and low overhead
 * timer, other platforms may have similar mechanisms
 */
#define rdtscll(val) \
     __asm__ __volatile__("rdtsc" : "=A" (val))

static inline unsigned long long get_count (void)
{
    unsigned long long ret;

    rdtscll(ret);
    return ret;
}

static int _logid = -1;
static long _next_mutex_id = 0;
static avl_tree *_mutextree = NULL;

static mutex_t _threadtree_mutex;
static mutex_t _mutextree_mutex;
static mutex_t _library_mutex;

static int _compare_mutexes(void *compare_arg, void *a, void *b);
static int _free_mutex(void *key);

#else

static mutex_t _threadtree_mutex = { PTHREAD_MUTEX_INITIALIZER };
static mutex_t _library_mutex = { PTHREAD_MUTEX_INITIALIZER };

#endif

/* INTERNAL FUNCTIONS */

static int _compare_threads(void *compare_arg, void *a, void *b);
static int _free_thread(void *key);

/* mutex fuctions */
static void _mutex_create(mutex_t *mutex);
static void _mutex_lock(mutex_t *mutex);
static void _mutex_unlock(mutex_t *mutex);

/* misc thread stuff */
static void *_start_routine(void *arg);
static void _catch_signals(void);
static void _block_signals(void);

/* LIBRARY INITIALIZATION */

void thread_initialize(void)
{
    thread_type *thread;

    /* set up logging */

#ifdef THREAD_DEBUG
    /* create all the internal mutexes, and initialize the mutex tree */

    _mutextree = avl_tree_new(_compare_mutexes, NULL);

    /* we have to create this one by hand, because there's no
    ** mutextree_mutex to lock yet! 
    */
    _mutex_create(&_mutextree_mutex);

    _mutextree_mutex.mutex_id = _next_mutex_id++;
    avl_insert(_mutextree, (void *)&_mutextree_mutex);

    log_initialize();
    _logid = log_open("thread.log");
    log_set_level(_logid, 4);
#endif

    thread_mutex_create(&_threadtree_mutex);
    thread_mutex_create(&_library_mutex);    

    /* initialize the thread tree and insert the main thread */

    _threadtree = avl_tree_new(_compare_threads, NULL);

    thread = (thread_type *)malloc(sizeof(thread_type));

    thread->thread_id = _next_thread_id++;
    thread->line = 0;
    thread->file = strdup("main.c");
    thread->sys_thread = pthread_self();
    thread->create_time = time(NULL);
    thread->name = strdup("Main Thread");

    avl_insert(_threadtree, (void *)thread);

    _catch_signals();

    abort_on_mutex_timeout = 0;
    if (getenv ("ICE_MUTEX_ABORT"))
        abort_on_mutex_timeout = 1;
    _initialized = 1;
}

void thread_shutdown(void)
{
    if (_initialized == 1) {
        thread_mutex_destroy(&_library_mutex);
        thread_mutex_destroy(&_threadtree_mutex);
#ifdef THREAD_DEBUG
        thread_mutex_destroy(&_mutextree_mutex);
        
        avl_tree_free(_mutextree, _free_mutex);
#endif
        avl_tree_free(_threadtree, _free_thread);
        _threadtree = NULL;
    }

#ifdef THREAD_DEBUG
    log_close(_logid);
    log_shutdown();
#endif

}

/*
 * Signals should be handled by the main thread, nowhere else.
 * I'm using POSIX signal interface here, until someone tells me
 * that I should use signal/sigset instead
 *
 * This function only valid for non-Win32
 */
static void _block_signals(void)
{
#ifndef _WIN32
        sigset_t ss;

        sigfillset(&ss);

        /* These ones we want */
        sigdelset(&ss, SIGKILL);
        sigdelset(&ss, SIGSTOP);
        sigdelset(&ss, SIGSEGV);
        sigdelset(&ss, SIGCHLD);
        sigdelset(&ss, SIGBUS);
        if (pthread_sigmask(SIG_BLOCK, &ss, NULL) != 0) {
#ifdef THREAD_DEBUG
                LOG_ERROR("Pthread_sigmask() failed for blocking signals");
#endif
        }
#endif
}

/*
 * Let the calling thread catch all the relevant signals
 *
 * This function only valid for non-Win32
 */
static void _catch_signals(void)
{
#ifndef _WIN32
        sigset_t ss;

        sigemptyset(&ss);

        /* These ones should only be accepted by the signal handling thread (main thread) */
        sigaddset(&ss, SIGHUP);
        sigaddset(&ss, SIGCHLD);
        sigaddset(&ss, SIGINT);
        sigaddset(&ss, SIGPIPE);
        sigaddset(&ss, SIGTERM);

        if (pthread_sigmask(SIG_UNBLOCK, &ss, NULL) != 0) {
#ifdef THREAD_DEBUG
                LOG_ERROR("pthread_sigmask() failed for catching signals!");
#endif
        }
#endif
}


thread_type *thread_create_c(char *name, void *(*start_routine)(void *), 
        void *arg, int detached, int line, char *file)
{
    int ok = 1;
    thread_type *thread = NULL;
    thread_start_t *start = NULL;
    pthread_attr_t attr;

    thread = (thread_type *)calloc(1, sizeof(thread_type));    
    do {
        if (thread == NULL)
            break;
        start = (thread_start_t *)calloc(1, sizeof(thread_start_t));
        if (start == NULL)
            break;
        if (pthread_attr_init (&attr) < 0)
            break;

        thread->line = line;
        thread->file = strdup(file);

        _mutex_lock (&_threadtree_mutex);    
        thread->thread_id = _next_thread_id++;
        _mutex_unlock (&_threadtree_mutex);

        thread->name = strdup(name);
        thread->create_time = time(NULL);

        start->start_routine = start_routine;
        start->arg = arg;
        start->thread = thread;

        pthread_attr_setstacksize (&attr, 512*1024);
        pthread_attr_setinheritsched (&attr, PTHREAD_INHERIT_SCHED);
        if (detached)
        {
            pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
            thread->detached = 1;
        }

#ifdef __OpenBSD__
        thread->running = 1;
#endif
        if (pthread_create (&thread->sys_thread, &attr, _start_routine, start) == 0)
        {
            pthread_attr_destroy (&attr);
            return thread;
        }
        else
            pthread_attr_destroy (&attr);
    }
    while (0);

#ifdef THREAD_DEBUG
    LOG_ERROR1("Could not create new thread %s", name);
#endif
    if (start) free (start);
    if (thread) free (thread);
    return NULL;
}

/* _mutex_create
** 
** creates a mutex
*/
static void _mutex_create(mutex_t *mutex)
{
#ifdef THREAD_DEBUG
    mutex->thread_id = MUTEX_STATE_NEVERLOCKED;
    mutex->line = -1;
#endif

    pthread_mutex_init(&mutex->sys_mutex, NULL);
}

void thread_mutex_create_c(mutex_t *mutex, int line, const char *file)
{
    _mutex_create(mutex);

#ifdef THREAD_DEBUG
    mutex->name = malloc (strlen (file)+20);
    sprintf (mutex->name, "%s:%d", file, line);
    _mutex_lock(&_mutextree_mutex);
    mutex->mutex_id = _next_mutex_id++;
    avl_insert(_mutextree, (void *)mutex);
    _mutex_unlock(&_mutextree_mutex);

    LOG_DEBUG3 ("mutex %s created (%s:%d)", mutex->name, file, line);
#endif
}

void thread_mutex_destroy (mutex_t *mutex)
{
    pthread_mutex_destroy(&mutex->sys_mutex);

#ifdef THREAD_DEBUG
    _mutex_lock(&_mutextree_mutex);
    avl_delete(_mutextree, mutex, _free_mutex);
    _mutex_unlock(&_mutextree_mutex);
#endif
}

void thread_mutex_lock_c(mutex_t *mutex, int line, char *file)
{
#ifdef THREAD_DEBUG
    LOG_DEBUG3("Lock on %s requested at %s:%d", mutex->name, file, line);
#endif
    _mutex_lock(mutex);
#ifdef THREAD_DEBUG
    mutex->lock_start = get_count();
    mutex->file = strdup (file);
    mutex->line = line;
    LOG_DEBUG3("Lock on %s acquired at %s:%d", mutex->name, file, line);
#endif /* THREAD_DEBUG */
}

void thread_mutex_unlock_c(mutex_t *mutex, int line, char *file)
{
    _mutex_unlock(mutex);
#ifdef THREAD_DEBUG
    LOG_DEBUG4 ("lock %s, at %s:%d lasted %llu", mutex->name, mutex->file,
            mutex->line, get_count() - mutex->lock_start);
    free (mutex->file);
    mutex->file = NULL;
#endif
}

void thread_cond_create_c(cond_t *cond, int line, char *file)
{
    pthread_cond_init(&cond->sys_cond, NULL);
}

void thread_cond_destroy(cond_t *cond)
{
    pthread_cond_destroy(&cond->sys_cond);
}

void thread_cond_signal_c(cond_t *cond, int line, char *file)
{
    cond->set = 1;
    pthread_cond_signal(&cond->sys_cond);
}

void thread_cond_broadcast_c(cond_t *cond, int line, char *file)
{
    pthread_cond_broadcast(&cond->sys_cond);
}

void thread_cond_timedwait_c(cond_t *cond, mutex_t *mutex, struct timespec *ts, int line, char *file)
{
    int rc = 0;

    cond->set = 0;
    while (cond->set == 0 && rc == 0)
        rc = pthread_cond_timedwait(&cond->sys_cond, &mutex->sys_mutex, ts);
    if (rc == 0 && cond->set == 1)
        cond->set = 0;
}

void thread_cond_wait_c(cond_t *cond, mutex_t *mutex,int line, char *file)
{
    pthread_cond_wait(&cond->sys_cond, &mutex->sys_mutex);
}

void thread_rwlock_create_c(const char *name, rwlock_t *rwlock, int line, const char *file)
{
    pthread_rwlock_init(&rwlock->sys_rwlock, NULL);
#ifdef THREAD_DEBUG
    rwlock->name = strdup (name);
    LOG_DEBUG3 ("rwlock %s created (%s:%d)", rwlock->name, file, line);
#endif
}

void thread_rwlock_destroy(rwlock_t *rwlock)
{
    pthread_rwlock_destroy(&rwlock->sys_rwlock);
#ifdef THREAD_DEBUG
    LOG_DEBUG1 ("rwlock %s destroyed", rwlock->name);
    free (rwlock->name);
    rwlock->name = NULL;
#endif
}

void thread_rwlock_rlock_c(rwlock_t *rwlock, int line, const char *file)
{
#ifdef THREAD_DEBUG
    LOG_DEBUG3("rLock on %s requested at %s:%d", rwlock->name, file, line);
#endif
    pthread_rwlock_rdlock(&rwlock->sys_rwlock);
#ifdef THREAD_DEBUG
    LOG_DEBUG3("rLock on %s acquired at %s:%d", rwlock->name, file, line);
#endif
}

void thread_rwlock_wlock_c(rwlock_t *rwlock, int line, const char *file)
{
#ifdef THREAD_DEBUG
    LOG_DEBUG3("wLock on %s requested at %s:%d", rwlock->name, file, line);
#endif
    pthread_rwlock_wrlock(&rwlock->sys_rwlock);
#ifdef THREAD_DEBUG
    LOG_DEBUG3("wLock on %s acquired at %s:%d", rwlock->name, file, line);
#endif
}

void thread_rwlock_unlock_c(rwlock_t *rwlock, int line, const char *file)
{
    pthread_rwlock_unlock(&rwlock->sys_rwlock);
#ifdef THREAD_DEBUG
    LOG_DEBUG3 ("unlock %s, at %s:%d", rwlock->name, file, line);
#endif
}

void thread_exit_c(long val, int line, char *file)
{
    thread_type *th = thread_self();

#if defined(DEBUG_MUTEXES) && defined(CHECK_MUTEXES)
    if (th) {
        avl_node *node;
        mutex_t *tmutex;
        char name[40];

        _mutex_lock(&_mutextree_mutex);

        while (node) {
            tmutex = (mutex_t *)node->key;

            if (tmutex->thread_id == th->thread_id) {
                LOG_WARN("Thread %d [%s] exiting in file %s line %d, without unlocking mutex [%s]", 
                     th->thread_id, th->name, file, line, mutex_to_string(tmutex, name));
            }

            node = avl_get_next (node);
        }

        _mutex_unlock(&_mutextree_mutex);
    }
#endif
#ifdef __OpenBSD__
    th->running = 0;
#endif

    if (th && th->detached)
    {
#ifdef THREAD_DEBUG
        LOG_DEBUG4("Removing thread %d [%s] started at [%s:%d]", th->thread_id,
                th->name, th->file, th->line);
#endif
        _mutex_lock(&_threadtree_mutex);
        avl_delete(_threadtree, th, _free_thread);
        _mutex_unlock(&_threadtree_mutex);
    }

    pthread_exit ((void*)val);
}

/* sleep for a number of microseconds */
void thread_sleep(unsigned long len)
{
#ifdef _WIN32
    Sleep(len / 1000);
#else
# ifdef HAVE_NANOSLEEP
    struct timespec time_sleep;
    struct timespec time_remaining;
    int ret;

    time_sleep.tv_sec = len / 1000000;
    time_sleep.tv_nsec = (len % 1000000) * 1000;

    ret = nanosleep(&time_sleep, &time_remaining);
    while (ret != 0 && errno == EINTR) {
        time_sleep.tv_sec = time_remaining.tv_sec;
        time_sleep.tv_nsec = time_remaining.tv_nsec;
        
        ret = nanosleep(&time_sleep, &time_remaining);
    }
# else
    struct timeval tv;

    tv.tv_sec = len / 1000000;
    tv.tv_usec = (len % 1000000);

    select(0, NULL, NULL, NULL, &tv);
# endif
#endif
}

static void *_start_routine(void *arg)
{
    thread_start_t *start = (thread_start_t *)arg;
    void *(*start_routine)(void *) = start->start_routine;
    void *real_arg = start->arg;
    thread_type *thread = start->thread;

    _block_signals();

    /* insert thread into thread tree here */
    _mutex_lock(&_threadtree_mutex);
    thread->sys_thread = pthread_self();
    avl_insert(_threadtree, (void *)thread);
    _mutex_unlock(&_threadtree_mutex);

#ifdef THREAD_DEBUG
    LOG_DEBUG4("Added thread %d [%s] started at [%s:%d]", thread->thread_id,
            thread->name, thread->file, thread->line);
#endif

    pthread_setcancelstate (PTHREAD_CANCEL_ENABLE, NULL);
    free (start);

    (start_routine)(real_arg);

#ifdef __OpenBSD__
    thread->running = 0;
#endif

    if (thread->detached)
    {
        _mutex_lock (&_threadtree_mutex);
        avl_delete (_threadtree, thread, _free_thread);
        _mutex_unlock (&_threadtree_mutex);
    }

    return NULL;
}

thread_type *thread_self(void)
{
    avl_node *node;
    thread_type *th;
    pthread_t sys_thread = pthread_self();

    _mutex_lock(&_threadtree_mutex);

    if (_threadtree == NULL) {
#ifdef THREAD_DEBUG
        LOG_WARN("Thread tree is empty, this must be wrong!");
#endif
        _mutex_unlock(&_threadtree_mutex);
        return NULL;
    }
    
    node = avl_get_first(_threadtree);
    
    while (node) {
        th = (thread_type *)node->key;

        if (th && pthread_equal(sys_thread, th->sys_thread)) {
            _mutex_unlock(&_threadtree_mutex);
            return th;
        }
        
        node = avl_get_next(node);
    }
    _mutex_unlock(&_threadtree_mutex);


#ifdef THREAD_DEBUG
    LOG_ERROR("Nonexistant thread alive...");
#endif
    
    return NULL;
}

void thread_rename(const char *name)
{
    thread_type *th;

    th = thread_self();
    if (th->name) free(th->name);

    th->name = strdup(name);
}

static void _mutex_lock(mutex_t *mutex) 
{
    if (abort_on_mutex_timeout)
    {
        struct timespec now;
        int rc;
        thread_get_timespec (&now);
        now.tv_sec += 4;
        rc = pthread_mutex_timedlock (&mutex->sys_mutex, &now);
        if (rc == ETIMEDOUT)
            abort();
        return;
    }
    pthread_mutex_lock(&mutex->sys_mutex);
}

static void _mutex_unlock(mutex_t *mutex)
{
    pthread_mutex_unlock(&mutex->sys_mutex);
}


void thread_library_lock(void)
{
    _mutex_lock(&_library_mutex);
}

void thread_library_unlock(void)
{
    _mutex_unlock(&_library_mutex);
}

void thread_join(thread_type *thread)
{
    void *ret;
    int i;

#ifdef __OpenBSD__
    /* openbsd masks signals while waiting */
    while (thread->running)
        thread_sleep (200000);
#endif
    i = pthread_join(thread->sys_thread, &ret);
    _mutex_lock(&_threadtree_mutex);
    avl_delete(_threadtree, thread, _free_thread);
    _mutex_unlock(&_threadtree_mutex);
}

/* AVL tree functions */

#ifdef THREAD_DEBUG
static int _compare_mutexes(void *compare_arg, void *a, void *b)
{
    mutex_t *m1, *m2;

    m1 = (mutex_t *)a;
    m2 = (mutex_t *)b;

    if (m1->mutex_id > m2->mutex_id)
        return 1;
    if (m1->mutex_id < m2->mutex_id)
        return -1;
    return 0;
}
#endif

static int _compare_threads(void *compare_arg, void *a, void *b)
{
    thread_type *t1, *t2;

    t1 = (thread_type *)a;
    t2 = (thread_type *)b;

    if (t1->thread_id > t2->thread_id)
        return 1;
    if (t1->thread_id < t2->thread_id)
        return -1;
    return 0;
}

#ifdef THREAD_DEBUG
static int _free_mutex(void *key)
{
    mutex_t *m;

    m = (mutex_t *)key;

    if (m && m->file) {
        free(m->file);
        m->file = NULL;
    }

    /* all mutexes are static.  don't need to free them */

    return 1;
}
#endif

static int _free_thread(void *key)
{
    thread_type *t;

    t = (thread_type *)key;

    if (t->file)
        free(t->file);
    if (t->name)
        free(t->name);

    free(t);

    return 1;
}


#ifdef HAVE_PTHREAD_SPIN_LOCK
void thread_spin_create (spin_t *spin)
{
    int x = pthread_spin_init (&spin->lock, PTHREAD_PROCESS_PRIVATE);
    if (x)
        abort();
}

void thread_spin_destroy (spin_t *spin)
{
    pthread_spin_destroy (&spin->lock);
}

void thread_spin_lock (spin_t *spin)
{
    int x = pthread_spin_lock (&spin->lock);
    if (x != 0)
        abort();
}

void thread_spin_unlock (spin_t *spin)
{
    pthread_spin_unlock (&spin->lock);
}
#endif


#ifdef HAVE_CLOCK_GETTIME
void thread_get_timespec (struct timespec *now)
{
    clock_gettime (CLOCK_REALTIME, now);
}
#elif HAVE_GETTIMEOFDAY
void thread_get_timespec (struct timespec *now)
{
    struct timeval mtv;

    gettimeofday (&mtv, NULL);
    now->tv_sec = mtv.tv_sec;
    now->tv_nsec = mtv.tv_usec*1000;
}
#elif HAVE_FTIME
void thread_get_timespec (struct timespec *now)
{
    struct timeb t;

    ftime (&t);
    now->tv_sec = t.time;
    now->tv_nsec = t.millitm * 1000000;
}
#endif


void thread_time_add_ms (struct timespec *ts, unsigned long value)
{
    if (value > 999)
    {
        ts->tv_sec += value/1000;
        value %= 1000;
    }
    ts->tv_nsec += (value*1000000);
    if (ts->tv_nsec > 999999999)
    {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000;
    }
}

