#include <unity.h>

#include <string.h>

#include <string>
#include <vector>

#if defined(_GLIBCXX_HAS_GTHREADS) || defined(_LIBCPP_HAS_THREADS)
#define OTA_TEST_HAS_THREADS 1
#include <atomic>
#include <thread>
#endif

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

void runUpdateUntilDone(OtaHandler& ota) {
    for (int i = 0; i < 100000 && ota.isOngoing(); ++i) ota.update();
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

static void test_ring_stall_times_out_and_fails_session() {
    OtaHandler ota;
    ota.prepareHttpUploadSession();
    std::vector<uint8_t> full(Ota::HTTP_RING_SIZE, 0xAB);
    TEST_ASSERT_TRUE(ota.enqueueHttpBytes(full.data(), full.size()));  // кольцо полное, цикл не сливает
    const uint8_t extra[] = {0x01};
    const unsigned long t0 = millis();
    TEST_ASSERT_FALSE(ota.enqueueHttpBytes(extra, sizeof extra));
    TEST_ASSERT_TRUE(millis() - t0 > Timing::OTA_RING_WAIT_MS);
    TEST_ASSERT_TRUE(test_log::contains("HTTP ring stalled"));
    // Сессия Failed: повтор отказывает сразу, без нового ожидания.
    const unsigned long t1 = millis();
    TEST_ASSERT_FALSE(ota.enqueueHttpBytes(extra, sizeof extra));
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(t1), static_cast<uint32_t>(millis()));
}

static void test_prepare_discards_stale_ring_bytes() {
    OtaHandler ota;
    ota.prepareHttpUploadSession();
    const uint8_t junk[] = {0xDE, 0xAD, 0xBE, 0xEF};
    TEST_ASSERT_TRUE(ota.enqueueHttpBytes(junk, sizeof junk));  // хвост прошлой сессии
    ota.prepareHttpUploadSession();                             // новая сессия выбрасывает его
    const auto pkt = firmwarePacket();
    TEST_ASSERT_TRUE(ota.enqueueHttpBytes(pkt.data(), pkt.size()));  // пришло до begin: должно сохраниться
    ota.markHttpFinal();
    ota.notifyHttpUploadComplete(true);
    ota.begin();
    runUpdateUntilDone(ota);
    TEST_ASSERT_TRUE(ota.streamSucceeded());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::beginCount());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(0, ota_stub::abortCount());
}

static void test_begin_resets_parser_after_failed_session() {
    OtaHandler ota;
    // Первая сессия: fwSize=4, пришли два байта прошивки и финал. Поток уходит в Failed.
    ota.prepareHttpUploadSession();
    const uint8_t cut[] = {4, 0, 0, 0, 0xE9, 0x03};
    TEST_ASSERT_TRUE(ota.enqueueHttpBytes(cut, sizeof cut));
    ota.markHttpFinal();
    ota.notifyHttpUploadComplete(true);
    ota.begin();
    runUpdateUntilDone(ota);
    TEST_ASSERT_FALSE(ota.streamSucceeded());
    TEST_ASSERT_TRUE(test_log::contains("прошивка неполная"));
    // Вторая сессия без onModeExit: парсер должен начаться с нуля, а не с остатков первой.
    ota.prepareHttpUploadSession();
    const auto pkt = firmwarePacket();
    TEST_ASSERT_TRUE(ota.enqueueHttpBytes(pkt.data(), pkt.size()));
    ota.markHttpFinal();
    ota.notifyHttpUploadComplete(true);
    ota.begin();
    runUpdateUntilDone(ota);
    TEST_ASSERT_TRUE(ota.streamSucceeded());
    TEST_ASSERT_EQUAL_INT(2, ota_stub::beginCount());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::endCount());
    TEST_ASSERT_EQUAL_INT(1, ota_stub::abortCount());
}

#if defined(OTA_TEST_HAS_THREADS)
// Producer (как async HTTP) и consumer (цикл) гоняют 256 КБ через кольцо. Часы в delay() не двигаем:
// лимит ожидания кольца не должен зависеть от планировщика хоста.
static void test_ring_stress_producer_consumer_keeps_all_bytes() {
    std::vector<uint8_t> fw(256 * 1024);
    uint32_t expectCheck = 0;
    for (size_t i = 0; i < fw.size(); ++i) {
        fw[i] = static_cast<uint8_t>((i * 131u) ^ (i >> 8));
        expectCheck = expectCheck * 131u + fw[i];
    }
    std::vector<uint8_t> pkt;
    appendU32(pkt, static_cast<uint32_t>(fw.size()));
    pkt.insert(pkt.end(), fw.begin(), fw.end());
    appendU16(pkt, 0);  // nameLen 0: хвост пуст

    OtaHandler ota;
    ota.prepareHttpUploadSession();
    ota.begin();
    delayAdvancesTestClock() = false;

    std::atomic<bool> enqueueOk{true};
    std::thread producer([&] {
        size_t off = 0;
        uint32_t seed = 12345u;
        while (off < pkt.size()) {
            seed = seed * 1103515245u + 12345u;
            size_t n = 1 + (seed >> 16) % 300;
            if (n > pkt.size() - off) n = pkt.size() - off;
            if (!ota.enqueueHttpBytes(pkt.data() + off, n)) {
                enqueueOk = false;
                break;
            }
            off += n;
        }
        ota.markHttpFinal();
        ota.notifyHttpUploadComplete(enqueueOk.load());
    });
    for (uint64_t i = 0; i < 50000000ull && ota.isOngoing(); ++i) {
        ota.update();
        std::this_thread::yield();
    }
    if (ota.isOngoing()) ota.streamAbort();  // лимит итераций сорван: освободить producer
    producer.join();
    delayAdvancesTestClock() = true;

    TEST_ASSERT_TRUE(enqueueOk.load());
    TEST_ASSERT_TRUE(ota.streamSucceeded());
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(fw.size()), static_cast<uint32_t>(ota_stub::writtenBytes()));
    TEST_ASSERT_EQUAL_UINT32(expectCheck, ota_stub::checksum());
}
#endif

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tail_rejects_config_and_program);
    RUN_TEST(test_tail_accepts_ui_gz_and_schema);
    RUN_TEST(test_boot_stays_running_until_stream_finish);
    RUN_TEST(test_stream_finish_activates_new_boot);
    RUN_TEST(test_tail_failure_after_end_does_not_abort);
    RUN_TEST(test_ring_stall_times_out_and_fails_session);
    RUN_TEST(test_prepare_discards_stale_ring_bytes);
    RUN_TEST(test_begin_resets_parser_after_failed_session);
#if defined(OTA_TEST_HAS_THREADS)
    RUN_TEST(test_ring_stress_producer_consumer_keeps_all_bytes);
#endif
    return UNITY_END();
}
