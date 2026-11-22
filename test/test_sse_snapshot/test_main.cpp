#include <unity.h>

#include <string.h>

#include "core/Core.h"
#include "io/DigitalInputs.h"
#include "io/RelayController.h"
#include "io/SensorsController.h"
#include "web/internal/SseIncCommon.h"
#include "web/internal/SseStatusPort.h"

namespace {

class BufPrint : public Print {
public:
    size_t write(uint8_t c) override {
        if (n_ + 1 < sizeof(buf_)) {
            buf_[n_++] = static_cast<char>(c);
            buf_[n_] = '\0';
        } else {
            n_++;
        }
        return 1;
    }
    const char* data() const { return buf_; }

private:
    char buf_[2048]{};
    size_t n_{0};
};

} // namespace

void setUp() {
    test_gpio::reset();
    core.getRelay().begin();
    core.getRelay().allOff();
    core.getInputs().begin();
}

void tearDown() {}

static void test_hardware_payload_matches_golden_json() {
    TEST_ASSERT_TRUE(core.getRelay().on(0));
    core.getSensors().testSetVoltage(12.5f, true);

    SseStatusPort st;
    st.relay = &core.getRelay();
    st.inputs = &core.getInputs();
    st.sensors = &core.getSensors();
    st.engineRunning = true;

    BufPrint out;
    sse_inc_detail::emitHardwarePayload(out, st);
    const char* golden =
        "{\"relaysById\":{\"K1\":true,\"K2\":false,\"K3\":false,\"K4\":false,\"K5\":false},"
        "\"inputsById\":{\"IN1\":false,\"IN2\":false,\"IN3\":false},"
        "\"inputFrequenciesById\":{\"IN1\":null,\"IN2\":null,\"IN3\":null},"
        "\"inputsEnabledById\":{\"IN1\":true,\"IN2\":true,\"IN3\":true},"
        "\"tempSensors\":["
        "{\"id\":null,\"valid\":false,\"lastMs\":0,\"t\":null},"
        "{\"id\":null,\"valid\":false,\"lastMs\":0,\"t\":null},"
        "{\"id\":null,\"valid\":false,\"lastMs\":0,\"t\":null}"
        "],\"voltage\":12.50,\"engineRunning\":true}";
    TEST_ASSERT_EQUAL_STRING(golden, out.data());
}

static void test_queue_threshold_and_dedup() {
    TEST_ASSERT_EQUAL_UINT32(18, (uint32_t)sse_inc_detail::sseSoftQueueThreshold(21));
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)sse_inc_detail::sseSoftQueueThreshold(4));
    TEST_ASSERT_TRUE(sse_inc_detail::sseDedupChanged(0, 1));
    TEST_ASSERT_FALSE(sse_inc_detail::sseDedupChanged(0xA5A5A5A5u, 0xA5A5A5A5u));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_hardware_payload_matches_golden_json);
    RUN_TEST(test_queue_threshold_and_dedup);
    return UNITY_END();
}
