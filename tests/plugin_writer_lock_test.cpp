#include <cstdlib>
#include <iostream>

#include <QUuid>

#include "PluginAPI/PluginWriterLock.h"

namespace
{

void Require(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

}

int main()
{
    PluginApi::WriterLock lock;
    const QUuid first = QUuid::createUuid();
    const QUuid second = QUuid::createUuid();

    Require(!lock.IsHeld(), "writer lock starts held");
    Require(!lock.Acquire(QUuid()), "writer lock accepted a null session");
    Require(lock.Acquire(first), "first writer was rejected");
    Require(lock.Acquire(first), "writer re-entry was rejected");
    Require(!lock.Acquire(second), "second writer was accepted");
    lock.Release(second);
    Require(lock.IsHeld(), "foreign release cleared the writer");
    lock.Release(first);
    Require(!lock.IsHeld(), "owner release did not clear the writer");
    Require(lock.Acquire(second), "writer was not reusable after release");
    lock.Clear();
    Require(!lock.IsHeld(), "clear did not release the writer");
    return EXIT_SUCCESS;
}
