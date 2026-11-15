#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QTimer>
#include <atomic>
#include <ctime>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "Agent/Core/AgentSession.h"
#include "Agent/UI/GuiApprovalGate.h"

using namespace SigilAgent;

namespace {
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}

void sessionCallbacks()
{
    AgentSession session;
    int notifications = 0;
    session.setListener([&](const AgentEvent& event) {
        ++notifications;
        require(!session.events().isEmpty(), "Listener cannot read session events");
        session.remember("callback", true);
        if (event.type == AgentEventType::UserMessage) session.append(AgentEventType::SessionCreated);
    });
    session.append(AgentEventType::UserMessage);
    require(notifications == 2 && session.recall("callback").toBool(), "Nested session notification failed");

    struct Capture {
        AgentSession& session;
        int& released;
        Capture(AgentSession& owner, int& count) : session(owner), released(count) {}
        ~Capture() { session.events(); ++released; }
    };
    int released = 0;
    auto capture = std::make_shared<Capture>(session, released);
    session.setListener([capture](const AgentEvent&) {});
    capture.reset();
    released = 0;
    session.setListener({}); // Destroying the old listener may itself read the session.
    require(released == 1, "Listener capture was not released");

    std::atomic_bool start {false};
    std::atomic_int callbacks {0};
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&, worker]() {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < 300; ++i) {
                if (worker == 0) session.clear();
                else if (worker == 1) {
                    session.setListener([&](const AgentEvent&) { session.events(); ++callbacks; });
                } else if (worker == 2) session.append(AgentEventType::UserMessage);
                else {
                    session.id();
                    session.events();
                    session.remember("iteration", i);
                }
            }
        });
    }
    start = true;
    for (auto& worker : workers) worker.join();
    session.setListener({});
    session.clear();
    const auto events = session.events();
    require(events.size() == 1 && events[0].type == AgentEventType::SessionCreated &&
            events[0].payload.value("session_id").toString() == session.id(), "Clearing session lost its identity event");
}

void approvals(bool testWorkerWait)
{
    AgentCancellation cancellation;
    GuiApprovalGate gate(&cancellation);
    QTimer::singleShot(10, [&]() {
        gate.resolve("stale-call", true);
        gate.resolve("gui-call", true, {{"scope", "selection"}});
    });
    auto result = gate.waitForApproval("gui-call", "", {}, "");
    require(result.approved && result.argumentOverrides.value("scope") == "selection", "GUI approval lost overrides");

    QTimer::singleShot(10, [&]() {
        std::thread resolver([&]() { gate.resolve("thread-call", true); });
        resolver.join();
    });
    require(gate.waitForApproval("thread-call", "", {}, "").approved, "Cross-thread GUI approval failed");

    QTimer::singleShot(10, [&]() { cancellation.request(); });
    require(!gate.waitForApproval("cancel-call", "", {}, "").approved, "Cancellation approved a pending call");
    cancellation.reset();

    if (testWorkerWait) {
        std::atomic_bool finished {false};
        std::thread waiter([&]() {
            result = gate.waitForApproval("worker-call", "", {}, "");
            finished = true;
        });
        // Repeated resolution also covers resolution racing with wait setup.
        while (!finished.load()) {
            gate.resolve("worker-call", true);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        waiter.join();
        require(result.approved, "Worker approval did not wake");
    }

    QElapsedTimer elapsed;
    elapsed.start();
    const auto cpuStart = std::clock();
    QTimer::singleShot(200, [&]() { gate.cancel(); });
    require(!gate.waitForApproval("idle-call", "", {}, "").approved, "Cancelled wait returned approval");
    const auto cpuMs = 1000.0 * (std::clock() - cpuStart) / CLOCKS_PER_SEC;
    std::cout << "Approval idle wait: elapsed=" << elapsed.elapsed() << " ms, CPU=" << cpuMs << " ms\n";
    require(cpuMs < elapsed.elapsed() * 0.2 + 10, "GUI approval busy-spins while idle");
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        sessionCallbacks();
        // A prebuilt, non-instrumented Qt cannot expose QWaitCondition's
        // internal mutex release/reacquire to TSan. Keep the default complete;
        // allow the other paths to be checked separately under that runtime.
        approvals(!app.arguments().contains("--skip-qt-condition-wait"));
        std::cout << "Agent lock tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
