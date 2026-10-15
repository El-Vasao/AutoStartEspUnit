#include <unity.h>

#include "gsm/GsmAtParse.h"

void test_parseCreg_n_stat() {
    int8_t stat = -1;
    TEST_ASSERT_TRUE(gsm_at::parseCregStat("+CREG: 0,1", stat));
    TEST_ASSERT_EQUAL_INT8(1, stat);
}

void test_parseCreg_stat_only() {
    int8_t stat = -1;
    TEST_ASSERT_TRUE(gsm_at::parseCregStat("+CREG: 5", stat));
    TEST_ASSERT_EQUAL_INT8(5, stat);
}

void test_parseCreg_with_lac() {
    int8_t stat = -1;
    TEST_ASSERT_TRUE(gsm_at::parseCregStat("+CREG: 1,\"00AB\",\"1234\"", stat));
    TEST_ASSERT_EQUAL_INT8(1, stat);
}

void test_parseCreg_reject_garbage() {
    int8_t stat = 0;
    TEST_ASSERT_FALSE(gsm_at::parseCregStat("+CGATT: 1", stat));
    TEST_ASSERT_FALSE(gsm_at::parseCregStat(nullptr, stat));
    TEST_ASSERT_FALSE(gsm_at::parseCregStat("+CREG: 9", stat));
}

void test_parseCgatt() {
    int8_t stat = -1;
    TEST_ASSERT_TRUE(gsm_at::parseCgattStat("+CGATT: 1", stat));
    TEST_ASSERT_EQUAL_INT8(1, stat);
    TEST_ASSERT_TRUE(gsm_at::parseCgattStat("+CGATT: 0", stat));
    TEST_ASSERT_EQUAL_INT8(0, stat);
    TEST_ASSERT_FALSE(gsm_at::parseCgattStat("+CGATT: 2", stat));
}

void test_isIpv4Line() {
    TEST_ASSERT_TRUE(gsm_at::isIpv4Line("1.2.3.4"));
    TEST_ASSERT_TRUE(gsm_at::isIpv4Line("192.168.0.1"));
    TEST_ASSERT_FALSE(gsm_at::isIpv4Line("1.2.3"));
    TEST_ASSERT_FALSE(gsm_at::isIpv4Line("1.2.3.256"));
    TEST_ASSERT_FALSE(gsm_at::isIpv4Line(nullptr));
}

void test_sapbr_usable_bearer() {
    TEST_ASSERT_TRUE(gsm_at::sapbrLineHasUsableBearer("+SAPBR: 1,1,\"10.20.30.40\""));
    TEST_ASSERT_FALSE(gsm_at::sapbrLineHasUsableBearer("+SAPBR: 1,3,\"0.0.0.0\""));
    TEST_ASSERT_FALSE(gsm_at::sapbrLineHasUsableBearer("+SAPBR: 1,1,\"0.0.0.0\""));
    TEST_ASSERT_FALSE(gsm_at::sapbrLineHasUsableBearer("+CREG: 1"));
}

void test_extractPhoneDigits() {
    char out[24];
    TEST_ASSERT_EQUAL(11, gsm_at::extractPhoneDigits("+7 (903) 123-45-67", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("79031234567", out);
    TEST_ASSERT_EQUAL(0, gsm_at::extractPhoneDigits("abc", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_phonesMatch_suffix10() {
    TEST_ASSERT_TRUE(gsm_at::phonesMatch("+79031234567", "89031234567"));
    TEST_ASSERT_TRUE(gsm_at::phonesMatch("79031234567", "9031234567"));
    TEST_ASSERT_FALSE(gsm_at::phonesMatch("79031234567", "79031234568"));
    TEST_ASSERT_FALSE(gsm_at::phonesMatch("", "79031234567"));
}

void test_phonesMatch_short() {
    TEST_ASSERT_TRUE(gsm_at::phonesMatch("12345", "12345"));
    TEST_ASSERT_FALSE(gsm_at::phonesMatch("12345", "12346"));
}

void test_parseClipDigits() {
    char digits[24];
    TEST_ASSERT_TRUE(gsm_at::parseClipDigits("+CLIP: \"+79031234567\",145,\"\",,\"\",0", digits, sizeof(digits)));
    TEST_ASSERT_EQUAL_STRING("79031234567", digits);
    TEST_ASSERT_FALSE(gsm_at::parseClipDigits("+CLIP: ,145", digits, sizeof(digits)));
}

void test_parseDtmfTone() {
    char tone = 0;
    TEST_ASSERT_TRUE(gsm_at::parseDtmfTone("+DTMF: 5", tone));
    TEST_ASSERT_EQUAL('5', tone);
    TEST_ASSERT_TRUE(gsm_at::parseDtmfTone("+DDET: #", tone));
    TEST_ASSERT_EQUAL('#', tone);
    TEST_ASSERT_TRUE(gsm_at::parseDtmfTone("+DTMF: *", tone));
    TEST_ASSERT_EQUAL('*', tone);
    TEST_ASSERT_FALSE(gsm_at::parseDtmfTone("+DTMF: A", tone));
    TEST_ASSERT_FALSE(gsm_at::parseDtmfTone("+CREG: 1", tone));
}

void test_parseClccStat() {
    int8_t stat = -1;
    TEST_ASSERT_TRUE(gsm_at::parseClccStat("+CLCC: 1,0,0,0,0,\"+79001234567\",145", stat));
    TEST_ASSERT_EQUAL_INT8(0, stat);
    TEST_ASSERT_TRUE(gsm_at::parseClccStat("+CLCC: 1,0,3,0,0,\"+79001234567\",145", stat));
    TEST_ASSERT_EQUAL_INT8(3, stat);
    TEST_ASSERT_TRUE(gsm_at::parseClccStat("+CLCC: 1,0,2,0,0,\"7900\",129", stat));
    TEST_ASSERT_EQUAL_INT8(2, stat);
    TEST_ASSERT_FALSE(gsm_at::parseClccStat("+CLIP: \"1\",129", stat));
    TEST_ASSERT_FALSE(gsm_at::parseClccStat("+CLCC: garbage", stat));
    TEST_ASSERT_FALSE(gsm_at::parseClccStat(nullptr, stat));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parseCreg_n_stat);
    RUN_TEST(test_parseCreg_stat_only);
    RUN_TEST(test_parseCreg_with_lac);
    RUN_TEST(test_parseCreg_reject_garbage);
    RUN_TEST(test_parseCgatt);
    RUN_TEST(test_isIpv4Line);
    RUN_TEST(test_sapbr_usable_bearer);
    RUN_TEST(test_extractPhoneDigits);
    RUN_TEST(test_phonesMatch_suffix10);
    RUN_TEST(test_phonesMatch_short);
    RUN_TEST(test_parseClipDigits);
    RUN_TEST(test_parseDtmfTone);
    RUN_TEST(test_parseClccStat);
    return UNITY_END();
}
