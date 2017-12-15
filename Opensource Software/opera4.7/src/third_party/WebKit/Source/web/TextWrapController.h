// Copyright 2015 Opera TV AS. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TextWrapController_h
#define TextWrapController_h

#include "core/editing/VisiblePosition.h"
#include "platform/heap/Handle.h"
#include "wtf/Forward.h"

namespace blink {

class Node;

enum TextWrapTransitionState {
    NotWrapping,
    WaitForStart,
    WaitForLayout,
    WaitForScrollCompensation,
};

class TextWrapController final: public NoBaseWillBeGarbageCollectedFinalized<TextWrapController> {

public:
    static PassOwnPtrWillBeRawPtr<TextWrapController> create()
    {
        return adoptPtrWillBeNoop(new TextWrapController);
    }

    TextWrapTransitionState transitionState() const { return m_transitionState; }
    void advanceState(TextWrapTransitionState newState) { m_transitionState = newState; }

    Node* targetBlock() const { return m_targetBlock.get(); }
    void setTargetBlock(Node* newTarget) { m_targetBlock = newTarget; }

    VisiblePosition& position() { return m_position; }
    void setPosition(const VisiblePosition& position) { m_position = position; }

    int targetY() const { return m_targetY; }
    void setTargetY(int newY) { m_targetY = newY; }

    bool targetIsText() const { return m_targetIsText; }
    void setTargetIsText(bool isText) { m_targetIsText = isText; }

    FloatSize scrollDelta() const { return m_scrollDelta; }
    void setScrollDelta(FloatSize newSize) { m_scrollDelta = newSize; }

    IntRect computeTextBounds();
    IntRect computePositionBounds();
    IntPoint adjustNextInterestPointForTextWrapScrollCompensation(const IntPoint&);

    void reset();

    DECLARE_TRACE();

private:
    TextWrapController();

    TextWrapTransitionState m_transitionState;
    RefPtrWillBeMember<Node> m_targetBlock;
    VisiblePosition m_position;
    int m_targetY;
    bool m_targetIsText;
    FloatSize m_scrollDelta;
};

} // namespace blink

#endif // TextWrapController_h
