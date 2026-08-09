/************************************************************************
**
**  Copyright (C) 2026 Sigil-Ebook contributors
**
**  This file is part of Sigil.
**
**  Sigil is free software: you can redistribute it and/or modify
**  it under the terms of the GNU General Public License as published by
**  the Free Software Foundation, either version 3 of the License, or
**  (at your option) any later version.
**
*************************************************************************/

#include "Misc/RegexMatchEnumerator.h"

#define PCRE2_CODE_UNIT_WIDTH 16
#include <pcre2.h>

#include <limits>

namespace RegexSearch
{

namespace
{

QString PcreErrorMessage(int errorCode)
{
    PCRE2_UCHAR16 buffer[256] = {};
    const int length = pcre2_get_error_message_16(errorCode, buffer, 256);
    if (length < 0) {
        return QStringLiteral("PCRE2 error %1").arg(errorCode);
    }
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer), length);
}

MatchError MapRuntimeError(int errorCode)
{
    switch (errorCode) {
        case PCRE2_ERROR_MATCHLIMIT:
            return MatchError::MatchLimit;
        case PCRE2_ERROR_DEPTHLIMIT:
            return MatchError::DepthLimit;
        case PCRE2_ERROR_HEAPLIMIT:
            return MatchError::HeapLimit;
        case PCRE2_ERROR_BADOFFSET:
            return MatchError::BadOffset;
        case PCRE2_ERROR_BADUTFOFFSET:
            return MatchError::BadUtfOffset;
        default:
            return MatchError::InternalError;
    }
}

int AdvanceOneCodePointOrCrLf(const QString& text, int offset)
{
    if (offset >= text.size()) {
        return offset;
    }

    const QChar current = text.at(offset);
    if (current == QLatin1Char('\r') && offset + 1 < text.size() &&
        text.at(offset + 1) == QLatin1Char('\n')) {
        return offset + 2;
    }
    if (current.isHighSurrogate() && offset + 1 < text.size() &&
        text.at(offset + 1).isLowSurrogate()) {
        return offset + 2;
    }
    return offset + 1;
}

}

struct RegexMatchEnumerator::Impl
{
    pcre2_code_16* code = nullptr;
    pcre2_match_data_16* matchData = nullptr;
    pcre2_match_context_16* matchContext = nullptr;
    int captureCount = 0;
    int compileErrorCode = 0;
    int compileErrorOffset = -1;
    QString compileErrorMessage;

    ~Impl()
    {
        if (matchContext != nullptr) {
            pcre2_match_context_free_16(matchContext);
        }
        if (matchData != nullptr) {
            pcre2_match_data_free_16(matchData);
        }
        if (code != nullptr) {
            pcre2_code_free_16(code);
        }
    }
};

RegexMatchEnumerator::RegexMatchEnumerator(const QString& pattern) :
    m_impl(std::make_unique<Impl>())
{
    int errorCode = 0;
    PCRE2_SIZE errorOffset = 0;
    m_impl->code = pcre2_compile_16(
        pattern.utf16(),
        static_cast<PCRE2_SIZE>(pattern.size()),
        PCRE2_UTF | PCRE2_MULTILINE,
        &errorCode,
        &errorOffset,
        nullptr);
    if (m_impl->code == nullptr) {
        m_impl->compileErrorCode = errorCode;
        m_impl->compileErrorOffset = errorOffset > static_cast<PCRE2_SIZE>(std::numeric_limits<int>::max())
                                         ? -1
                                         : static_cast<int>(errorOffset);
        m_impl->compileErrorMessage = PcreErrorMessage(errorCode);
        return;
    }

    m_impl->matchData = pcre2_match_data_create_from_pattern_16(m_impl->code, nullptr);
    m_impl->matchContext = pcre2_match_context_create_16(nullptr);
    if (m_impl->matchData == nullptr || m_impl->matchContext == nullptr) {
        m_impl->compileErrorCode = PCRE2_ERROR_NOMEMORY;
        m_impl->compileErrorMessage = PcreErrorMessage(PCRE2_ERROR_NOMEMORY);
        return;
    }

    const uint32_t ovectorCount = pcre2_get_ovector_count_16(m_impl->matchData);
    m_impl->captureCount = ovectorCount > 0 ? static_cast<int>(ovectorCount - 1) : 0;
}

RegexMatchEnumerator::~RegexMatchEnumerator() = default;

bool RegexMatchEnumerator::isValid() const
{
    return m_impl->code != nullptr && m_impl->matchData != nullptr && m_impl->matchContext != nullptr;
}

int RegexMatchEnumerator::captureGroupCount() const
{
    return isValid() ? m_impl->captureCount : 0;
}

MatchResult RegexMatchEnumerator::enumerate(const QString& text, const MatchOptions& options)
{
    MatchResult result;
    if (!isValid()) {
        result.error = MatchError::InvalidPattern;
        result.nativeErrorCode = m_impl->compileErrorCode;
        result.errorOffset = m_impl->compileErrorOffset;
        result.errorMessage = m_impl->compileErrorMessage;
        return result;
    }

    const int rangeEnd = options.to < 0 ? text.size() : options.to;
    const bool rangeStartsInsideSurrogate = options.from > 0 && options.from < text.size() &&
                                            text.at(options.from).isLowSurrogate() &&
                                            text.at(options.from - 1).isHighSurrogate();
    const bool rangeEndsInsideSurrogate = rangeEnd > 0 && rangeEnd < text.size() &&
                                          text.at(rangeEnd).isLowSurrogate() &&
                                          text.at(rangeEnd - 1).isHighSurrogate();
    if (options.from < 0 || rangeEnd < options.from || rangeEnd > text.size() ||
        rangeStartsInsideSurrogate || rangeEndsInsideSurrogate) {
        result.error = MatchError::InvalidRange;
        result.errorMessage = QStringLiteral("Invalid regex search range [%1, %2) for text length %3")
                                  .arg(options.from)
                                  .arg(rangeEnd)
                                  .arg(text.size());
        return result;
    }
    if (options.matchLimit == 0 || options.depthLimit == 0 || options.heapLimitKiB == 0) {
        result.error = MatchError::InternalError;
        result.errorMessage = QStringLiteral("PCRE2 limits must be greater than zero");
        return result;
    }

    if (pcre2_set_match_limit_16(m_impl->matchContext, options.matchLimit) != 0 ||
        pcre2_set_depth_limit_16(m_impl->matchContext, options.depthLimit) != 0 ||
        pcre2_set_heap_limit_16(m_impl->matchContext, options.heapLimitKiB) != 0) {
        result.error = MatchError::InternalError;
        result.errorMessage = QStringLiteral("Unable to configure PCRE2 match limits");
        return result;
    }

    const QString subject = text.mid(options.from, rangeEnd - options.from);
    PCRE2_SIZE startOffset = 0;
    bool retryNonEmptyAtSameOffset = false;

    while (startOffset <= static_cast<PCRE2_SIZE>(subject.size())) {
        if (options.isCancelled && options.isCancelled()) {
            result.matches.clear();
            result.error = MatchError::Cancelled;
            result.errorMessage = QStringLiteral("Regex match enumeration cancelled");
            return result;
        }

        const uint32_t matchFlags = retryNonEmptyAtSameOffset
                                        ? PCRE2_ANCHORED | PCRE2_NOTEMPTY_ATSTART
                                        : 0;
        const int rc = pcre2_match_16(
            m_impl->code,
            subject.utf16(),
            static_cast<PCRE2_SIZE>(subject.size()),
            startOffset,
            matchFlags,
            m_impl->matchData,
            m_impl->matchContext);

        if (rc == PCRE2_ERROR_NOMATCH) {
            if (!retryNonEmptyAtSameOffset) {
                result.success = true;
                return result;
            }
            if (startOffset >= static_cast<PCRE2_SIZE>(subject.size())) {
                result.success = true;
                return result;
            }
            startOffset = static_cast<PCRE2_SIZE>(
                AdvanceOneCodePointOrCrLf(subject, static_cast<int>(startOffset)));
            retryNonEmptyAtSameOffset = false;
            continue;
        }
        if (rc < 0) {
            result.matches.clear();
            result.error = MapRuntimeError(rc);
            result.nativeErrorCode = rc;
            result.errorMessage = PcreErrorMessage(rc);
            return result;
        }

        PCRE2_SIZE* ovector = pcre2_get_ovector_pointer_16(m_impl->matchData);
        const PCRE2_SIZE matchStart = ovector[0];
        const PCRE2_SIZE matchEnd = ovector[1];
        Match match;
        match.start = options.from + static_cast<int>(matchStart);
        match.end = options.from + static_cast<int>(matchEnd);
        match.captureGroups.reserve(m_impl->captureCount);
        for (int group = 1; group <= m_impl->captureCount; ++group) {
            const PCRE2_SIZE captureStart = ovector[2 * group];
            const PCRE2_SIZE captureEnd = ovector[2 * group + 1];
            Capture capture;
            if (captureStart != PCRE2_UNSET && captureEnd != PCRE2_UNSET) {
                capture.start = options.from + static_cast<int>(captureStart);
                capture.end = options.from + static_cast<int>(captureEnd);
                capture.participated = true;
            }
            match.captureGroups.append(capture);
        }

        const bool isEmpty = matchStart == matchEnd;
        if (!isEmpty || options.allowEmpty) {
            result.matches.append(match);
        }
        startOffset = matchEnd;
        retryNonEmptyAtSameOffset = isEmpty;
    }

    result.success = true;
    return result;
}

}
