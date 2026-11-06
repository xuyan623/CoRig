#pragma once

// The local endpoint accepts authenticated loopback requests only. Commands
// execute on the owning GUI thread and return jobs for background operations.
#include <QTcpServer>
#include <QVariantMap>

namespace corig {
class Controller;
class ApiServer final : public QTcpServer {
public:
    explicit ApiServer(Controller &controller);
    ~ApiServer() override;

    // Copyable app.connection payload.  The discovery record contains the token
    // but connectionDocument() never reads or returns it.
    QVariantMap connectionInfo() const;
private:
    void publish();
    Controller &controller_;
    QString token_, discovery_, alias_, instanceId_;
    QVariantMap record_;
};
}
