#include <unity.h>

#include <string.h>

#include <string>
#include <vector>

#include "LogSink.h"
#include "Update.h"
#include "fs/FsManager.h"
#include "ota/OtaHandler.h"

// Хвост OTA пишется после Update.end. До успешного streamFinish boot остаётся
// на выполняемом слоте. Сбой пути хвоста не вызывает Update.abort.

namespace {

const uint8_t kFw[] = {0xE9, 0x03, 0x02, 0x01};

void appendU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void appendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

std::vector<uint8_t> firmwarePacket() {
    std::vector<uint8_t> out;
    appendU32(out, static_cast<uint32_t>(sizeof kFw));
    out.insert(out.end(), kFw, kFw + sizeof kFw);
    return out;
}

void appendName(std::vector<uint8_t>& out, const char* name) {
    const uint16_t n = static_cast<uint16_t>(strlen(name));
    appendU16(out, n);
    out.insert(out.end(), name, name + n);
}

void appendFile(std::vector<uint8_t>& out, const char* name, const char* data) {
    appendName(out, name);
    const uint32_t n = static_cast<uint32_t>(strlen(data));
    appendU32(out, n);
    out.insert(out.end(), data, data + n);
}

bool feed(OtaHandler& ota, const std::vector<uint8_t>& pkt) {
    return ota.streamFeed(pkt.data(), pkt.size());
}

bool bootIsRunning() {
    return esp_ota_get_boot_partition() == esp_ota_get_running_partition();
}

bool bootIsNext() {
    return esp_ota_get_boot_partition() == esp_ota_test_next_partition();
}

} // пространство имён

void setUp() {
    ram_fs::reset();
    ota_stub::reset();
    if (fileSystem.isInitialized()) fileSystem.end();
    TEST_ASSERT_TRUE(fileSystem.begin());
    test_log::clear();
    test_clock::set(1000);
}

void tearDown() {}

static void test_tail_rejects_config_and_program() {
    const char* paths[] = {"/config.json", "/programs/1.json"};
    for (const char* path : paths) {
        ram_fs::reset();
        ota_stub::reset();
        test_log::clear();
        auto pkt = firmwarePacket();
        appendName(pkt, path);
        OtaHandler ota;
        TEST_ASSERT_FALSE(feed(ota, pkt));
        TEST_ASSERT_FALSE(ota.streamSucceeded());
        TEST_ASSERT_TRUE(test_log::contains("UI tail path rejected"));
        TEST_ASSERT_TRUE(test_log::contains(path));
        TEST_ASSERT_FALSE(ram_fs::exists(path));
        TEST_ASSERT_FALSE(ram_fs::exists((std::string(path) + ".tmp").c_str()));
    }
}

static void test_tail_accepts_ui_gz_and_schema() {
    auto pkt = firmwarePacket();
    appendFile(pkt, "/index.html.gz", "ui-gz");
    appendFile(pkt, "/validation/settings.schema.json", "schema");
    OtaHandler ota;
    TEST_ASSERT_TRUE(feed(ota, pkt));

    // Хвост уже на томе, сессия Update закрыта (end был внутри прошивки).
    const std::string html = ram_fs::read("/index.html.gz");
    const std::string schema = ram_fs::read("/validation/settings.schema.json");
    TEST_ASSERT_EQUAL_STRING("ui-gz", html.c_str());
    TEST_ASSERT_EQUAL_STRING("schema", schema.c_str());
    TEST_ASSERT_FALSE(ram_fs::exists("/index.html.gz.tmp"));
    TEST_ASSERT_FALSE(ram_fs::exists("/index.html.gz.bak"));
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());

    TEST_ASSERT_TRUE(ota.streamFinish());
    TEST_ASSERT_TRUE(ota.streamSucceeded());
}

static void test_boot_stays_running_until_stream_finish() {
    auto pkt = firmwarePacket();
    appendFile(pkt, "/index.html.gz", "ui-gz");
    OtaHandler ota;
    TEST_ASSERT_TRUE(feed(ota, pkt));
    TEST_ASSERT_EQUAL_INT(1, ota_stub::beginCount());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());
    TEST_ASSERT_FALSE(ota.streamSucceeded());
    // end уже сдвинул boot на новый слот, pin вернул выполняемый. Finish ещё не звали.
    TEST_ASSERT_TRUE(bootIsRunning());
    TEST_ASSERT_FALSE(bootIsNext());
}

static void test_stream_finish_activates_new_boot() {
    OtaHandler ota;
    TEST_ASSERT_TRUE(feed(ota, firmwarePacket()));
    TEST_ASSERT_TRUE(bootIsRunning());
    TEST_ASSERT_TRUE(ota.streamFinish());
    TEST_ASSERT_TRUE(ota.streamSucceeded());
    TEST_ASSERT_TRUE(bootIsNext());
    TEST_ASSERT_FALSE(bootIsRunning());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());
}

static void test_tail_failure_after_end_does_not_abort() {
    auto pkt = firmwarePacket();
    appendName(pkt, "/config.json");
    OtaHandler ota;
    TEST_ASSERT_FALSE(feed(ota, pkt));
    TEST_ASSERT_EQUAL_INT(1, ota_stub::beginCount());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());
    TEST_ASSERT_TRUE(bootIsRunning());
    TEST_ASSERT_FALSE(ota.streamFinish());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());
    TEST_ASSERT_TRUE(bootIsRunning());
    TEST_ASSERT_FALSE(ota.streamSucceeded());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tail_rejects_config_and_program);
    RUN_TEST(test_tail_accepts_ui_gz_and_schema);
    RUN_TEST(test_boot_stays_running_until_stream_finish);
    RUN_TEST(test_stream_finish_activates_new_boot);
    RUN_TEST(test_tail_failure_after_end_does_not_abort);
    return UNITY_END();
}
