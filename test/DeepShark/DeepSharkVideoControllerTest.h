#pragma once

#include "UnitTest.h"

class DeepSharkVideoControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _rtspTransportPreservesCameraUrls();
    void _videoPanelSavesTransportSelection();
    void _pendingAutoStartIsCancelled();
    void _clearingUriCancelsPendingAutoStart();
    void _clearingActiveReceiverUriStopsPipeline();
    void _duplicateDecoderPadsReleaseParentReference();
    void _videoSinkSizeUsesDecodedCaps();
    void _failureSignalIsIndependentFromDisplayText();
    void _restartRebuildsVideoSink();
    void _failedRestartCanScheduleAgain();
    void _startedRestartCanScheduleAgain();
    void _flyViewStatusQmlLoads();
    void _videoRowsUpdateWithoutReplacingDelegates();
    void _disabledVideoResourcesCanBeRecreated();
    void _coreOptionsSurviveGarbageCollection();
};
