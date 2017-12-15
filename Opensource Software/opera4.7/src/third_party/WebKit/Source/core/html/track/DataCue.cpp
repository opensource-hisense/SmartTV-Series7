// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "core/html/track/DataCue.h"

namespace blink {

DataCue::DataCue(ExecutionContext* context, double startTime, double endTime, PassRefPtr<DOMArrayBuffer> data)
    : TextTrackCue(startTime, endTime)
    , ContextLifecycleObserver(context)
{
    setData(data);
}

DataCue::~DataCue()
{
}

#ifndef NDEBUG
String DataCue::toString() const
{
    return String::format("%p id=%s interval=%f-->%f)", this, id().utf8().data(), startTime(), endTime());
}
#endif

PassRefPtr<DOMArrayBuffer> DataCue::data() const
{
    ASSERT(m_data);
    return m_data.get();
}

void DataCue::setData(PassRefPtr<DOMArrayBuffer> data)
{
    m_data = data;
}

ExecutionContext* DataCue::executionContext() const
{
    return ContextLifecycleObserver::executionContext();
}

DEFINE_TRACE(DataCue)
{
    ContextLifecycleObserver::trace(visitor);
    TextTrackCue::trace(visitor);
}

} // namespace blink
