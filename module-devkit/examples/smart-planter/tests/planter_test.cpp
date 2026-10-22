#include "History.h"
#include "pi_module_api.h"
#include "firmware/components/power_guard/include/power_policy.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDateTime>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
static QJsonObject event;
static QByteArray directory;
static int emitted;
static const char *dataDirectory() { return directory.constData(); }
static void emitEvent(void *, const char *, const char *json) { event = QJsonDocument::fromJson(json).object(); ++emitted; }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); QTemporaryDir tmp; CHECK(tmp.isValid());
    const qint64 now = 1800000000000LL;
    const auto file = tmp.path() + "/history.json";
    {
        History h(file); h.add(now, 20); h.add(now + 1000, 24); h.add(now + 60 * 60000, 26);
        h.add(now + 60 * 60000, NAN);
        auto a = h.series(now + 60 * 60000, History::week, 15 * History::minute);
        CHECK(a.size() == 2); CHECK(a[0].toObject()["v"].toDouble() == 22);
        CHECK(a[0].toObject()["min"].toDouble() == 20); CHECK(h.save());
    }
    History reopened(file); CHECK(reopened.series(now + 60 * 60000, History::week, 15 * History::minute).size() == 2);
    reopened.prune(now + 2 * History::week); CHECK(reopened.series(now + 2 * History::week, History::week, History::minute).isEmpty());
    QFile broken(tmp.path() + "/broken"); CHECK(broken.open(QIODevice::WriteOnly)); broken.write("not JSON"); broken.close();
    History corrupt(broken.fileName()); CHECK(!corrupt.error.isEmpty());
    directory = tmp.path().toUtf8(); pi_host_services_t host{};
    host.api_version = 1; host.emit_event = emitEvent; host.data_directory = dataDirectory;
    auto m = pi_module_create(&host); CHECK(m);
    CHECK(pi_module_on_attach(m, "{\"module_instance\":\"test-device\"}") == 0);
    const QByteArray payload("{\"temperature_C\":24,\"battery_pct\":null}");
    CHECK(pi_module_on_data(m, 2, reinterpret_cast<const uint8_t *>(payload.constData()), payload.size()) == 0);
    CHECK(emitted == 1); CHECK(event["battery_pct"].isNull()); CHECK(event["history_day"].toArray().size() == 1);
    pi_module_stop(m);
    CHECK(pi_module_on_data(m, 2, reinterpret_cast<const uint8_t *>(payload.constData()), payload.size()) == -1);
    CHECK(emitted == 1); pi_module_destroy(m);
    m = pi_module_create(&host); CHECK(pi_module_on_attach(m, "{\"module_instance\":\"test-device\"}") == 0);
    CHECK(pi_module_on_data(m, 2, reinterpret_cast<const uint8_t *>(payload.constData()), payload.size()) == 0);
    CHECK(event["history_week"].toArray().size() == 1); pi_module_destroy(m);
    power_policy_t policy{};
    CHECK(!power_policy_update(&policy, -1, true, 3.2f, 1000000));
    CHECK(!power_policy_update(&policy, 1, true, 3.2f, 100000000));
    CHECK(!power_policy_update(&policy, 0, true, 3.2f, 1000000));
    CHECK(!power_policy_update(&policy, 0, true, 3.2f, 20000000));
    CHECK(power_policy_update(&policy, 0, true, 3.2f, 21000000));
    CHECK(!power_policy_update(&policy, 1, true, 3.2f, 22000000));
    CHECK(!policy.low_since_us);
    CHECK(!power_policy_update(&policy, 0, false, 3.2f, 30000000));
    CHECK(!power_policy_update(&policy, 0, true, 0.0f, 31000000));
    CHECK(!power_policy_update(&policy, 0, true, 3.5f, 32000000));
    puts("PASS: history persistence/gaps/retention/nulls/ABI-stop/power policy");
}
