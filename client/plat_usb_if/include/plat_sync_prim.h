/*
 * SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 * See LICENSE.md for the full license.
 */

#ifndef PLAT_SYNC_PRIM_H
#define PLAT_SYNC_PRIM_H

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

//-----------------------------------------------------------------
// Class PlatMutex
//-----------------------------------------------------------------
class PlatMutex {
private:
    std::mutex m_mutex;
public:
    // Default constructor 
    PlatMutex() = default;

    // Lock and unlock methods
    void lock()   { m_mutex.lock(); }
    void unlock() { m_mutex.unlock(); }

    // Nested scoped lock class
    class ScopedLock {
    private:
        std::unique_lock<std::mutex> m_lock;
    public:
        explicit ScopedLock(PlatMutex& mutex) : m_lock(mutex.m_mutex) {}
        void     lock()   { m_lock.lock(); }
        void     unlock() { m_lock.unlock(); }

        // Get raw pointer for advanced use
        std::unique_lock<std::mutex>& get_lock() { return m_lock; }
        // Prevent copying
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;
    };

    // Get raw mutex pointer for advanced use
    std::mutex* get() { return &m_mutex; }
    
    // Prevent copying
    PlatMutex(const PlatMutex&) = delete;
    PlatMutex& operator=(const PlatMutex&) = delete;
};

/*
 * ----------------------------------------------------------------------------
 * Class PlatThreadCondVar
 * ----------------------------------------------------------------------------
 */
class PlatThreadCondVar {
private:
    std::mutex              m_mutex;
    std::condition_variable m_condition;
public:
    // Default constructor 
    PlatThreadCondVar() = default;

    // Wait methods
    template<typename Lock>
    void wait(Lock& lock) { m_condition.wait(lock.get_lock()); }
    template<typename Lock>
    bool timed_wait(Lock& lock, const struct timespec abs_timeout) {
        auto duration = std::chrono::system_clock::from_time_t(abs_timeout.tv_sec) + 
                        std::chrono::nanoseconds(abs_timeout.tv_nsec);
        return m_condition.wait_until(lock.get_lock(), duration) != std::cv_status::timeout;
    }
    // Notify methods
    void notify_one() { m_condition.notify_one(); }
    void notify_all() { m_condition.notify_all(); }

    // Get mutex for advanced use
    std::mutex* get_mutex() { return &m_mutex; }

    // Prevent copying
    PlatThreadCondVar(const PlatThreadCondVar&) = delete;
    PlatThreadCondVar& operator=(const PlatThreadCondVar&) = delete;
};

#endif // PLAT_SYNC_PRIM_H