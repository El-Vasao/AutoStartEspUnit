#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <deque>
#include <string>
#include <vector>

#include "Client.h"
#include "HostFakes.h"
#include "LogSink.h"
#include "app/AppPorts.h"
#include "mqtt/MqttClient.h"

namespace {

class FakeClient : public Client {
public:
    bool up = false;
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;

    int connect(IPAddress, uint16_t) override { return connect("", 0); }
    int connect(const char*, uint16_t) override {
        up = true;
        return 1;
    }
    size_t write(uint8_t b) override {
        tx.push_back(b);
        return 1;
    }
    size_t write(const uint8_t* b, size_t n) override {
        tx.insert(tx.end(), b, b + n);
        return n;
    }
    int available() override { return (int)rx.size(); }
    int read() override {
        if (rx.empty()) return -1;
        const uint8_t b = rx.front();
        rx.pop_front();
        return b;
    }
    int read(uint8_t* buf, size_t size) override {
        size_t n = 0;
        while (n < size && !rx.empty()) {
            buf[n++] = rx.front();
            rx.pop_front();
        }
        return (int)n;
    }
    int peek() override { return rx.empty() ? -1 : (int)rx.front(); }
    void flush() override {}
    void stop() override { up = false; }
    uint8_t connected() override { return up ? 1 : 0; }
    operator bool() override { return up; }

    void inject(std::initializer_list<uint8_t> bytes) { rx.insert(rx.end(), bytes.begin(), bytes.end()); }
    void injectStr(const std::string& s) { rx.insert(rx.end(), s.begin(), s.end()); }
};

FakeClient* g_net = nullptr;
MqttClient* g_mqtt = nullptr;
AppPorts g_ports{};
int g_starts = 0;
uint16_t g_pid = 1;

bool startProgram(void*, uint8_t) {
    g_starts++;
    ProgramExecutorTestFlags::set(true, false);
    return true;
}

MqttFsmClient::Budgets budgets() {
    MqttFsmClient::Budgets b;
    b.maxReadBytesPerTick = 512;
    b.maxWriteBytesPerTick = 1024;
    b.maxParseFramesPerTick = 8;
    b.maxMsPerTick = 0;
    return b;
}

bool txHas(const char* needle) {
    const std::string tx(g_net->tx.begin(), g_net->tx.end());
    return tx.find(needle) != std::string::npos;
}

std::string thin(const char* id, unsigned code) {
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"id\":\"%s\",\"code\":%u}", id, code);
    return buf;
}

} // namespace

struct MqttClientTestAccess {
    static void armTopics(MqttClient& m) {
        snprintf(m._topicCmd, sizeof(m._topicCmd), "dev/cmd");
        snprintf(m._topicReply, sizeof(m._topicReply), "dev/reply");
        snprintf(m._topicOnline, sizeof(m._topicOnline), "dev/online");
    }
    static void tick(MqttClient& m) { m._fsm.tick(budgets()); }
    static void drain(MqttClient& m) { m.drainInbound_(); }
    static bool inFlight(MqttClient& m, const char* id) {
        const MqttClient::IdRingEntry* hit = m.findIdRing_(id);
        return hit && hit->inFlight;
    }
    static bool ringUsed(MqttClient& m, const char* id) { return m.findIdRing_(id) != nullptr; }
    static uint16_t ringCode(MqttClient& m, const char* id) {
        const MqttClient::IdRingEntry* hit = m.findIdRing_(id);
        return hit ? hit->code : 0;
    }
    static bool activeIs(const MqttClient& m, const char* id) {
        return m._hasActiveRun && strcmp(m._activeRunId, id) == 0;
    }
    static MqttFsmClient& fsm(MqttClient& m) { return m._fsm; }
};

namespace {

size_t countNeedle(const std::vector<uint8_t>& hay, const char* needle) {
    if (!needle || !needle[0] || hay.empty()) return 0;
    const size_t nlen = strlen(needle);
    size_t hits = 0;
    for (size_t i = 0; i + nlen <= hay.size(); i++) {
        if (memcmp(hay.data() + i, needle, nlen) == 0) hits++;
    }
    return hits;
}

void connectMqtt() {
    MqttFsmClient::Config cfg;
    cfg.host = "broker";
    cfg.port = 1883;
    cfg.clientId = "dev1";
    cfg.keepAliveSec = 30;
    MqttFsmClient& fsm = MqttClientTestAccess::fsm(*g_mqtt);
    fsm.begin(cfg);
    fsm.requestConnect();
    MqttClientTestAccess::tick(*g_mqtt);
    MqttClientTestAccess::tick(*g_mqtt);
    g_net->inject({0x20, 0x02, 0x00, 0x00});
    MqttClientTestAccess::tick(*g_mqtt);
    TEST_ASSERT_TRUE(fsm.isConnected());
}

void ingest(const char* payload) {
    const char* topic = "dev/cmd";
    const uint16_t topicLen = (uint16_t)strlen(topic);
    const uint16_t payloadLen = (uint16_t)strlen(payload);
    const uint16_t rem = (uint16_t)(2 + topicLen + payloadLen);
    TEST_ASSERT_LESS_THAN(128, rem);
    std::string frame;
    frame.push_back((char)0x30);
    frame.push_back((char)rem);
    frame.push_back(0);
    frame.push_back((char)topicLen);
    frame.append(topic, topicLen);
    frame.append(payload, payloadLen);
    g_net->injectStr(frame);
    MqttClientTestAccess::tick(*g_mqtt);
}

void runCmd(const char* id, unsigned program) {
    char json[80];
    snprintf(json, sizeof(json), "{\"id\":\"%s\",\"cmd\":\"run\",\"program\":%u}", id, program);
    ingest(json);
}

void ackReply() {
    const uint8_t frame[] = {0x40, 0x02, (uint8_t)(g_pid >> 8), (uint8_t)(g_pid & 0xFF)};
    g_pid++;
    g_net->inject({frame[0], frame[1], frame[2], frame[3]});
    MqttClientTestAccess::tick(*g_mqtt);
}

void acceptRun(const char* id, unsigned program) {
    runCmd(id, program);
    TEST_ASSERT_TRUE_MESSAGE(txHas(thin(id, MqttCmd::CODE_ACCEPTED).c_str()), id);
    ackReply();
    TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, id));
}

void dispatch() {
    MqttClientTestAccess::drain(*g_mqtt);
    MqttClientTestAccess::tick(*g_mqtt);
}

} // namespace

void setUp() {
    test_clock::set(100000);
    test_log::clear();
    ProgramExecutorTestFlags::set(false, false);
    g_starts = 0;
    g_pid = 1;
    g_net = new FakeClient();
    g_mqtt = new MqttClient(*g_net);
    MqttClientTestAccess::armTopics(*g_mqtt);
    g_ports = AppPorts{};
    g_ports.control.startProgram = startProgram;
    g_mqtt->setAppPorts(&g_ports);
    connectMqtt();
}

void tearDown() {
    delete g_mqtt;
    g_mqtt = nullptr;
    delete g_net;
    g_net = nullptr;
}

static void test_preempt_run_final_reply_uses_old_id() {
    acceptRun("old1", 2);
    dispatch();
    TEST_ASSERT_EQUAL_INT(1, g_starts);
    TEST_ASSERT_TRUE(MqttClientTestAccess::activeIs(*g_mqtt, "old1"));

    acceptRun("new1", 2);
    dispatch();
    TEST_ASSERT_EQUAL_INT(2, g_starts);
    TEST_ASSERT_TRUE(MqttClientTestAccess::activeIs(*g_mqtt, "new1"));
    TEST_ASSERT_FALSE(txHas(thin("old1", MqttCmd::CODE_INTERNAL).c_str()));
    TEST_ASSERT_FALSE(txHas(thin("new1", MqttCmd::CODE_INTERNAL).c_str()));

    g_mqtt->onProgramLifecycle(2, false);
    MqttClientTestAccess::tick(*g_mqtt);

    TEST_ASSERT_TRUE(txHas(thin("old1", MqttCmd::CODE_INTERNAL).c_str()));
    TEST_ASSERT_FALSE(txHas(thin("new1", MqttCmd::CODE_INTERNAL).c_str()));
    TEST_ASSERT_FALSE(MqttClientTestAccess::inFlight(*g_mqtt, "old1"));
    TEST_ASSERT_EQUAL_UINT16(MqttCmd::CODE_INTERNAL, MqttClientTestAccess::ringCode(*g_mqtt, "old1"));
    TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, "new1"));
    TEST_ASSERT_TRUE(MqttClientTestAccess::activeIs(*g_mqtt, "new1"));
}

static void test_failed_stage_ack_releases_id_ring() {
    g_mqtt->setCtrlPlaneBusy([](void*) { return true; }, nullptr);
    for (int i = 0; i < MqttCmd::PENDING_REPLY_DEPTH; i++) {
        char id[8];
        snprintf(id, sizeof(id), "k%d", i);
        runCmd(id, 1);
        TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, id));
    }
    runCmd("kdrop", 1);
    TEST_ASSERT_FALSE(MqttClientTestAccess::ringUsed(*g_mqtt, "kdrop"));
    TEST_ASSERT_FALSE(MqttClientTestAccess::inFlight(*g_mqtt, "kdrop"));
    TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, "k0"));
    TEST_ASSERT_EQUAL_INT(0, g_starts);
}

static void test_full_inbound_releases_id_ring() {
    for (int i = 0; i < MqttCmd::INBOUND_DEPTH; i++) {
        char id[8];
        snprintf(id, sizeof(id), "r%d", i);
        acceptRun(id, 1);
    }
    runCmd("rfull", 1);
    TEST_ASSERT_FALSE(MqttClientTestAccess::ringUsed(*g_mqtt, "rfull"));
    TEST_ASSERT_FALSE(MqttClientTestAccess::inFlight(*g_mqtt, "rfull"));
    TEST_ASSERT_TRUE(txHas(thin("rfull", MqttCmd::CODE_UNAVAILABLE).c_str()));
    TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, "r0"));
    TEST_ASSERT_EQUAL_INT(0, g_starts);
}

static void test_run_during_starter_handoff_preempts() {
    acceptRun("keep", 2);
    dispatch();
    TEST_ASSERT_EQUAL_INT(1, g_starts);
    TEST_ASSERT_TRUE(MqttClientTestAccess::activeIs(*g_mqtt, "keep"));

    // Передача управления разрешена и на шаге стартера (гасится только стартер).
    ProgramExecutorTestFlags::set(true, true);
    acceptRun("late", 2);
    dispatch();

    TEST_ASSERT_EQUAL_INT(2, g_starts);
    TEST_ASSERT_TRUE(MqttClientTestAccess::activeIs(*g_mqtt, "late"));
    TEST_ASSERT_TRUE(MqttClientTestAccess::inFlight(*g_mqtt, "late"));
    TEST_ASSERT_FALSE(txHas(thin("keep", MqttCmd::CODE_CONFLICT).c_str()));
    TEST_ASSERT_FALSE(txHas(thin("late", MqttCmd::CODE_UNPROCESSABLE).c_str()));

    g_mqtt->onProgramLifecycle(2, false);
    MqttClientTestAccess::tick(*g_mqtt);
    TEST_ASSERT_TRUE(txHas(thin("keep", MqttCmd::CODE_INTERNAL).c_str()));
}

static void test_disconnect_publishes_online_false_once() {
    g_mqtt->setReconnectEnabled(false);
    g_net->tx.clear();

    g_mqtt->disconnect();
    g_mqtt->disconnect();
    TEST_ASSERT_TRUE(MqttClientTestAccess::fsm(*g_mqtt).isDisconnectPending());

    for (int i = 0; i < 8; i++) {
        g_mqtt->loop();
        if (MqttClientTestAccess::fsm(*g_mqtt).state() == MqttFsmClient::State::Idle) break;
    }
    TEST_ASSERT_EQUAL_INT((int)MqttFsmClient::State::Idle, (int)MqttClientTestAccess::fsm(*g_mqtt).state());
    TEST_ASSERT_FALSE(MqttClientTestAccess::fsm(*g_mqtt).isDisconnectPending());
    TEST_ASSERT_EQUAL(1u, countNeedle(g_net->tx, "false"));
    TEST_ASSERT_TRUE(txHas("dev/online"));

    const size_t txBefore = g_net->tx.size();
    for (int i = 0; i < 4; i++) g_mqtt->loop();
    TEST_ASSERT_EQUAL(txBefore, g_net->tx.size());
    TEST_ASSERT_EQUAL(1u, countNeedle(g_net->tx, "false"));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_preempt_run_final_reply_uses_old_id);
    RUN_TEST(test_failed_stage_ack_releases_id_ring);
    RUN_TEST(test_full_inbound_releases_id_ring);
    RUN_TEST(test_run_during_starter_handoff_preempts);
    RUN_TEST(test_disconnect_publishes_online_false_once);
    return UNITY_END();
}
