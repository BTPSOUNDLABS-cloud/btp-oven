#pragma once

#include <JuceHeader.h>
#include "EngineClient.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

/**
    Owns the engine worker thread.

    Why this lives in the processor and not in an editor: FL unloads the plugin
    DLL after the processor is destroyed, but an editor can be created and
    destroyed many times. A thread owned by an editor can outlive the module and
    crash the host on unload. A thread owned here is joined exactly once, before
    the module goes away.
*/
class EngineService
{
public:
    using Callback = std::function<void (const EngineReply&)>;

    EngineService();
    ~EngineService();

    void setEndpoint (const juce::String& host, int port);
    void health (int timeoutMs, Callback callback);
    void post (const juce::var& request, int timeoutMs, Callback callback);

    /** Ping the engine and, if it is not running, start it and wait for it to
        answer. Runs on the worker thread, so it never blocks the message
        thread. Safe to call more than once: the start is attempted once. */
    void ensureRunning (Callback callback);

    void shutdown();

private:
    struct Job
    {
        juce::var request;
        int timeoutMs = 15000;
        Callback callback;
        bool ensureRunning = false;
    };

    void run();

    EngineReply ensureEngineRunning();

    static juce::File findEngineDir();
    static bool spawnEngine (const juce::File& dir, int port);

    EngineClient client;
    std::thread worker;
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<Job> queue;
    bool stopping = false;
    bool spawnAttempted = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EngineService)
};
