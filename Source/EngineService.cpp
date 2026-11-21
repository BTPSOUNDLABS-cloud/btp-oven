#include "EngineService.h"

#include <cstdlib>

EngineService::EngineService()
{
    worker = std::thread ([this] { run(); });
}

EngineService::~EngineService()
{
    shutdown();
}

void EngineService::setEndpoint (const juce::String& host, int port)
{
    std::lock_guard<std::mutex> lock (mutex);
    client.setEndpoint (host, port);
}

void EngineService::health (int timeoutMs, Callback callback)
{
    post (EngineClient::makeRequest ("ping"), timeoutMs, std::move (callback));
}

void EngineService::post (const juce::var& request, int timeoutMs, Callback callback)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (stopping)
            return;
        queue.push_back ({ request, timeoutMs, std::move (callback), false });
    }
    condition.notify_one();
}

void EngineService::ensureRunning (Callback callback)
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (stopping)
            return;
        queue.push_back ({ {}, 0, std::move (callback), true });
    }
    condition.notify_one();
}

juce::File EngineService::findEngineDir()
{
    if (const auto* env = std::getenv ("BTP_ENGINE_DIR"))
    {
        const juce::File fromEnv (juce::String::fromUTF8 (env));
        if (fromEnv.getChildFile ("voxchain").isDirectory())
            return fromEnv;
    }

    // A copy shipped with the plugin, once the hub exists.
    const auto bundled = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                             .getChildFile ("BtP Oven")
                             .getChildFile ("engine");

    if (bundled.getChildFile ("voxchain").isDirectory())
        return bundled;

    // Development fallback: the source tree this machine builds from.
    const juce::File dev ("D:/Dev/ai-gen-app/Vocal chain builder");
    if (dev.getChildFile ("voxchain").isDirectory())
        return dev;

    return {};
}

bool EngineService::spawnEngine (const juce::File& dir, int port)
{
    // pythonw keeps the engine headless. The working directory matters because
    // `python -m voxchain` resolves the package from it.
    const auto command = "cmd.exe /c cd /d \"" + dir.getFullPathName()
                       + "\" && pythonw -m voxchain serve --port " + juce::String (port);

    juce::ChildProcess child;

    // The ChildProcess destructor does not kill the process, so the engine
    // outlives this handle and stays warm for the next open.
    return child.start (command, 0);
}

EngineReply EngineService::ensureEngineRunning()
{
    auto reply = client.health (1500);

    if (reply.ok)
        return reply;

    const auto dir = findEngineDir();

    if (! dir.isDirectory())
    {
        reply.ok = false;
        reply.errorMessage = "engine is not running, and no engine folder was found "
                             "(set BTP_ENGINE_DIR to the folder containing voxchain/)";
        return reply;
    }

    if (! spawnAttempted)
    {
        spawnAttempted = true;

        if (! spawnEngine (dir, client.getPort()))
        {
            reply.ok = false;
            reply.errorMessage = "could not start the engine from " + dir.getFullPathName();
            return reply;
        }
    }

    // Loading numpy/scipy takes a few seconds; give it twenty before giving up.
    for (int i = 0; i < 40; ++i)
    {
        juce::Thread::sleep (500);
        reply = client.health (1500);
        if (reply.ok)
            return reply;
    }

    return reply;
}

void EngineService::shutdown()
{
    {
        std::lock_guard<std::mutex> lock (mutex);
        if (stopping)
            return;
        stopping = true;
    }
    condition.notify_all();

    if (worker.joinable())
        worker.join();

    std::lock_guard<std::mutex> lock (mutex);
    queue.clear();
}

void EngineService::run()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock (mutex);
            condition.wait (lock, [this] { return stopping || ! queue.empty(); });

            if (stopping)
                return;

            job = std::move (queue.front());
            queue.pop_front();
        }

        const auto reply = job.ensureRunning ? ensureEngineRunning()
                                            : client.post (job.request, job.timeoutMs);

        if (job.callback)
        {
            auto callback = job.callback;
            juce::MessageManager::callAsync ([callback, reply] { callback (reply); });
        }
    }
}
