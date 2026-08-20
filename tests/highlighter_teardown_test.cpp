#include <QCoreApplication>
#include <QSignalBlocker>
#include <QSyntaxHighlighter>
#include <QTextDocument>

#include <iostream>

namespace
{

class TestHighlighter : public QSyntaxHighlighter
{
public:
    TestHighlighter()
        : QSyntaxHighlighter(static_cast<QObject*>(nullptr))
    {
    }

protected:
    void highlightBlock(const QString&) override
    {
    }
};

bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << std::endl;
    }
    return condition;
}

}

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QTextDocument document(QStringLiteral("<p>teardown</p>"));
    int content_changes = 0;
    QObject::connect(&document, &QTextDocument::contentsChanged, [&]() {
        ++content_changes;
    });

    TestHighlighter* highlighter = new TestHighlighter;
    highlighter->setDocument(&document);
    QCoreApplication::processEvents();
    content_changes = 0;

    {
        const QSignalBlocker blocker(&document);
        highlighter->setDocument(nullptr);
        delete highlighter;
    }
    QCoreApplication::processEvents();

    return expect(content_changes == 0,
                  "highlighter teardown escaped the document signal blocker")
        ? 0 : 1;
}
