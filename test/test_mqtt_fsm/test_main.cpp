#include <unity.h>

#include <deque>
#include <string>
#include <vector>

#include "Client.h"
#include "LogSink.h"
#include "mqtt/MqttFsmClient.h"

namespace {

class FakeClient : public Client {
public:
    bool up = false;
    bool connectSucceeds = true;
    int connectCalls = 0;
    int stops = 0;
    std::vector<uint8_t> tx;
    std::deque<uint8_t> rx;

    int connect(IPAddress, uint16_t) override { return connect("", 0); }
    int connect(const char*, uint16_t) override {
        connectCalls++;
        if (connectSucceeds) up = true;
        return up ? 1 : 0;
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
    int peek() override { return rx.empty() ? -1 : rx.front(); }
    void flush() override {}
    void stop() override {
        up = false;
        stops++;
    }
    uint8_t connected() override { return up ? 1 : 0; }
    operator bool() override { return up; }

    void inject(std::initializer_list<uint8_t> bytes) { rx.insert(rx.end(), bytes.begin(), bytes.end()); }
    void injectStr(const std::string& s) { rx.insert(rx.end(), s.begin(), s.end()); }
};

FakeClient* g_net = nullptr;
MqttFsmClient* g_fsm = nullptr;
std::string g_lastTopic;
std::string g_lastPayload;
int g_publishes = 0;

void onPublish(void*, const char* topic, const uint8_t* payload, uint16_t len, bool) {
    g_lastTopic = topic;
    g_lastPayload.assign(reinterpret_cast<const char*>(payload), len);
    g_publishes++;
}

MqttFsmClient::Budgets budgets() {
    MqttFsmClient::Budgets b;
    b.maxReadBytesPerTick = 512;
    b.maxWriteBytesPerTick = 1024;
    b.maxParseFramesPerTick = 8;
    b.maxMsPerTick = 0;
    return b;
}

void tick(int n = 1) {
    for (int i = 0; i < n; i++) g_fsm->tick(budgets());
}

MqttFsmClient::Config config() {
    MqttFsmClient::Config c;
    c.host = "broker";
    c.port = 1883;
    c.clientId = "dev1";
    c.keepAliveSec = 30;
    return c;
}

void connectAndAck() {
    g_fsm->begin(config());
    g_fsm->requestConnect();
    tick(2);
    g_net->inject({0x20, 0x02, 0x00, 0x00});
    tick();
}

bool txContains(const std::vector<uint8_t>& needle) {
    const auto& tx = g_net->tx;
    if (needle.empty() || tx.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= tx.size(); i++) {
        if (std::equal(needle.begin(), needle.end(), tx.begin() + (long)i)) return true;
    }
    return false;
}

size_t bigEncoder(Print& p, void*) {
    std::string big(2000, 'x');
    return p.write(reinterpret_cast<const uint8_t*>(big.data()), big.size());
}

size_t smallEncoder(Print& p, void*) { return p.write(reinterpret_cast<const uint8_t*>("{\"a\":1}"), 7); }

} // namespace

void setUp() {
    test_clock::set(100000);
    test_log::clear();
    g_net = new FakeClient();
    g_fsm = new MqttFsmClient(*g_net);
    g_fsm->setPublishCallback(onPublish, nullptr);
    g_lastTopic.clear();
    g_lastPayload.clear();
    g_publishes = 0;
}

void tearDown() {
    delete g_fsm;
    delete g_net;
}

static void test_connect_sends_connect_and_reaches_connected_on_connack() {
    g_fsm->begin(config());
    g_fsm->requestConnect();
    tick(2);
    TEST_ASSERT_EQUAL_INT(1, g_net->connectCalls);
    TEST_ASSERT_FALSE(g_net->tx.empty());
    TEST_ASSERT_EQUAL_HEX8(0x10, g_net->tx[0]);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MqttFsmClient::State::MqttConnecting, (uint8_t)g_fsm->state());

    g_net->inject({0x20, 0x02, 0x00, 0x00});
    tick();
    TEST_ASSERT_TRUE(g_fsm->isConnected());
}

static void test_connack_refused_goes_to_error() {
    g_fsm->begin(config());
    g_fsm->requestConnect();
    tick(2);
    g_net->inject({0x20, 0x02, 0x00, 0x05});
    tick();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MqttFsmClient::State::Error, (uint8_t)g_fsm->state());
    TEST_ASSERT_EQUAL_STRING("connack_refused", g_fsm->lastErrorReason());
}

static void test_subscribe_and_suback_confirm() {
    connectAndAck();
    g_net->tx.clear();
    TEST_ASSERT_TRUE(g_fsm->subscribe("car/cmd"));
    tick();
    TEST_ASSERT_TRUE(txContains({0x82, 0x0C, 0x00, 0x01, 0x00, 0x07, 'c', 'a', 'r', '/', 'c', 'm', 'd', 0x00}));
    TEST_ASSERT_TRUE(g_fsm->isSubscribePending());
    g_net->inject({0x90, 0x03, 0x00, 0x01, 0x00});
    tick();
    TEST_ASSERT_TRUE(g_fsm->isSubscribeConfirmed());
    TEST_ASSERT_FALSE(g_fsm->isSubscribePending());
}

static void test_suback_failure_requeues_subscribe() {
    connectAndAck();
    g_fsm->subscribe("car/cmd");
    tick();
    g_net->inject({0x90, 0x03, 0x00, 0x01, 0x80});
    tick();
    TEST_ASSERT_FALSE(g_fsm->isSubscribeConfirmed());
    TEST_ASSERT_TRUE(g_fsm->isSubscribePending());
}

static void test_incoming_publish_reaches_callback() {
    connectAndAck();
    std::string frame;
    const std::string topic = "car/cmd";
    const std::string payload = "{\"cmd\":\"status\"}";
    frame.push_back((char)0x30);
    frame.push_back((char)(2 + topic.size() + payload.size()));
    frame.push_back(0x00);
    frame.push_back((char)topic.size());
    frame += topic + payload;
    g_net->injectStr(frame);
    tick();
    TEST_ASSERT_EQUAL_INT(1, g_publishes);
    TEST_ASSERT_EQUAL_STRING("car/cmd", g_lastTopic.c_str());
    TEST_ASSERT_EQUAL_STRING("{\"cmd\":\"status\"}", g_lastPayload.c_str());
}

static void test_split_frame_is_assembled_across_ticks() {
    connectAndAck();
    g_net->inject({0x30, 0x05, 0x00, 0x01});
    tick();
    TEST_ASSERT_EQUAL_INT(0, g_publishes);
    g_net->inject({'t', 'h', 'i'});
    tick();
    TEST_ASSERT_EQUAL_INT(1, g_publishes);
    TEST_ASSERT_EQUAL_STRING("hi", g_lastPayload.c_str());
}

static void test_leading_junk_is_skipped_without_disconnect() {
    connectAndAck();
    g_net->inject({0x00, 0xD0, 0x00});  // junk byte then PINGRESP
    tick();
    TEST_ASSERT_TRUE(g_fsm->isConnected());
}

static void test_pingreq_after_half_keepalive() {
    connectAndAck();
    g_net->tx.clear();
    test_clock::advance(14000);
    tick();
    TEST_ASSERT_FALSE(txContains({0xC0, 0x00}));
    test_clock::advance(2000);
    tick();
    TEST_ASSERT_TRUE(txContains({0xC0, 0x00}));
}

static void test_keepalive_deadman_without_rx() {
    connectAndAck();
    test_clock::advance(46000);  // > 1.5 x 30 s without any inbound frame
    tick();
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MqttFsmClient::State::Error, (uint8_t)g_fsm->state());
    TEST_ASSERT_EQUAL_STRING("keepalive_timeout", g_fsm->lastErrorReason());
}

static void test_server_disconnect_is_error() {
    connectAndAck();
    g_net->inject({0xE0, 0x00});
    tick();
    TEST_ASSERT_EQUAL_STRING("server_disconnect", g_fsm->lastErrorReason());
}

static void test_publish_writes_frame() {
    connectAndAck();
    g_net->tx.clear();
    const uint8_t payload[] = {'o', 'n'};
    TEST_ASSERT_TRUE(g_fsm->publish("t/a", payload, 2, true, true));
    tick();
    TEST_ASSERT_TRUE(txContains({0x31, 0x07, 0x00, 0x03, 't', '/', 'a', 'o', 'n'}));
}

static void test_failed_coalesced_status_does_not_corrupt_queue() {
    connectAndAck();
    g_net->tx.clear();
    // Queue one Tele status without ticking, then coalesce an oversized one into the same slot.
    TEST_ASSERT_TRUE(g_fsm->publishPrintedMeasured("t/status", smallEncoder, nullptr, 900, false, false));
    TEST_ASSERT_FALSE(g_fsm->publishPrintedMeasured("t/status", bigEncoder, nullptr, 900, false, false));
    tick();
    // The queued slot is neutralized into a PINGREQ instead of a half-overwritten PUBLISH.
    TEST_ASSERT_TRUE(txContains({0xC0, 0x00}));
    TEST_ASSERT_FALSE(txContains({'x', 'x', 'x', 'x'}));
    TEST_ASSERT_TRUE(g_fsm->isConnected());
}

static void test_requested_disconnect_sends_disconnect_and_stops() {
    connectAndAck();
    g_net->tx.clear();
    g_fsm->requestDisconnect();
    tick();
    TEST_ASSERT_TRUE(txContains({0xE0, 0x00}));
    TEST_ASSERT_EQUAL_UINT8((uint8_t)MqttFsmClient::State::Idle, (uint8_t)g_fsm->state());
    TEST_ASSERT_TRUE(g_net->stops > 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_connect_sends_connect_and_reaches_connected_on_connack);
    RUN_TEST(test_connack_refused_goes_to_error);
    RUN_TEST(test_subscribe_and_suback_confirm);
    RUN_TEST(test_suback_failure_requeues_subscribe);
    RUN_TEST(test_incoming_publish_reaches_callback);
    RUN_TEST(test_split_frame_is_assembled_across_ticks);
    RUN_TEST(test_leading_junk_is_skipped_without_disconnect);
    RUN_TEST(test_pingreq_after_half_keepalive);
    RUN_TEST(test_keepalive_deadman_without_rx);
    RUN_TEST(test_server_disconnect_is_error);
    RUN_TEST(test_publish_writes_frame);
    RUN_TEST(test_failed_coalesced_status_does_not_corrupt_queue);
    RUN_TEST(test_requested_disconnect_sends_disconnect_and_stops);
    return UNITY_END();
}
