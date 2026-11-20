#include <unity.h>

#include <string>

#include "LogSink.h"
#include "fs/FsManager.h"

// Атомарное закрытие потока: цель уходит в .bak, .tmp становится целью, .bak снимается.
// Если переименование .tmp → цель не удалось, .bak возвращается на место.

void setUp() {
    ram_fs::reset();
    if (fileSystem.isInitialized()) fileSystem.end();
    TEST_ASSERT_TRUE(fileSystem.begin());
    test_log::clear();
}

void tearDown() {}

static File openTmp(const char* path) {
    File f = LittleFS.open(path, "r");
    TEST_ASSERT_TRUE(static_cast<bool>(f));
    return f;
}

static void test_commit_renames_target_to_bak_then_tmp() {
    // Старое содержимое уже на цели, новое записано в .tmp.
    ram_fs::put("/config.json", "staroe");
    ram_fs::put("/config.json.tmp", "novoe");
    File f = openTmp("/config.json.tmp");

    TEST_ASSERT_TRUE(fileSystem.closeWriteStream(f, "/config.json", true));

    const std::string body = ram_fs::read("/config.json");
    TEST_ASSERT_EQUAL_STRING("novoe", body.c_str());
    TEST_ASSERT_FALSE(ram_fs::exists("/config.json.tmp"));
    TEST_ASSERT_FALSE(ram_fs::exists("/config.json.bak"));

    const auto& tr = ram_fs::trace();
    TEST_ASSERT_EQUAL_UINT(3, (unsigned)tr.size());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ram_fs::RamOpKind::Rename, (uint8_t)tr[0].kind);
    TEST_ASSERT_TRUE(tr[0].ok);
    TEST_ASSERT_EQUAL_STRING("/config.json", tr[0].from.c_str());
    TEST_ASSERT_EQUAL_STRING("/config.json.bak", tr[0].to.c_str());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ram_fs::RamOpKind::Rename, (uint8_t)tr[1].kind);
    TEST_ASSERT_TRUE(tr[1].ok);
    TEST_ASSERT_EQUAL_STRING("/config.json.tmp", tr[1].from.c_str());
    TEST_ASSERT_EQUAL_STRING("/config.json", tr[1].to.c_str());
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ram_fs::RamOpKind::Remove, (uint8_t)tr[2].kind);
    TEST_ASSERT_TRUE(tr[2].ok);
    TEST_ASSERT_EQUAL_STRING("/config.json.bak", tr[2].from.c_str());
}

static void test_failed_tmp_rename_restores_bak() {
    ram_fs::put("/config.json", "staroe");
    ram_fs::put("/config.json.tmp", "novoe");
    // Следующий rename именно в цель (это .tmp → цель) срывается один раз.
    ram_fs::failNextRenameTo("/config.json");
    File f = openTmp("/config.json.tmp");

    TEST_ASSERT_FALSE(fileSystem.closeWriteStream(f, "/config.json", true));

    const std::string body = ram_fs::read("/config.json");
    TEST_ASSERT_EQUAL_STRING("staroe", body.c_str());
    TEST_ASSERT_FALSE(ram_fs::exists("/config.json.tmp"));
    TEST_ASSERT_FALSE(ram_fs::exists("/config.json.bak"));

    const auto& tr = ram_fs::trace();
    TEST_ASSERT_EQUAL_UINT(4, (unsigned)tr.size());
    TEST_ASSERT_EQUAL_STRING("/config.json", tr[0].from.c_str());
    TEST_ASSERT_EQUAL_STRING("/config.json.bak", tr[0].to.c_str());
    TEST_ASSERT_TRUE(tr[0].ok);
    TEST_ASSERT_EQUAL_STRING("/config.json.tmp", tr[1].from.c_str());
    TEST_ASSERT_EQUAL_STRING("/config.json", tr[1].to.c_str());
    TEST_ASSERT_FALSE(tr[1].ok);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)ram_fs::RamOpKind::Remove, (uint8_t)tr[2].kind);
    TEST_ASSERT_EQUAL_STRING("/config.json.tmp", tr[2].from.c_str());
    TEST_ASSERT_TRUE(tr[2].ok);
    TEST_ASSERT_EQUAL_STRING("/config.json.bak", tr[3].from.c_str());
    TEST_ASSERT_EQUAL_STRING("/config.json", tr[3].to.c_str());
    TEST_ASSERT_TRUE(tr[3].ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_commit_renames_target_to_bak_then_tmp);
    RUN_TEST(test_failed_tmp_rename_restores_bak);
    return UNITY_END();
}
