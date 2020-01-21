#include "utils/setup.h"
#include "settings/settings.h"
#include "tabcontrollers/audiomanager/AudioManagerPulse.h"

INITIALIZE_EASYLOGGINGPP

int main( int argc, char* argv[] )
{
    setUpLogging();

    advsettings::AudioManagerPulse p;
    p.init(nullptr);
    LOG(INFO) << "MAIN: ";
    LOG( INFO ) << p.getPlaybackDevName();
    LOG( INFO ) << p.getPlaybackDevId();

    LOG( INFO ) << p.getMicDevName();
    LOG( INFO ) << p.getMicDevId();

    LOG( INFO ) << "Playback devices:";
    const auto playback = p.getPlaybackDevices();
    for ( const auto& dev : playback )
    {
        LOG( INFO ) << "\t" << dev.name();
        LOG( INFO ) << "\t" << dev.id();
    }

    LOG( INFO ) << "Recording devices:";
    const auto recording = p.getRecordingDevices();
    for ( const auto& dev : playback )
    {
        LOG( INFO ) << "\t" << dev.name();
        LOG( INFO ) << "\t" << dev.id();
    }

    LOG( INFO ) << "Mic mute status: " << p.getMicMuted();
    LOG( INFO ) << "Set mic mute: " << p.setMicMuted( true );
    LOG( INFO ) << "Mic mute status: " << p.getMicMuted();
    LOG( INFO ) << "Set mic mute: " << p.setMicMuted( false );

    LOG( INFO ) << "MIC:";
    LOG( INFO ) << "Mic vol: " << p.getMicVolume();
    LOG( INFO ) << "set Mic vol: " << p.setMicVolume( 0.5 );
    LOG( INFO ) << "Mic vol: " << p.getMicVolume();
    LOG( INFO ) << "set Mic vol: " << p.setMicVolume( 1.0 );

    exit( 0 );

    LOG( INFO ) << "Settings File: "
                << settings::initializeAndGetSettingsPath();

    LOG( INFO ) << settings::getSettingsAndValues();

    QCoreApplication::setAttribute( Qt::AA_Use96Dpi );
    MyQApplication mainEventLoop( argc, argv );
    mainEventLoop.setOrganizationName(
        application_strings::applicationOrganizationName );
    mainEventLoop.setApplicationName( application_strings::applicationName );
    mainEventLoop.setApplicationDisplayName(
        application_strings::applicationDisplayName );
    mainEventLoop.setApplicationVersion(
        application_strings::applicationVersionString );

    qInstallMessageHandler( mainQtMessageHandler );

    const auto commandLineArgs
        = argument::returnCommandLineParser( mainEventLoop );

    // It is important that either install_manifest or remove_manifest are true,
    // otherwise the handleManifests function will not behave properly.
    if ( commandLineArgs.forceInstallManifest
         || commandLineArgs.forceRemoveManifest )
    {
        manifest::handleManifests( commandLineArgs.forceInstallManifest,
                                   commandLineArgs.forceRemoveManifest );
    }

    openvr_init::initializeOpenVR(
        openvr_init::OpenVrInitializationType::Overlay );

    try
    {
        QQmlEngine qmlEngine;

        advsettings::OverlayController controller( commandLineArgs.desktopMode,
                                                   commandLineArgs.forceNoSound,
                                                   qmlEngine );

        constexpr auto widgetPath = "res/qml/common/mainwidget.qml";
        const auto path = paths::binaryDirectoryFindFile( widgetPath );

        if ( !path.has_value() )
        {
            LOG( ERROR ) << "Unable to find file '" << widgetPath << "'.";
            throw std::runtime_error(
                "Unable to find critical file. See log for more information." );
        }

        const auto url
            = QUrl::fromLocalFile( QString::fromStdString( ( *path ) ) );

        QQmlComponent component( &qmlEngine, url );
        auto errors = component.errors();
        for ( auto& e : errors )
        {
            LOG( ERROR ) << "QML Error: " << e.toString().toStdString()
                         << std::endl;
        }
        auto quickObj = component.create();
        controller.SetWidget( qobject_cast<QQuickItem*>( quickObj ),
                              application_strings::applicationDisplayName,
                              application_strings::applicationKey );

        // Attempts to install the application manifest on all "regular" starts.
        if ( !commandLineArgs.desktopMode && !commandLineArgs.forceNoManifest )
        {
            try
            {
                const auto manifestPath = paths::binaryDirectoryFindFile(
                    manifest::kVRManifestName );
                if ( !manifestPath.has_value() )
                {
                    throw std::runtime_error(
                        "Could not find applications manifest." );
                }
                else
                {
                    manifest::installApplicationManifest( *manifestPath );
                }
            }
            catch ( std::exception& e )
            {
                LOG( ERROR ) << e.what();
            }
        }

        if ( commandLineArgs.desktopMode )
        {
            auto m_pWindow = new QQuickWindow();
            qobject_cast<QQuickItem*>( quickObj )
                ->setParentItem( m_pWindow->contentItem() );
            m_pWindow->setGeometry(
                0,
                0,
                static_cast<int>(
                    qobject_cast<QQuickItem*>( quickObj )->width() ),
                static_cast<int>(
                    qobject_cast<QQuickItem*>( quickObj )->height() ) );
            m_pWindow->show();
        }

        return mainEventLoop.exec();
    }
    catch ( const std::exception& e )
    {
        LOG( FATAL ) << e.what();
        return ReturnErrorCode::GENERAL_FAILURE;
    }
}
