#include "SessionStateMachine.h"
#include <QAbstractTransition>
#include <QState>
#include <QHash>
#include <QTimer>
#include <functional>

namespace {
const auto eventType = static_cast<QEvent::Type>(QEvent::registerEventType());
struct StateEvent : QEvent {
    QString target, message;
    StateEvent(QString target, QString message) : QEvent(eventType), target(target), message(message) {}
};
class Transition : public QAbstractTransition {
public:
    QString target;
    std::function<void(const StateEvent &)> apply;
    bool eventTest(QEvent *event) override {
        return event->type() == eventType && static_cast<StateEvent *>(event)->target == target;
    }
    void onTransition(QEvent *event) override { apply(*static_cast<StateEvent *>(event)); }
};
}
SessionStateMachine::SessionStateMachine(QObject *parent) : QObject(parent) {
    // All transport callbacks are serialized onto this machine's event queue.
    const QHash<QString, QStringList> edges{
        {"attached", {"connecting", "error", "disconnecting"}},
        {"connecting", {"downloading_package", "data_lost", "error", "disconnecting"}},
        {"downloading_package", {"verifying_package", "awaiting_trust", "loading_plugin", "data_lost", "error", "disconnecting"}},
        {"verifying_package", {"downloading_package", "awaiting_trust", "loading_plugin", "data_lost", "error", "disconnecting"}},
        {"awaiting_trust", {"loading_plugin", "data_lost", "error", "disconnecting"}},
        {"loading_plugin", {"ready", "data_lost", "error", "disconnecting"}},
        {"ready", {"contact_detached", "data_lost", "error", "disconnecting"}},
        {"contact_detached", {"ready", "data_lost", "error", "disconnecting"}},
        {"data_lost", {"ready", "contact_detached", "downloading_package", "verifying_package", "awaiting_trust", "loading_plugin", "error", "disconnecting"}},
        {"error", {"connecting", "downloading_package", "disconnecting"}},
        {"disconnecting", {"unloading"}}, {"unloading", {"closed"}}, {"closed", {}}
    };
    QHash<QString, QState *> states;
    for (auto it = edges.begin(); it != edges.end(); ++it) states[it.key()] = new QState(&machine_);
    for (auto it = edges.begin(); it != edges.end(); ++it) {
        auto targets = it.value(); targets.append(it.key());
        for (const auto &target : targets) {
            auto *transition = new Transition;
            transition->target = target; transition->setTargetState(states[target]);
            transition->apply = [this](const StateEvent &e) { emit changed(e.target, e.message); };
            states[it.key()]->addTransition(transition);
        }
    }
    machine_.setInitialState(states["attached"]); machine_.start();
}
void SessionStateMachine::request(const QString &state, const QString &message) {
    if (machine_.isRunning()) machine_.postEvent(new StateEvent(state, message));
    else QTimer::singleShot(0, this, [this, state, message] { request(state, message); });
}
