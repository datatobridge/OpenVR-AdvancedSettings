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

    float originalDefaultOutputDeviceVolume;
    float originalDefaultInputDeviceVolume;

    std::vector<AudioDevice> sinkOutputDevices;
    std::vector<AudioDevice> sourceInputDevices;

    pa_sink_info currentDefaultSinkInfo;
    pa_source_info currentDefaultSourceInfo;
} pulseAudioData;

static PulseAudioLoopControl loopControl = PulseAudioLoopControl::Run;

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

PulseAudioIsLastMeaning getIsLastMeaning( const int isLast ) noexcept
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

    LOG( DEBUG ) << "getDeviceName done with: " << s;
    return s;
}

template <class T> void deviceCallback( const T* i, const int isLast )
{
    LOG( DEBUG ) << "deviceCallback called with 'T': " << typeid( T ).name();

    static_assert(
        std::is_same<pa_source_info, T>::value
            || std::is_same<pa_sink_info, T>::value,
        "Function should only be used with pa_source_info or pa_sink_info." );

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

    LOG( DEBUG ) << "deviceCallback done with: " << i->name;
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

    LOG( DEBUG ) << "getDefaultDevicesCallback done with sink output devices: "
                 << pulseAudioData.defaultSinkOutputDeviceId
                 << " and source input "
                 << pulseAudioData.defaultSourceInputDeviceId;
}

void stateCallbackFunction( pa_context* c, void* userdata )
{
    UNREFERENCED_PARAMETER( c );
    UNREFERENCED_PARAMETER( userdata );

    switch ( pa_context_get_state( c ) )
    {
    case PA_CONTEXT_TERMINATED:
        LOG( ERROR ) << "PA_CONTEXT_TERMINATED in stateCallbackFunction";
        dumpPulseAudioState();
        return;
    case PA_CONTEXT_CONNECTING:
        LOG( DEBUG ) << "PA_CONTEXT_CONNECTING";
        return;
    case PA_CONTEXT_AUTHORIZING:
        LOG( DEBUG ) << "PA_CONTEXT_AUTHORIZING";
        return;
    case PA_CONTEXT_SETTING_NAME:
        LOG( DEBUG ) << "PA_CONTEXT_SETTING_NAME";
        return;
    case PA_CONTEXT_UNCONNECTED:
        LOG( DEBUG ) << "PA_CONTEXT_UNCONNECTED";
        return;
    case PA_CONTEXT_FAILED:
        LOG( DEBUG ) << "PA_CONTEXT_FAILED";
        return;

    case PA_CONTEXT_READY:
        LOG( DEBUG ) << "PA_CONTEXT_READY";
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

    LOG( DEBUG ) << "updateAllPulseData done.";
}

void setPlaybackCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "setPlaybackCallback called with succes: " << success;

    UNREFERENCED_PARAMETER( c );
    UNREFERENCED_PARAMETER( userdata );

    if ( !success )
    {
        LOG( ERROR ) << "Error setting mic volume status.";
    }

    loopControl = PulseAudioLoopControl::Stop;
}

void setPlaybackDeviceInternal( const std::string& id )
{
    updateAllPulseData();

    pa_context_set_default_sink(
        pulseAudioPointers.context, id.c_str(), setPlaybackCallback, nullptr );

    customPulseLoop();

    LOG( DEBUG ) << "setPlaybackDeviceInternal done with id: " << id;
}

std::string getCurrentDefaultPlaybackDeviceName()
{
    updateAllPulseData();

    for ( const auto& dev : pulseAudioData.sinkOutputDevices )
    {
        if ( dev.id() == pulseAudioData.defaultSinkOutputDeviceId )
        {
            LOG( DEBUG ) << "getCurrentDefaultPlaybackDeviceName done with "
                         << dev.name();
            return dev.name();
        }
    }
    LOG( ERROR ) << "Unable to find default playback device.";

    return "ERROR";
}

std::string getCurrentDefaultPlaybackDeviceId()
{
    updateAllPulseData();

    LOG( DEBUG ) << "getCurrentDefaultPlaybackDeviceId done with "
                 << pulseAudioData.defaultSinkOutputDeviceId;

    return pulseAudioData.defaultSinkOutputDeviceId;
}

std::string getCurrentDefaultRecordingDeviceName()
{
    updateAllPulseData();

    for ( const auto& dev : pulseAudioData.sourceInputDevices )
    {
        if ( dev.id() == pulseAudioData.defaultSourceInputDeviceId )
        {
            LOG( DEBUG ) << "getCurrentDefaultRecordingDeviceName done with: "
                         << dev.name();
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

    LOG( DEBUG ) << "getCurrentDefaultRecordingDeviceId done with "
                 << pulseAudioData.defaultSourceInputDeviceId;
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

bool isMicrophoneValid()
{
    updateAllPulseData();

    const auto valid = pulseAudioData.defaultSourceInputDeviceId != "";

    LOG( DEBUG ) << "isMicrophoneValid done with: " << valid;

    return valid;
}

float getMicrophoneVolume()
{
    updateAllPulseData();

    const auto linearVolume = pa_sw_volume_to_linear(
        pa_cvolume_avg( &pulseAudioData.currentDefaultSourceInfo.volume ) );

    LOG( DEBUG ) << "getMicrophoneVolume done with: " << linearVolume;

    return static_cast<float>( linearVolume );
}

bool getMicrophoneMuted()
{
    updateAllPulseData();

    LOG( DEBUG ) << "getMicrophoneMuted done with: "
                 << pulseAudioData.currentDefaultSourceInfo.mute;

    return pulseAudioData.currentDefaultSourceInfo.mute;
}

void setMicrophoneCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "setMicrophoneCallback called with 'success': " << success;

    UNREFERENCED_PARAMETER( c );
    UNREFERENCED_PARAMETER( userdata );

    if ( !success )
    {
        LOG( ERROR ) << "Error setting mic volume status.";
    }

    loopControl = PulseAudioLoopControl::Stop;

    LOG( DEBUG ) << "setMicrophoneCallback done.";
}

void setSourceOutputCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "setSourceOutputCallback called with succes: " << success;

    UNREFERENCED_PARAMETER( c );
    UNREFERENCED_PARAMETER( userdata );

    if ( !success )
    {
        LOG( ERROR ) << "Error setting source output..";
    }

    loopControl = PulseAudioLoopControl::Stop;
}

void sourceOutputCallback( pa_context* c,
                           const pa_source_output_info* i,
                           int isLast,
                           void* userdata )
{
    UNREFERENCED_PARAMETER( userdata );

    const auto deviceState = getIsLastMeaning( isLast );
    if ( deviceState == PulseAudioIsLastMeaning::PreviousDeviceWasLastReal )
    {
        loopControl = PulseAudioLoopControl::Stop;
        return;
    }
    else if ( deviceState == PulseAudioIsLastMeaning::Error )
    {
        LOG( ERROR ) << "Error in sourceOutputCallback function.";
        dumpPulseAudioState();
        return;
    }

    const auto sourceOutputIndex = i->index;
    const auto sourceIndex = pulseAudioData.currentDefaultSourceInfo.index;
    pa_context_move_source_output_by_index(
        c, sourceIndex, sourceOutputIndex, setSourceOutputCallback, nullptr );
}

void setMicrophoneDevice( const std::string& id )
{
    LOG( DEBUG ) << "setMicrophoneDevice called with 'id': " << id;

    updateAllPulseData();

    pa_context_set_default_source( pulseAudioPointers.context,
                                   id.c_str(),
                                   setMicrophoneCallback,
                                   nullptr );

    customPulseLoop();

    constexpr auto noUserData = nullptr;
    pa_context_get_source_output_info_list(
        pulseAudioPointers.context, sourceOutputCallback, noUserData );

    customPulseLoop();

    LOG( DEBUG ) << "setMicrophoneDevice done.";
}

void setPlaybackVolumeCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "setPlaybackVolumeCallback called with 'success': "
                 << success;

    UNREFERENCED_PARAMETER( c );

    if ( success )
    {
        *static_cast<bool*>( userdata ) = true;
    }
    else
    {
        LOG( ERROR ) << "Error setting playback volume status.";
    }

    loopControl = PulseAudioLoopControl::Stop;

    LOG( DEBUG ) << "setPlaybackVolumeCallback done.";
}

bool setPlaybackVolume( const float volume )
{
    LOG( DEBUG ) << "setPlaybackVolume called with 'volume': " << volume;

    updateAllPulseData();

    auto pulseVolume = pulseAudioData.currentDefaultSinkInfo.volume;
    const auto vol = pa_sw_volume_from_linear( static_cast<double>( volume ) );

    pa_cvolume_set( &pulseVolume, pulseVolume.channels, vol );

    auto success = false;
    pa_context_set_sink_volume_by_name(
        pulseAudioPointers.context,
        pulseAudioData.defaultSinkOutputDeviceId.c_str(),
        &pulseVolume,
        setPlaybackVolumeCallback,
        &success );

    customPulseLoop();

    LOG( DEBUG ) << "setPlaybackVolume done with 'success': " << success;

    return success;
}

void setMicVolumeCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "setMicVolumeCallback called with 'success': " << success;

    UNREFERENCED_PARAMETER( c );

    if ( success )
    {
        *static_cast<bool*>( userdata ) = true;
    }
    else
    {
        LOG( ERROR ) << "Error setting mic volume status.";
    }

    loopControl = PulseAudioLoopControl::Stop;

    LOG( DEBUG ) << "setMicVolumeCallback done.";
}

bool setMicrophoneVolume( const float volume )
{
    LOG( DEBUG ) << "setMicrophoneVolume called with 'volume': " << volume;

    updateAllPulseData();

    auto pulseVolume = pulseAudioData.currentDefaultSourceInfo.volume;
    const auto vol = pa_sw_volume_from_linear( static_cast<double>( volume ) );

    pa_cvolume_set( &pulseVolume, pulseVolume.channels, vol );

    auto success = false;
    pa_context_set_source_volume_by_name(
        pulseAudioPointers.context,
        pulseAudioData.defaultSourceInputDeviceId.c_str(),
        &pulseVolume,
        setMicVolumeCallback,
        &success );

    customPulseLoop();

    LOG( DEBUG ) << "setMicrophoneVolume done with 'success': " << success;

    return success;
}

void micMuteStatusCallback( pa_context* c, int success, void* userdata )
{
    LOG( DEBUG ) << "micMuteStatusCallback called with 'success': " << success;

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

    LOG( DEBUG ) << "micMuteStatusCallback done.";
}

bool setMicMuteState( const bool muted )
{
    LOG( DEBUG ) << "setMicMuteState called with 'muted': " << muted;
    bool success = false;

    pa_context_set_source_mute_by_name(
        pulseAudioPointers.context,
        pulseAudioData.defaultSourceInputDeviceId.c_str(),
        muted,
        micMuteStatusCallback,
        &success );

    customPulseLoop();

    LOG( DEBUG ) << "setMicMuteState done with 'success': " << success;

    return success;
}

void restorePulseAudioState()
{
    LOG( DEBUG ) << "restorePulseAudioState called.";

    setPlaybackDeviceInternal( pulseAudioData.originalDefaultOutputDeviceId );
    setPlaybackVolume( pulseAudioData.originalDefaultOutputDeviceVolume );

    setMicrophoneDevice( pulseAudioData.originalDefaultInputDeviceId );
    setMicrophoneVolume( pulseAudioData.originalDefaultInputDeviceVolume );

    LOG( DEBUG ) << "restorePulseAudioState done.";
}

void initializePulseAudio()
{
    LOG( DEBUG ) << "initializePulseAudio called.";

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

    pulseAudioData.originalDefaultInputDeviceVolume
        = static_cast<float>( pa_sw_volume_to_linear( pa_cvolume_avg(
            &pulseAudioData.currentDefaultSourceInfo.volume ) ) );

    pulseAudioData.originalDefaultOutputDeviceId
        = getCurrentDefaultPlaybackDeviceId();

    pulseAudioData.originalDefaultOutputDeviceVolume
        = static_cast<float>( pa_sw_volume_to_linear(
            pa_cvolume_avg( &pulseAudioData.currentDefaultSinkInfo.volume ) ) );

    LOG( DEBUG ) << "initializePulseAudio finished.";
}
} // namespace advsettings
