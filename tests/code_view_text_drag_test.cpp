#include <iostream>
#include <stdexcept>

#include <QString>

#include "ViewEditors/CodeViewTextDrag.h"

namespace
{

void require(bool condition, const char *message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void expect(const QString &text, int start, int end, int drop, const QString &expected, int selection_start)
{
    const CodeViewTextDrag::Result result = CodeViewTextDrag::Apply(text, start, end, drop);
    require(result.changed && result.text == expected, "Moved text does not match");
    require(result.selectionStart == selection_start
                && result.selectionEnd == selection_start + (end - start),
            "Moved selection was not placed at the drop");
    require(CodeViewTextDrag::Apply(result.text, result.selectionStart, result.selectionEnd,
                                    start > drop ? start + (end - start) : start).text == text,
            "Moving the text back did not restore the original");
}

}

int main()
{
    try {
        const QString text = QStringLiteral("alpha beta gamma delta");
        const int beta = text.indexOf(QLatin1String("beta"));
        const int delta = text.indexOf(QLatin1String("delta"));
        expect(text, beta, delta, 0, QStringLiteral("beta gamma alpha delta"), 0);
        expect(text, beta, delta, text.size(), QStringLiteral("alpha deltabeta gamma "),
               QStringLiteral("alpha delta").size());

        const QString lines = QStringLiteral("one\ntwo\nthree");
        expect(lines, 0, 4, lines.size(), QStringLiteral("two\nthreeone\n"), lines.size() - 4);

        const CodeViewTextDrag::Result inside = CodeViewTextDrag::Apply(text, beta, delta, beta + 1);
        require(!inside.changed && inside.text == text, "A drop inside the selection must not move text");
        const CodeViewTextDrag::Result invalid = CodeViewTextDrag::Apply(text, -1, delta, 0);
        require(!invalid.changed && invalid.text == text, "An invalid range must not move text");
        require(CodeViewTextDrag::MimeType() == QLatin1String("application/x-sigil-codeview-text"),
                "The internal drag payload type changed");

        std::cout << "Code view text drag checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
