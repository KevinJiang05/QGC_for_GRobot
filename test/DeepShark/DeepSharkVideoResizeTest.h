#pragma once

#include "QmlUITestBase.h"

class DeepSharkVideoResizeTest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    void _threeLocalStreamsRemainResponsiveDuringResize();
};
