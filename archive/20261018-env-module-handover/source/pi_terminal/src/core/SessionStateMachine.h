#pragma once
#include <QStateMachine>
#include <QString>

class SessionStateMachine : public QObject {
    Q_OBJECT
public:
    explicit SessionStateMachine(QObject *parent = nullptr);
    void request(const QString &state, const QString &message);
signals:
    void changed(QString state, QString message);
private:
    QStateMachine machine_;
};
