// SPDX-License-Identifier: GPL-3.0-only
// Test-only interposition. It does not prove all library/syscall paths are covered.
#include "rt_audit.hpp"
#include <cstdlib>
#include <algorithm>
#include <new>
#ifdef _WIN32
#include <malloc.h>
#endif
thread_local bool rt_audit::active = false;
thread_local rt_audit::Counts rt_audit::counts;
void *operator new(std::size_t n) {
    if (rt_audit::active)
        ++rt_audit::counts.cppAllocate;
    if (auto *p = std::malloc(std::max(n, std::size_t{1})))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    if (p && rt_audit::active)
        ++rt_audit::counts.cppFree;
    std::free(p);
}
void operator delete[](void *p) noexcept {
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void *operator new(std::size_t n, std::align_val_t alignment) {
    if (rt_audit::active)
        ++rt_audit::counts.cppAllocate;
    const auto a = static_cast<std::size_t>(alignment);
    void *p = nullptr;
#ifdef _WIN32
    p = _aligned_malloc(std::max(n, std::size_t{1}), a);
#else
    if (posix_memalign(&p, a, std::max(n, std::size_t{1})))
        p = nullptr;
#endif
    if (!p)
        throw std::bad_alloc();
    return p;
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete(void *p, std::align_val_t) noexcept {
    if (p && rt_audit::active)
        ++rt_audit::counts.cppFree;
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void *p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
#ifdef SC_WRAP_LIBC
#include <pthread.h>
extern "C" {
void *__real_malloc(std::size_t);
void __real_free(void *);
void *__real_calloc(std::size_t, std::size_t);
void *__real_realloc(void *, std::size_t);
int __real_pthread_mutex_lock(pthread_mutex_t *);
void *__wrap_malloc(std::size_t n) {
    if (rt_audit::active)
        ++rt_audit::counts.cAllocate;
    return __real_malloc(n);
}
void __wrap_free(void *p) {
    if (p && rt_audit::active)
        ++rt_audit::counts.cFree;
    __real_free(p);
}
void *__wrap_calloc(std::size_t n, std::size_t size) {
    if (rt_audit::active)
        ++rt_audit::counts.cAllocate;
    return __real_calloc(n, size);
}
void *__wrap_realloc(void *p, std::size_t size) {
    if (rt_audit::active) {
        ++rt_audit::counts.cAllocate;
        if (p)
            ++rt_audit::counts.cFree;
    }
    return __real_realloc(p, size);
}
int __wrap_pthread_mutex_lock(pthread_mutex_t *p) {
    if (rt_audit::active)
        ++rt_audit::counts.blockingLock;
    return __real_pthread_mutex_lock(p);
}
}
#endif
