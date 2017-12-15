// Copyright 2015 Opera TV AS. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "web/TextWrapController.h"

#include "core/dom/Range.h"
#include "core/editing/RenderedPosition.h"
#include "core/layout/LayoutObject.h"
#include "public/platform/WebFloatSize.h"

namespace blink {

TextWrapController::TextWrapController()
    : m_transitionState(NotWrapping)
    , m_targetY(-1)
    , m_targetIsText(false)
{
}

void TextWrapController::reset()
{
    advanceState(NotWrapping);
    m_targetBlock = nullptr;
    m_position = VisiblePosition();
    m_scrollDelta = WebFloatSize();

}

DEFINE_TRACE(TextWrapController)
{
    visitor->trace(m_targetBlock);
    visitor->trace(m_position);
}

IntRect TextWrapController::computeTextBounds()
{
    if (m_targetBlock) {
        Vector<IntRect> textRects;
        RefPtrWillBeRawPtr<Range> range = Range::create(m_targetBlock->document());
        range->selectNodeContents(m_targetBlock.get(), IGNORE_EXCEPTION);
        range->textRects(textRects);
        IntRect rect;
        for (size_t i = 0; i < textRects.size(); ++i)
            rect.unite(textRects[i]);
        return rect;
    }
    return IntRect();
}

IntRect TextWrapController::computePositionBounds()
{
    // Given a position in the document, compute the bounds of a
    // suitable area that we can relate to before and after wrapping.

    IntRect rect;
    if (m_position.isNotNull()) {
        if (Node* node = m_position.deepEquivalent().anchorNode()) {
            if (node->isTextNode()) {
                rect = RenderedPosition(m_position).absoluteRect();
            } else if (LayoutObject* renderer = node->layoutObject()) {
                rect = renderer->absoluteBoundingBoxRect();
            }
        }
    }
    return rect;
}

IntPoint TextWrapController::adjustNextInterestPointForTextWrapScrollCompensation(const IntPoint& interestPoint)
{
    IntPoint adjustedInterestPoint = interestPoint;
    if (transitionState() == WaitForScrollCompensation) {
        adjustedInterestPoint += IntSize(scrollDelta().width(), scrollDelta().height());
        setScrollDelta(WebFloatSize());
    }
    return adjustedInterestPoint;
}

} // namespace blink
