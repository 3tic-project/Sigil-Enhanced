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

#pragma once
#ifndef REGEX_MATCH_ENUMERATOR_H
#define REGEX_MATCH_ENUMERATOR_H

#include <cstdint>
#include <functional>
#include <memory>

#include <QList>
#include <QString>

namespace RegexSearch
{

enum class MatchError {
    None,
    InvalidPattern,
    InvalidRange,
    Cancelled,
    MatchLimit,
    DepthLimit,
    HeapLimit,
    BadOffset,
    BadUtfOffset,
    InternalError
};

struct Capture
{
    int start = -1;
    int end = -1;
    bool participated = false;
};

struct Match
{
    int start = -1;
    int end = -1;
    // Index 0 represents capture group 1. The full match is start/end above.
    QList<Capture> captureGroups;
};

struct MatchOptions
{
    bool allowEmpty = false;
    int from = 0;
    int to = -1;
    uint32_t matchLimit = 1000000;
    uint32_t depthLimit = 10000;
    uint32_t heapLimitKiB = 32 * 1024;
    int maxMatches = -1;
    std::function<bool()> isCancelled;
};

struct MatchResult
{
    bool success = false;
    QList<Match> matches;
    MatchError error = MatchError::None;
    int nativeErrorCode = 0;
    int errorOffset = -1;
    QString errorMessage;
};

class RegexMatchEnumerator final
{
public:
    explicit RegexMatchEnumerator(const QString& pattern);
    ~RegexMatchEnumerator();

    RegexMatchEnumerator(const RegexMatchEnumerator&) = delete;
    RegexMatchEnumerator& operator=(const RegexMatchEnumerator&) = delete;

    bool isValid() const;
    int captureGroupCount() const;
    MatchResult enumerate(const QString& text, const MatchOptions& options = MatchOptions());

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}

#endif // REGEX_MATCH_ENUMERATOR_H
