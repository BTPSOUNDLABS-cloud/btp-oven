#include "EngineService.h"

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
        queue.push_back ({ request, timeoutMs, std::move (callback) });
    }
    condition.notify_one();
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

        const auto reply = client.post (job.request, job.timeoutMs);

        if (job.callback)
        {
            auto callback = job.callback;
            juce::MessageManager::callAsync ([callback, reply] { callback (reply); });
        }
    }
}
