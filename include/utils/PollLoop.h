/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef UTILS_POLL_LOOP_H
#define UTILS_POLL_LOOP_H

#include <utils/Vector.h>
#include <utils/threads.h>

#include <sys/poll.h>

#include <android/looper.h>

struct ALooper : public android::RefBase {
protected:
    virtual ~ALooper() { }

public:
    ALooper() { }
};

namespace android {

/**
 * A basic file descriptor polling loop based on poll() with callbacks.
 */
class PollLoop : public ALooper {
protected:
    virtual ~PollLoop();

public:
    PollLoop(bool allowNonCallbacks);

    /**
     * A callback that it to be invoked when an event occurs on a file descriptor.
     * Specifies the events that were triggered and the user data provided when the
     * callback was set.
     *
     * Returns true if the callback should be kept, false if it should be removed automatically
     * after the callback returns.
     */
    typedef bool (*Callback)(int fd, int events, void* data);

    enum {
        POLL_CALLBACK = ALOOPER_POLL_CALLBACK,
        POLL_TIMEOUT = ALOOPER_POLL_TIMEOUT,
        POLL_ERROR = ALOOPER_POLL_ERROR,
    };
    
    /**
     * Performs a single call to poll() with optional timeout in milliseconds.
     * Invokes callbacks for all file descriptors on which an event occurred.
     *
     * If the timeout is zero, returns immediately without blocking.
     * If the timeout is negative, waits indefinitely until awoken.
     *
     * Returns ALOOPER_POLL_CALLBACK if a callback was invoked.
     *
     * Returns ALOOPER_POLL_TIMEOUT if there was no data before the given
     * timeout expired.
     *
     * Returns ALOPER_POLL_ERROR if an error occurred.
     *
     * Returns a value >= 0 containing a file descriptor if it has data
     * and it has no callback function (requiring the caller here to handle it).
     * In this (and only this) case outEvents and outData will contain the poll
     * events and data associated with the fd.
     *
     * This method must only be called on the thread owning the PollLoop.
     * This method blocks until either a file descriptor is signalled, a timeout occurs,
     * or wake() is called.
     * This method does not return until it has finished invoking the appropriate callbacks
     * for all file descriptors that were signalled.
     */
    int32_t pollOnce(int timeoutMillis, int* outEvents = NULL, void** outData = NULL);

    /**
     * Wakes the loop asynchronously.
     *
     * This method can be called on any thread.
     * This method returns immediately.
     */
    void wake();

    /**
     * Control whether this PollLoop instance allows using IDs instead
     * of callbacks.
     */
    bool getAllowNonCallbacks() const;
    
    /**
     * Sets the callback for a file descriptor, replacing the existing one, if any.
     * It is an error to call this method with events == 0 or callback == NULL.
     *
     * Note that a callback can be invoked with the POLLERR, POLLHUP or POLLNVAL events
     * even if it is not explicitly requested when registered.
     *
     * This method can be called on any thread.
     * This method may block briefly if it needs to wake the poll loop.
     */
    void setCallback(int fd, int ident, int events, Callback callback, void* data = NULL);

    /**
     * Convenience for above setCallback when ident is not used.  In this case
     * the ident is set to POLL_CALLBACK.
     */
    void setCallback(int fd, int events, Callback callback, void* data = NULL);
    
    /**
     * Like setCallback(), but for the NDK callback function.
     */
    void setLooperCallback(int fd, int ident, int events, ALooper_callbackFunc* callback,
            void* data);
    
    /**
     * Removes the callback for a file descriptor, if one exists.
     *
     * When this method returns, it is safe to close the file descriptor since the poll loop
     * will no longer have a reference to it.  However, it is possible for the callback to
     * already be running or for it to run one last time if the file descriptor was already
     * signalled.  Calling code is responsible for ensuring that this case is safely handled.
     * For example, if the callback takes care of removing itself during its own execution either
     * by returning false or calling this method, then it can be guaranteed to not be invoked
     * again at any later time unless registered anew.
     *
     * This method can be called on any thread.
     * This method may block briefly if it needs to wake the poll loop.
     *
     * Returns true if a callback was actually removed, false if none was registered.
     */
    bool removeCallback(int fd);

    /**
     * Set the given PollLoop to be associated with the
     * calling thread.  There must be a 1:1 relationship between
     * PollLoop and thread.
     */
    static void setForThread(const sp<PollLoop>& pollLoop);
    
    /**
     * Return the PollLoop associated with the calling thread.
     */
    static sp<PollLoop> getForThread();
    
private:
    struct RequestedCallback {
        Callback callback;
        ALooper_callbackFunc* looperCallback;
        int ident;
        void* data;
    };

    struct PendingCallback {
        int fd;
        int ident;
        int events;
        Callback callback;
        ALooper_callbackFunc* looperCallback;
        void* data;
    };
    
    const bool mAllowNonCallbacks; // immutable

    int mWakeReadPipeFd;  // immutable
    int mWakeWritePipeFd; // immutable

    // The lock guards state used to track whether there is a poll() in progress and whether
    // there are any other threads waiting in wakeAndLock().  The condition variables
    // are used to transfer control among these threads such that all waiters are
    // serviced before a new poll can begin.
    // The wakeAndLock() method increments mWaiters, wakes the poll, blocks on mAwake
    // until mPolling becomes false, then decrements mWaiters again.
    // The poll() method blocks on mResume until mWaiters becomes 0, then sets
    // mPolling to true, blocks until the poll completes, then resets mPolling to false
    // and signals mResume if there are waiters.
    Mutex mLock;
    bool mPolling;      // guarded by mLock
    uint32_t mWaiters;  // guarded by mLock
    Condition mAwake;   // guarded by mLock
    Condition mResume;  // guarded by mLock

    // The next two vectors are only mutated when mPolling is false since they must
    // not be changed while the poll() system call is in progress.  To mutate these
    // vectors, the poll() must first be awoken then the lock acquired.
    Vector<struct pollfd> mRequestedFds;
    Vector<RequestedCallback> mRequestedCallbacks;

    // This state is only used privately by pollOnce and does not require a lock since
    // it runs on a single thread.
    Vector<PendingCallback> mPendingCallbacks;
    Vector<PendingCallback> mPendingFds;
    size_t mPendingFdsPos;
    
    void openWakePipe();
    void closeWakePipe();

    void setCallbackCommon(int fd, int ident, int events, Callback callback,
            ALooper_callbackFunc* looperCallback, void* data);
    ssize_t getRequestIndexLocked(int fd);
    void wakeAndLock();
    static void threadDestructor(void *st);
};

} // namespace android

#endif // UTILS_POLL_LOOP_H
