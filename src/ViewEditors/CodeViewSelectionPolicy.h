#pragma once

class QString;
class TagLister;

class CodeViewSelectionPolicy
{
public:
    enum class Reason {
        ElementContent,
        Sentence,
        SentenceFallbackElement,
        OutsideDocument,
        InMarkup,
        UnsafeContainer,
        EmptyContent,
        NoTextUnit,
        NoSentence
    };

    struct Result {
        int start = -1;
        int end = -1;
        int outerStart = -1;
        int outerEnd = -1;
        Reason reason = Reason::NoTextUnit;

        bool hasSelection() const { return start >= 0 && end > start; }
    };

    static Result FindTextUnit(const QString &source, TagLister &tags, int position);
    static Result FindSentence(const QString &source, TagLister &tags, int position);
};
