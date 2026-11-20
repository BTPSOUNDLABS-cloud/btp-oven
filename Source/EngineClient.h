#pragma once

#include <JuceHeader.h>

/**
    Talks to the local VoxChain engine over loopback HTTP.

    Deliberately synchronous. Callers run this on a background thread; nothing
    here should ever execute on the audio thread.

    The plugin never links the engine. That keeps GPL-licensed pieces (pedalboard)
    out of the shipped binary, and means the engine can be updated independently.
*/
struct EngineReply
{
    bool ok = false;
    juce::var data;
    juce::String errorCode;
    juce::String errorMessage;
    int status = 0;
    juce::String raw;

    juce::String describe() const
    {
        if (ok) return "ok";
        if (errorCode.isNotEmpty()) return errorCode + ": " + errorMessage;
        return "HTTP " + juce::String (status);
    }
};

class EngineClient
{
public:
    EngineClient() = default;
    explicit EngineClient (juce::String hostName, int portNumber)
        : host (std::move (hostName)), port (portNumber) {}

    void setEndpoint (const juce::String& hostName, int portNumber)
    {
        host = hostName;
        port = portNumber;
    }

    juce::String getHost() const { return host; }
    int getPort() const { return port; }

    EngineReply post (const juce::var& request, int timeoutMs = 120000) const;
    EngineReply health (int timeoutMs = 1500) const;

    static juce::var makeRequest (const juce::String& type)
    {
        auto* object = new juce::DynamicObject();
        object->setProperty ("type", type);
        return juce::var (object);
    }

private:
    juce::URL url (const juce::String& path) const
    {
        return juce::URL ("http://" + host + ":" + juce::String (port) + path);
    }

    EngineReply parse (const juce::String& body, int status) const;

    juce::String host { "127.0.0.1" };
    int port { 8765 };
};
