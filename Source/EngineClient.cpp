#include "EngineClient.h"

EngineReply EngineClient::parse (const juce::String& body, int status) const
{
    EngineReply reply;
    reply.status = status;
    reply.raw = body;

    auto parsed = juce::JSON::parse (body);
    if (parsed.isVoid())
    {
        reply.errorCode = "bad_json";
        reply.errorMessage = "engine returned something that is not JSON";
        return reply;
    }

    reply.ok = (bool) parsed.getProperty ("ok", false);

    // /rpc wraps its payload in "data"; /health is flat. Fall back to the whole
    // object so a health reply still exposes service, version and protocol.
    reply.data = parsed.getProperty ("data", parsed);

    if (! reply.ok)
    {
        const auto error = parsed.getProperty ("error", juce::var());
        reply.errorCode = error.getProperty ("code", "unknown").toString();
        reply.errorMessage = error.getProperty ("message", "").toString();
    }
    return reply;
}

EngineReply EngineClient::post (const juce::var& request, int timeoutMs) const
{
    EngineReply reply;
    const auto body = juce::JSON::toString (request, true);

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withConnectionTimeoutMs (timeoutMs)
                       .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n");

    auto stream = url ("/rpc").withPOSTData (body).createInputStream (options);

    if (stream == nullptr)
    {
        reply.errorCode = "engine_unreachable";
        reply.errorMessage = "cannot reach the engine on http://" + host + ":" + juce::String (port)
                             + " - start it with: python -m voxchain serve";
        return reply;
    }

    return parse (stream->readEntireStreamAsString(), 200);
}

EngineReply EngineClient::health (int timeoutMs) const
{
    EngineReply reply;
    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                       .withConnectionTimeoutMs (timeoutMs);

    auto stream = url ("/health").createInputStream (options);
    if (stream == nullptr)
    {
        reply.errorCode = "engine_unreachable";
        reply.errorMessage = "engine not running";
        return reply;
    }
    return parse (stream->readEntireStreamAsString(), 200);
}
