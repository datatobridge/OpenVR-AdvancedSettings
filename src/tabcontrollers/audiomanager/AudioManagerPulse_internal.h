#pragma once
#include <pulse/pulseaudio.h>
#include <string>
#include <easylogging++.h>
#include "AudioManager.h"

// Used to get the compiler to shut up about C4100: unreferenced formal
// parameter. The cast is to get GCC to shut up about it.
#define UNREFERENCED_PARAMETER( P ) static_cast<void>( ( P ) )

namespace advsettings
{
enum class PulseAudioIsLastMeaning
{
    Error,
    RealDevice,
    PreviousDeviceWasLastReal,
};

enum class PulseAudioLoopControl
{
    Stop,
    Run,
};

struct
{
    pa_mainloop* mainLoop;
    pa_mainloop_api* api;
    pa_context* context;
} pulseAudioPointers;

struct
{
    std::string defaultSinkOutputDeviceId;
    std::string defaultSourceInputDeviceId;

    std::string originalDefaultOutputDeviceId;
    std::string originalDefaultInputDeviceId;

    std::vector<AudioDevice> sinkOutputDevices;
    std::vector<AudioDevice> sourceInputDevices;

    pa_sink_info currentDefaultSinkInfo;
    pa_source_info currentDefaultSourceInfo;
} pulseAudioData;

PulseAudioLoopControl loopControl = PulseAudioLoopControl::Run;

void customPulseLoop()
{
    while ( loopControl == PulseAudioLoopControl::Run )
    {
        constexpr auto noReturnValue = nullptr;
        constexpr auto blockForEvents = 1;
        pa_mainloop_iterate(
            pulseAudioPointers.mainLoop, blockForEvents, noReturnValue );
    }

    loopControl = PulseAudioLoopControl::Run;
}

// Error function
void dumpPulseAudioState()
{
    LOG( ERROR ) << "Dumping PulseAudio state: ";
    LOG( ERROR ) << "mainLoop: " << pulseAudioPointers.mainLoop;
    LOG( ERROR ) << "api: " << pulseAudioPointers.api;
    LOG( ERROR ) << "context: " << pulseAudioPointers.context;

    LOG( ERROR ) << "sinkOutputDevices: ";
    for ( const auto& device : pulseAudioData.sinkOutputDevices )
    {
        LOG( ERROR ) << "\tDevice Name: " << device.name();
        LOG( ERROR ) << "\tDevice Id: " << device.id();
    }

    LOG( ERROR ) << "sourceInputDevices: ";
    for ( const auto& device : pulseAudioData.sourceInputDevices )
    {
        LOG( ERROR ) << "\tDevice Name: " << device.name();
        LOG( ERROR ) << "\tDevice Id: " << device.id();
    }
}

constexpr PulseAudioIsLastMeaning getIsLastMeaning( const int isLast ) noexcept
{
    if ( isLast < 0 )
    {
        LOG( ERROR ) << "Error in isLast.";
        dumpPulseAudioState();
        return PulseAudioIsLastMeaning::Error;
    }

    if ( isLast > 0 )
    {
        return PulseAudioIsLastMeaning::PreviousDeviceWasLastReal;
    }

    return PulseAudioIsLastMeaning::RealDevice;
}

std::string getDeviceName( pa_proplist* p )
{
    if ( !p )
    {
        LOG( ERROR ) << "proplist not valid.";
    }

    constexpr auto deviceDescription = "device.description";
    if ( !pa_proplist_contains( p, deviceDescription ) )
    {
        LOG( ERROR ) << "proplist does not contain '" << deviceDescription
                     << "'.";
        return "ERROR";
    }

    std::string s;
    s.assign( pa_proplist_gets( p, deviceDescription ) );
    return s;
}

template <class T> void deviceCallback( const T* i, const int isLast )
{
    const auto deviceState = getIsLastMeaning( isLast );
    if ( deviceState == PulseAudioIsLastMeaning::PreviousDeviceWasLastReal )
    {
        loopControl = PulseAudioLoopControl::Stop;
        return;
    }
    else if ( deviceState == PulseAudioIsLastMeaning::Error )
    {
        LOG( ERROR ) << "Error in deviceCallback function.";
        dumpPulseAudioState();
        loopControl = PulseAudioLoopControl::Stop;
        return;
    }

    if constexpr ( std::is_same<pa_source_info, T>::value )
    {
        if ( i->name == pulseAudioData.defaultSourceInputDeviceId )
        {
            pulseAudioData.currentDefaultSourceInfo = *i;
        }

        pulseAudioData.sourceInputDevices.push_back(
            AudioDevice( i->name, getDeviceName( i->proplist ) ) );
    }

    else if constexpr ( std::is_same<pa_sink_info, T>::value )
    {
        if ( i->name == pulseAudioData.defaultSinkOutputDeviceId )
        {
            pulseAudioData.currentDefaultSinkInfo = *i;
        }

        pulseAudioData.sinkOutputDevices.push_back(
            AudioDevice( i->name, getDeviceName( i->proplist ) ) );
    }
}

void setInputDevicesCallback( pa_context* c,
                              const pa_source_info* i,
                              int isLast,
                              void* userdata )
{
    UNREFERENCED_PARAMETER( userdata );
    UNREFERENCED_PARAMETER( c );

    deviceCallback( i, isLast );
}

void setOutputDevicesCallback( pa_context* c,
                               const pa_sink_info* i,
                               int isLast,
                               void* userdata )
{
    UNREFERENCED_PARAMETER( userdata );
    UNREFERENCED_PARAMETER( c );

    deviceCallback( i, isLast );
}

void getDefaultDevicesCallback( pa_context* c,
                                const pa_server_info* i,
                                void* userdata )
{
    UNREFERENCED_PARAMETER( c );
    UNREFERENCED_PARAMETER( userdata );

    if ( !i )
    {
        LOG( ERROR ) << "i == 0";
        pulseAudioData.defaultSinkOutputDeviceId = "";
        pulseAudioData.defaultSourceInputDeviceId = "";
        return;
    }

    // Copy because we don't know how long the pa_server_info* lives for
    pulseAudioData.defaultSinkOutputDeviceId.assign( i->default_sink_name );
    pulseAudioData.defaultSourceInputDeviceId.assign( i->default_source_name );

    loopControl = PulseAudioLoopControl::Stop;
}

void stateCallbackFunction( pa_context* c, void* userdata )
{
    ( void ) c;
    ( void ) userdata;

    switch ( pa_context_get_state( c ) )
    {
    case PA_CONTEXT_TERMINATED:
        LOG( ERROR ) << "PA_CONTEXT_TERMINATED in stateCallbackFunction";
        dumpPulseAudioState();
        return;
    case PA_CONTEXT_CONNECTING:
        LOG( INFO ) << "PA_CONTEXT_CONNECTING";
        return;
    case PA_CONTEXT_AUTHORIZING:
        LOG( INFO ) << "PA_CONTEXT_AUTHORIZING";
        return;
    case PA_CONTEXT_SETTING_NAME:
        LOG( INFO ) << "PA_CONTEXT_SETTING_NAME";
        return;
    case PA_CONTEXT_UNCONNECTED:
        LOG( INFO ) << "PA_CONTEXT_UNCONNECTED";
        return;
    case PA_CONTEXT_FAILED:
        LOG( INFO ) << "PA_CONTEXT_FAILED";
        return;

    case PA_CONTEXT_READY:
        LOG( INFO ) << "PA_CONTEXT_READY";
        loopControl = PulseAudioLoopControl::Stop;
        return;
    }
}

void updateAllPulseData()
{
    constexpr auto noCustomUserdata = nullptr;

    pulseAudioData.sinkOutputDevices.clear();
    pa_context_get_sink_info_list( pulseAudioPointers.context,
                                   setOutputDevicesCallback,
                                   noCustomUserdata );
    customPulseLoop();

    pulseAudioData.sourceInputDevices.clear();
    pa_context_get_source_info_list(
        pulseAudioPointers.context, setInputDevicesCallback, noCustomUserdata );
    customPulseLoop();

    pa_context_get_server_info( pulseAudioPointers.context,
                                getDefaultDevicesCallback,
                                noCustomUserdata );
    customPulseLoop();
}

std::string getCurrentDefaultPlaybackDeviceName()
{
    updateAllPulseData();

    for ( const auto& dev : pulseAudioData.sinkOutputDevices )
    {
        if ( dev.id() == pulseAudioData.defaultSinkOutputDeviceId )
        {
            return dev.name();
        }
    }
    LOG( ERROR ) << "Unable to find default playback device.";

    return "ERROR";
}

std::string getCurrentDefaultPlaybackDeviceId()
{
    updateAllPulseData();

    return pulseAudioData.defaultSinkOutputDeviceId;
}

std::string getCurrentDefaultRecordingDeviceName()
{
    updateAllPulseData();

    for ( const auto& dev : pulseAudioData.sourceInputDevices )
    {
        if ( dev.id() == pulseAudioData.defaultSourceInputDeviceId )
        {
            return dev.name();
        }
    }
    LOG( ERROR ) << "Unable to find default playback device.";

    return "ERROR";
}

std::string getCurrentDefaultRecordingDeviceId()
{
    updateAllPulseData();

    return pulseAudioData.defaultSourceInputDeviceId;
}

std::vector<AudioDevice> returnRecordingDevices()
{
    updateAllPulseData();

    return pulseAudioData.sourceInputDevices;
}

std::vector<AudioDevice> returnPlaybackDevices()
{
    updateAllPulseData();

    return pulseAudioData.sinkOutputDevices;
}

float getMicrophoneVolume()
{
    updateAllPulseData();

    return static_cast<float>(
        pulseAudioData.currentDefaultSourceInfo.volume.values[0] );
}

bool getMicrophoneMuted()
{
    updateAllPulseData();

    return pulseAudioData.currentDefaultSourceInfo.mute;
}

void micMuteStatusCallback( pa_context* c, int success, void* userdata )
{
    UNREFERENCED_PARAMETER( c );
    if ( success )
    {
        *static_cast<bool*>( userdata ) = true;
    }
    else
    {
        LOG( ERROR ) << "Error setting mic mute status.";
    }

    loopControl = PulseAudioLoopControl::Stop;
}

bool setMicMuteState( const bool muted )
{
    bool success = false;

    pa_context_set_source_mute_by_name(
        pulseAudioPointers.context,
        pulseAudioData.defaultSourceInputDeviceId.c_str(),
        muted,
        micMuteStatusCallback,
        &success );

    customPulseLoop();

    LOG( INFO ) << "success: " << success;
    return success;
}

void initializePulseAudio()
{
    pulseAudioPointers.mainLoop = pa_mainloop_new();

    pulseAudioPointers.api = pa_mainloop_get_api( pulseAudioPointers.mainLoop );

    pulseAudioPointers.context
        = pa_context_new( pulseAudioPointers.api, "openvr-advanced-settings" );

    constexpr auto noCustomUserdata = nullptr;
    pa_context_set_state_callback(
        pulseAudioPointers.context, stateCallbackFunction, noCustomUserdata );

    constexpr auto useDefaultServer = nullptr;
    constexpr auto useDefaultSpawnApi = nullptr;
    pa_context_connect( pulseAudioPointers.context,
                        useDefaultServer,
                        PA_CONTEXT_NOFLAGS,
                        useDefaultSpawnApi );
    customPulseLoop();

    pulseAudioData.originalDefaultInputDeviceId
        = getCurrentDefaultRecordingDeviceId();
    pulseAudioData.originalDefaultOutputDeviceId
        = getCurrentDefaultPlaybackDeviceId();
}
} // namespace advsettings
