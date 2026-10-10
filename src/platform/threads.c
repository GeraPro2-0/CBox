/*
 * Copyright 2026 GeraPro2_0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "platform/threads.h"
#include "util/terminal.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CBOX_MAX_BUILD_WORKERS 8U
#define CBOX_MAX_BACKGROUND_WORKERS (CBOX_MAX_BUILD_WORKERS - 1U)

#ifdef _WIN32
#if defined(_MSC_VER)
#define CBOX_THREAD_LOCAL __declspec(thread)
#else
#define CBOX_THREAD_LOCAL __thread
#endif
#include <windows.h>
#include <process.h>

struct cbox_mutex {
    CRITICAL_SECTION native;
};

static SRWLOCK cbox_pool_guard = SRWLOCK_INIT;
static SRWLOCK cbox_pool_state_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE cbox_pool_work_available = CONDITION_VARIABLE_INIT;
static CONDITION_VARIABLE cbox_pool_work_complete = CONDITION_VARIABLE_INIT;
static HANDLE cbox_pool_threads[CBOX_MAX_BACKGROUND_WORKERS];
#else
#define CBOX_THREAD_LOCAL _Thread_local
#include <pthread.h>
#include <unistd.h>

struct cbox_mutex {
    pthread_mutex_t native;
};

static pthread_mutex_t cbox_pool_guard = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cbox_pool_state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cbox_pool_work_available = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cbox_pool_work_complete = PTHREAD_COND_INITIALIZER;
static pthread_t cbox_pool_threads[CBOX_MAX_BACKGROUND_WORKERS];
#endif

typedef struct {
    size_t worker_id;
} cbox_pool_worker_argument;

static cbox_pool_worker_argument
    cbox_pool_worker_arguments[CBOX_MAX_BACKGROUND_WORKERS];
static size_t cbox_pool_thread_count;
static bool cbox_pool_shutdown_registered;
static bool cbox_pool_stopping;
static size_t cbox_pool_generation;
static size_t cbox_pool_active_workers;
static size_t cbox_pool_completed_workers;
static size_t cbox_pool_count;
static size_t cbox_pool_next;
static cbox_parallel_job_fn cbox_pool_job;
static void *cbox_pool_context;
static bool *cbox_pool_results;
static CBOX_THREAD_LOCAL unsigned int cbox_parallel_callback_depth;

#ifdef _WIN32
static void cbox_pool_lock(void) {
    AcquireSRWLockExclusive(&cbox_pool_state_lock);
}

static void cbox_pool_unlock(void) {
    ReleaseSRWLockExclusive(&cbox_pool_state_lock);
}

static void cbox_pool_wake_workers(void) {
    WakeAllConditionVariable(&cbox_pool_work_available);
}

static void cbox_pool_wait_for_work(void) {
    (void)SleepConditionVariableSRW(&cbox_pool_work_available,
                                    &cbox_pool_state_lock, INFINITE, 0);
}

static void cbox_pool_wait_for_completion(void) {
    (void)SleepConditionVariableSRW(&cbox_pool_work_complete,
                                    &cbox_pool_state_lock, INFINITE, 0);
}

static void cbox_pool_signal_completion(void) {
    WakeAllConditionVariable(&cbox_pool_work_complete);
}
#else
static void cbox_pool_lock(void) {
    (void)pthread_mutex_lock(&cbox_pool_state_lock);
}

static void cbox_pool_unlock(void) {
    (void)pthread_mutex_unlock(&cbox_pool_state_lock);
}

static void cbox_pool_wake_workers(void) {
    (void)pthread_cond_broadcast(&cbox_pool_work_available);
}

static void cbox_pool_wait_for_work(void) {
    (void)pthread_cond_wait(&cbox_pool_work_available, &cbox_pool_state_lock);
}

static void cbox_pool_wait_for_completion(void) {
    (void)pthread_cond_wait(&cbox_pool_work_complete, &cbox_pool_state_lock);
}

static void cbox_pool_signal_completion(void) {
    (void)pthread_cond_broadcast(&cbox_pool_work_complete);
}
#endif

static void cbox_pool_consume(void) {
    for (;;) {
        size_t index;
        cbox_parallel_job_fn job;
        void *context;
        bool *results;

        cbox_pool_lock();
        if (cbox_pool_next >= cbox_pool_count) {
            cbox_pool_unlock();
            return;
        }
        index = cbox_pool_next++;
        job = cbox_pool_job;
        context = cbox_pool_context;
        results = cbox_pool_results;
        cbox_pool_unlock();

        ++cbox_parallel_callback_depth;
        results[index] = job(index, context);
        --cbox_parallel_callback_depth;
    }
}

#ifdef _WIN32
static unsigned __stdcall cbox_pool_worker(void *argument) {
    size_t worker_id = ((cbox_pool_worker_argument *)argument)->worker_id;
    size_t observed_generation = 0U;

    for (;;) {
        cbox_pool_lock();
        while (!cbox_pool_stopping
               && cbox_pool_generation == observed_generation) {
            cbox_pool_wait_for_work();
        }
        if (cbox_pool_stopping) {
            cbox_pool_unlock();
            return 0U;
        }
        observed_generation = cbox_pool_generation;
        if (worker_id >= cbox_pool_active_workers) {
            cbox_pool_unlock();
            continue;
        }
        cbox_pool_unlock();

        cbox_pool_consume();

        cbox_pool_lock();
        ++cbox_pool_completed_workers;
        cbox_pool_signal_completion();
        cbox_pool_unlock();
    }
}
#else
static void *cbox_pool_worker(void *argument) {
    size_t worker_id = ((cbox_pool_worker_argument *)argument)->worker_id;
    size_t observed_generation = 0U;

    for (;;) {
        cbox_pool_lock();
        while (!cbox_pool_stopping
               && cbox_pool_generation == observed_generation) {
            cbox_pool_wait_for_work();
        }
        if (cbox_pool_stopping) {
            cbox_pool_unlock();
            return NULL;
        }
        observed_generation = cbox_pool_generation;
        if (worker_id >= cbox_pool_active_workers) {
            cbox_pool_unlock();
            continue;
        }
        cbox_pool_unlock();

        cbox_pool_consume();

        cbox_pool_lock();
        ++cbox_pool_completed_workers;
        cbox_pool_signal_completion();
        cbox_pool_unlock();
    }
}
#endif

static void cbox_pool_shutdown(void) {
    size_t index;

#ifdef _WIN32
    AcquireSRWLockExclusive(&cbox_pool_guard);
#else
    (void)pthread_mutex_lock(&cbox_pool_guard);
#endif
    cbox_pool_lock();
    cbox_pool_stopping = true;
    cbox_pool_wake_workers();
    cbox_pool_unlock();

#ifdef _WIN32
    for (index = 0U; index < cbox_pool_thread_count; ++index) {
        (void)WaitForSingleObject(cbox_pool_threads[index], INFINITE);
        (void)CloseHandle(cbox_pool_threads[index]);
    }
    ReleaseSRWLockExclusive(&cbox_pool_guard);
#else
    for (index = 0U; index < cbox_pool_thread_count; ++index) {
        (void)pthread_join(cbox_pool_threads[index], NULL);
    }
    (void)pthread_mutex_unlock(&cbox_pool_guard);
#endif
}

static bool cbox_pool_start(size_t requested_workers) {
    size_t desired_background = requested_workers - 1U;

    // Reuse workers between compile batches; creating threads for every wave would
    // spend the time parallelism is supposed to save.
    if (!cbox_pool_shutdown_registered) {
        if (atexit(cbox_pool_shutdown) != 0) {
            fprintf(stderr, "CBox: cannot register build-worker shutdown handler\n");
            return false;
        }
        cbox_pool_shutdown_registered = true;
    }
    while (cbox_pool_thread_count < desired_background) {
        size_t worker_id = cbox_pool_thread_count;
        cbox_pool_worker_arguments[worker_id].worker_id = worker_id;
#ifdef _WIN32
        {
            uintptr_t thread = _beginthreadex(NULL, 0U, cbox_pool_worker,
                &cbox_pool_worker_arguments[worker_id], 0U, NULL);
            if (thread == 0U) {
                (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
                    "warning: could not start all persistent build workers\n");
                break;
            }
            cbox_pool_threads[worker_id] = (HANDLE)thread;
        }
#else
        if (pthread_create(&cbox_pool_threads[worker_id], NULL,
                           cbox_pool_worker,
                           &cbox_pool_worker_arguments[worker_id]) != 0) {
            (void)cbox_terminal_printf(stderr, CBOX_TERMINAL_YELLOW,
                "warning: could not start all persistent build workers\n");
            break;
        }
#endif
        ++cbox_pool_thread_count;
    }
    return cbox_pool_thread_count != 0U;
}

size_t cbox_processor_count(void) {
#ifdef _WIN32
    SYSTEM_INFO information;
    GetSystemInfo(&information);
    return information.dwNumberOfProcessors > 0U
        ? (size_t)information.dwNumberOfProcessors : 1U;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (size_t)count : 1U;
#endif
}

cbox_mutex *cbox_mutex_create(void) {
    cbox_mutex *mutex = malloc(sizeof(*mutex));
    if (mutex == NULL) {
        fprintf(stderr, "CBox: out of memory creating mutex\n");
        return NULL;
    }
#ifdef _WIN32
    InitializeCriticalSection(&mutex->native);
#else
    if (pthread_mutex_init(&mutex->native, NULL) != 0) {
        fprintf(stderr, "CBox: failed to initialize mutex\n");
        free(mutex);
        return NULL;
    }
#endif
    return mutex;
}

void cbox_mutex_destroy(cbox_mutex *mutex) {
    if (mutex == NULL) return;
#ifndef _WIN32
    (void)pthread_mutex_destroy(&mutex->native);
#else
    DeleteCriticalSection(&mutex->native);
#endif
    free(mutex);
}

void cbox_mutex_lock(cbox_mutex *mutex) {
    if (mutex == NULL) return;
#ifdef _WIN32
    EnterCriticalSection(&mutex->native);
#else
    (void)pthread_mutex_lock(&mutex->native);
#endif
}

void cbox_mutex_unlock(cbox_mutex *mutex) {
    if (mutex == NULL) return;
#ifdef _WIN32
    LeaveCriticalSection(&mutex->native);
#else
    (void)pthread_mutex_unlock(&mutex->native);
#endif
}

bool cbox_parallel_for(size_t count, size_t worker_count,
                       cbox_parallel_job_fn job, void *context) {
    bool *results;
    bool success = true;
    size_t index;
    size_t effective_workers;
    size_t background_workers;
    size_t processor_count;

    if (job == NULL) return false;
    if (count == 0U) return true;
    if (count > SIZE_MAX / sizeof(*results)) {
        fprintf(stderr, "CBox: parallel job result count is too large\n");
        return false;
    }
    if (cbox_parallel_callback_depth > 0U) {
        for (index = 0U; index < count; ++index) {
            if (!job(index, context)) success = false;
        }
        return success;
    }
    if (worker_count > count) worker_count = count;
    processor_count = cbox_processor_count();
    if (worker_count > processor_count) worker_count = processor_count;
    if (worker_count > CBOX_MAX_BUILD_WORKERS) {
        worker_count = CBOX_MAX_BUILD_WORKERS;
    }
    if (worker_count < 2U) {
        for (index = 0U; index < count; ++index) {
            if (!job(index, context)) success = false;
        }
        return success;
    }

    results = calloc(count, sizeof(*results));
    if (results == NULL) {
        fprintf(stderr, "CBox: out of memory allocating parallel job results\n");
        return false;
    }
#ifdef _WIN32
    AcquireSRWLockExclusive(&cbox_pool_guard);
#else
    (void)pthread_mutex_lock(&cbox_pool_guard);
#endif
    if (!cbox_pool_start(worker_count)) {
#ifdef _WIN32
        ReleaseSRWLockExclusive(&cbox_pool_guard);
#else
        (void)pthread_mutex_unlock(&cbox_pool_guard);
#endif
        free(results);
        for (index = 0U; index < count; ++index) {
            if (!job(index, context)) success = false;
        }
        return success;
    }
    effective_workers = cbox_pool_thread_count + 1U;
    if (effective_workers > worker_count) effective_workers = worker_count;
    background_workers = effective_workers - 1U;

    cbox_pool_lock();
    cbox_pool_count = count;
    cbox_pool_next = 0U;
    cbox_pool_job = job;
    cbox_pool_context = context;
    cbox_pool_results = results;
    cbox_pool_active_workers = background_workers;
    cbox_pool_completed_workers = 0U;
    ++cbox_pool_generation;
    cbox_pool_wake_workers();
    cbox_pool_unlock();

    if (effective_workers > 1U) {
        (void)cbox_terminal_printf(stdout, CBOX_TERMINAL_CYAN,
                                   "dispatching %zu jobs across %zu build workers\n",
                                   count, effective_workers);
    }
    cbox_pool_consume();
    cbox_pool_lock();
    while (cbox_pool_completed_workers < background_workers) {
        cbox_pool_wait_for_completion();
    }
    cbox_pool_results = NULL;
    cbox_pool_job = NULL;
    cbox_pool_context = NULL;
    cbox_pool_unlock();

#ifdef _WIN32
    ReleaseSRWLockExclusive(&cbox_pool_guard);
#else
    (void)pthread_mutex_unlock(&cbox_pool_guard);
#endif

    for (index = 0U; index < count; ++index) {
        if (!results[index]) success = false;
    }
    free(results);
    return success;
}
