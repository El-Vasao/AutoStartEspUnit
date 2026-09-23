// src/config/Config.ProgramJsonIo.cpp
#include "config/internal/ProgramJsonIo.h"

#include "json/Json.ParseFile.h"
#include "JsonListener.h"
#include "program/ProgramAction.h"
#include "common/Constants.h"

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

namespace program_json {

namespace {

bool streq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

float parseF(const char* v, float def) {
    if (!v || !*v) return def;
    return strtof(v, nullptr);
}

uint32_t parseU32(const char* v, uint32_t def) {
    if (!v || !*v) return def;
    char* end = nullptr;
    unsigned long n = strtoul(v, &end, 10);
    if (!end || *end != '\0') return def;
    return static_cast<uint32_t>(n);
}

uint16_t parseU16(const char* v, uint16_t def) {
    uint32_t n = parseU32(v, def);
    if (n > 65535u) return 65535;
    return static_cast<uint16_t>(n);
}

uint8_t parseU8(const char* v, uint8_t def) {
    uint32_t n = parseU32(v, def);
    if (n > 255u) return 255;
    return static_cast<uint8_t>(n);
}

static ComparisonOp parseComparisonLocal(const char* s) {
    if (!s || !*s) return ComparisonOp::Above;
    if (strcmp(s, "below") == 0) return ComparisonOp::Below;
    return ComparisonOp::Above;
}

void writeEscaped(Print& p, const char* s) {
    p.print('"');
    if (!s) {
        p.print('"');
        return;
    }
    for (const unsigned char* u = reinterpret_cast<const unsigned char*>(s); *u; u++) {
        const char c = static_cast<char>(*u);
        switch (c) {
            case '\"':
                p.print("\\\"");
                break;
            case '\\':
                p.print("\\\\");
                break;
            case '\b':
                p.print("\\b");
                break;
            case '\f':
                p.print("\\f");
                break;
            case '\n':
                p.print("\\n");
                break;
            case '\r':
                p.print("\\r");
                break;
            case '\t':
                p.print("\\t");
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20u) {
                    char buf[7];
                    snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                    p.print(buf);
                } else {
                    p.write(static_cast<unsigned char>(c));
                }
        }
    }
    p.print('"');
}

inline void comma(Print& p, bool* needComma) {
    if (*needComma) p.print(',');
    *needComma = true;
}

struct ForwardCountPrint final : public Print {
    Print& d;
    size_t n = 0;
    explicit ForwardCountPrint(Print& downstream) : d(downstream) {}
    size_t write(uint8_t b) override {
        const size_t w = d.write(b);
        n += w;
        return w;
    }
    size_t write(const uint8_t* buffer, size_t size) override {
        const size_t w = d.write(buffer, size);
        n += w;
        return w;
    }
};

// ---- index.json: [ {id, name}, ... ] ------------------------------------

enum class Idx : uint8_t { RootArr, Row, Junk };

class IndexListener final : public JsonListener {
public:
    explicit IndexListener(IndexRow* rows, size_t rowCap) : rows_(rows), cap_(rowCap) {}

    bool ok() const { return !failed_ && sawRootArray_ && sp_ == -1; }
    size_t count() const { return n_; }

    void whitespace(char) override {}
    void startDocument() override {
        failed_ = false;
        sawRootArray_ = false;
        sp_ = -1;
        n_ = 0;
        junk_ = 0;
        pending_[0] = '\0';
        row_.id = 0;
        row_.name[0] = '\0';
    }
    void endDocument() override {}

    void startArray() override {
        if (failed_) return;
        if (sp_ < 0) {
            push(Idx::RootArr);
            sawRootArray_ = true;
            return;
        }
        intoJunk_();
    }
    void endArray() override {
        if (failed_) return;
        if (popJunk()) return;
        if (sp_ >= 0 && cur() == Idx::RootArr) {
            pop();
            return;
        }
        failed_ = true;
    }

    void startObject() override {
        if (failed_) return;
        if (junk_) {
            junk_++;
            return;
        }
        if (cur() != Idx::RootArr) {
            intoJunk_();
            return;
        }
        push(Idx::Row);
        row_.id = 0;
        row_.name[0] = '\0';
    }
    void endObject() override {
        if (failed_) return;
        if (popJunk()) return;
        if (cur() != Idx::Row) {
            failed_ = true;
            return;
        }
        if (rows_ && n_ < cap_) {
            rows_[n_] = row_;
            n_++;
        } else if (cap_ > 0) {
            failed_ = true;
            pop();
            return;
        }
        pop();
    }

    void key(const char* k) override {
        if (failed_ || junk_) return;
        if (cur() == Idx::Row) strlcpy(pending_, k ? k : "", sizeof pending_);
    }
    void value(const char* val) override {
        if (failed_ || junk_) return;
        if (cur() != Idx::Row) return;
        const char* v = val;
        if (v && strcmp(v, "null") == 0) return;
        if (streq(pending_, "id")) row_.id = parseU8(v, 0);
        else if (streq(pending_, "name")) strlcpy(row_.name, v ? v : "", sizeof row_.name);
    }

private:
    Idx cur() const { return sp_ >= 0 ? stk_[sp_] : Idx::RootArr; }

    void push(Idx x) {
        if (sp_ + 1 >= 8) {
            failed_ = true;
            return;
        }
        stk_[++sp_] = x;
    }
    void pop() {
        if (sp_ >= 0) sp_--;
    }

    void intoJunk_() {
        junk_++;
    }
    bool popJunk() {
        if (!junk_) return false;
        junk_--;
        return true;
    }

    IndexRow* rows_;
    size_t cap_;
    size_t n_{0};
    bool failed_{false};
    bool sawRootArray_{false};
    Idx stk_[8]{};
    int sp_{-1};
    uint8_t junk_{0};
    char pending_[20]{};
    IndexRow row_{};
};

bool parseProgramIndexDispatch(File& f, IndexRow* rows, size_t rowCap, size_t* outCount) {
    if (outCount) *outCount = 0;
    IndexListener listener(rows, rowCap);
    jsonStreamingParseWholeFile(f, listener);
    if (!listener.ok()) return false;
    if (outCount) *outCount = listener.count();
    return true;
}

// ---- program object + optional steps subtree skip -----------------------

enum class P : uint8_t { Root, StepsArr, StepObj, StepsSkip, Junk };

class SkipStepsRootListener : public JsonListener {
protected:
    void push(P x) {
        if (sp_ + 1 >= 12) {
            failed_ = true;
            return;
        }
        stk_[++sp_] = x;
    }
    void pop() {
        if (sp_ >= 0) sp_--;
    }
    P cur() const { return stk_[sp_]; }

    bool junk_{false};
    unsigned junkNest_{0};

    bool bumpJunkStart() {
        if (junk_) {
            junkNest_++;
            return true;
        }
        return false;
    }
    bool bumpJunkEnd() {
        if (!junk_) return false;
        if (junkNest_) junkNest_--;
        return true;
    }
    void enterJunk() {
        junk_ = true;
        junkNest_ = 0;
    }

    bool inStepsSkip() const { return sp_ >= 0 && cur() == P::StepsSkip; }

    uint8_t skipLvl_{0};

    static constexpr unsigned kSkipInit = 1;

protected:
    bool failed_{false};
    P stk_[12]{};
    int sp_{-1};
    // Program keys fit in <= 16 chars ("input_trigger_id"), keep small headroom.
    char pending_[24]{};
};

/** Reads id+name only; skips `steps` via StepsSkip+delta nesting. */
class HeaderListener final : public SkipStepsRootListener {
public:
    HeaderListener(uint8_t pathId, uint8_t* oid, char* name, size_t nameCap)
        : pid_(pathId), oid_(oid), name_(name), nameCap_(nameCap) {}

    bool okResult() const { return ok_ && !failed_; }

    void whitespace(char) override {}
    void startDocument() override {
        failed_ = false;
        ok_ = false;
        sp_ = -1;
        junk_ = false;
        junkNest_ = 0;
        skipLvl_ = 0;
        pending_[0] = '\0';
        if (oid_) *oid_ = 0;
        if (name_ && nameCap_) name_[0] = '\0';
    }
    void endDocument() override { ok_ = !failed_; }

    void startObject() override {
        if (failed_) return;
        if (bumpJunkStart()) return;
        if (inStepsSkip()) {
            skipLvl_++;
            return;
        }
        if (sp_ < 0) {
            push(P::Root);
            return;
        }
        enterJunk();
    }

    void endObject() override {
        if (failed_) return;
        if (bumpJunkEnd()) return;
        if (inStepsSkip() && skipLvl_) {
            if (skipLvl_) skipLvl_--;
            return;
        }
        if (sp_ >= 0 && cur() == P::Root) {
            pop();
            return;
        }
        failed_ = true;
    }

    void startArray() override {
        if (failed_) return;
        if (bumpJunkStart()) return;
        if (inStepsSkip()) {
            skipLvl_++;
            return;
        }
        if (sp_ >= 0 && cur() == P::Root && streq(pending_, "steps")) {
            push(P::StepsSkip);
            skipLvl_ = kSkipInit;
            return;
        }
        enterJunk();
    }

    void endArray() override {
        if (failed_) return;
        if (bumpJunkEnd()) return;
        if (inStepsSkip()) {
            if (skipLvl_) skipLvl_--;
            if (skipLvl_ == 0) pop();
            return;
        }
        failed_ = true;
    }

    void key(const char* k) override {
        if (failed_ || junk_ || inStepsSkip()) return;
        strlcpy(pending_, k ? k : "", sizeof pending_);
    }

    void value(const char* val) override {
        if (failed_ || junk_) return;
        if (sp_ < 0 || cur() != P::Root) return;
        const char* v = val;
        if (v && strcmp(v, "null") == 0) return;
        if (streq(pending_, "id")) {
            const uint8_t id = parseU8(v, 0);
            if (oid_) *oid_ = id;
            if (id != pid_) failed_ = true;
        } else if (streq(pending_, "name") && name_ && nameCap_) {
            strlcpy(name_, v ? v : "", nameCap_);
        }
    }

private:
    uint8_t pid_{0};
    uint8_t* oid_{nullptr};
    char* name_{nullptr};
    size_t nameCap_{0};
    bool ok_{false};
};

template <typename StepSink>
class ProgramBodyListenerBase : public SkipStepsRootListener {
public:
    void whitespace(char) override {}

    void startDocument() override {
        failed_ = false;
        sp_ = -1;
        junk_ = false;
        junkNest_ = 0;
        pending_[0] = '\0';
        static_cast<StepSink*>(this)->onStartDoc();
    }
    void endDocument() override { static_cast<StepSink*>(this)->onEndDoc(); }

    void startObject() override {
        if (failed_) return;
        if (bumpJunkStart()) return;
        if (sp_ < 0) {
            push(P::Root);
            return;
        }
        if (cur() == P::StepsArr) {
            static_cast<StepSink*>(this)->onBeginStepObject();
            push(P::StepObj);
            return;
        }
        if (cur() == P::StepObj) {
            enterJunk();
            return;
        }
        enterJunk();
    }

    void endObject() override {
        if (failed_) return;
        if (bumpJunkEnd()) return;
        if (sp_ >= 0 && cur() == P::StepObj) {
            static_cast<StepSink*>(this)->onEndStepObject();
            pop(); // StepObj
            return;
        }
        if (sp_ >= 0 && cur() == P::Root) {
            pop();
            return;
        }
        failed_ = true;
    }

    void startArray() override {
        if (failed_) return;
        if (bumpJunkStart()) return;
        if (sp_ >= 0 && cur() == P::Root && streq(pending_, "steps")) {
            push(P::StepsArr);
            return;
        }
        enterJunk();
    }

    void endArray() override {
        if (failed_) return;
        if (bumpJunkEnd()) return;
        if (sp_ >= 0 && cur() == P::StepsArr) {
            pop();
            return;
        }
        failed_ = true;
    }

    void key(const char* k) override {
        if (failed_ || junk_) return;
        strlcpy(pending_, k ? k : "", sizeof pending_);
    }

protected:
    void valueRoot(const char* v) {
        if (failed_ || junk_) return;
        if (sp_ < 0 || cur() != P::Root) return;
        if (v && strcmp(v, "null") == 0) return;
        static_cast<StepSink*>(this)->onRootValue(v);
    }
    void valueStep(const char* v) {
        if (failed_ || junk_) return;
        if (sp_ < 0 || cur() != P::StepObj) return;
        if (v && strcmp(v, "null") == 0) return;
        static_cast<StepSink*>(this)->onStepValue(v);
    }
};

class FullProgramListener final : public ProgramBodyListenerBase<FullProgramListener> {
public:
    explicit FullProgramListener(Program* p) : prog_(p) {}

    void onStartDoc() {
        if (prog_) *prog_ = Program();
    }
    void onEndDoc() {
        if (prog_ && prog_->id && prog_->step_count > 0) good_ = true;
    }
    void onBeginStepObject() {
        if (!prog_) {
            failed_ = true;
            return;
        }
        if (prog_->step_count >= Limits::MAX_STEPS_PER_PROGRAM) {
            failed_ = true;
            return;
        }
        cur_ = Step{};
    }
    void onEndStepObject() {
        if (!prog_) return;
        Step& s = prog_->steps[prog_->step_count];
        s = cur_;
        if (s.step == 0) s.step = static_cast<uint8_t>(prog_->step_count + 1);
        if (s.action[0] == '\0') {
            failed_ = true;
            return;
        }
        prog_->step_count++;
    }
    void onRootValue(const char* v) {
        if (!prog_) return;
        if (streq(pending_, "id")) prog_->id = parseU8(v, 0);
        else if (streq(pending_, "name")) strlcpy(prog_->name, v ? v : "", sizeof prog_->name);
    }
    void onStepValue(const char* v) {
        if (streq(pending_, "step")) cur_.step = parseU8(v, 0);
        else if (streq(pending_, "action")) strlcpy(cur_.action, v ? v : "", sizeof cur_.action);
        else if (streq(pending_, "relay_id")) cur_.relay_id = parseU16(v, 0);
        else if (streq(pending_, "ms")) cur_.ms = parseU32(v, 0);
        else if (streq(pending_, "timeout_ms")) cur_.timeout_ms = parseU32(v, 0);
        else if (streq(pending_, "input_id")) cur_.input_id = parseU16(v, 0);
        else if (streq(pending_, "sensor_id")) cur_.sensor_id = parseU16(v, 0);
        else if (streq(pending_, "sensor_name")) strlcpy(cur_.sensor_name, v ? v : "", sizeof cur_.sensor_name);
        else if (streq(pending_, "input_trigger_id")) cur_.input_trigger_id = parseU16(v, 0);
        else if (streq(pending_, "temp_trigger_id")) cur_.temp_trigger_id = parseU16(v, 0);
        else if (streq(pending_, "program_id")) cur_.program_id = parseU8(v, 0);
        else if (streq(pending_, "retries")) cur_.retries = parseU8(v, 1);
        else if (streq(pending_, "expected_state")) cur_.expected_state = parseU8(v, 1);
        else if (streq(pending_, "comparison")) strlcpy(cur_.comparison, v ? v : "above", sizeof cur_.comparison);
        else if (streq(pending_, "threshold")) cur_.threshold = parseF(v, 0.0f);
        else if (streq(pending_, "engine_state")) cur_.engine_state = parseU8(v, 1);
        else if (streq(pending_, "timeout_action")) cur_.timeout_action = parseU8(v, 0);
        else if (streq(pending_, "skip_count")) cur_.skip_count = parseU8(v, 0);
    }

    void value(const char* val) override {
        const char* v = val;
        valueRoot(v);
        valueStep(v);
    }

    bool good() const { return good_ && !failed_; }

private:
    Program* prog_{nullptr};
    Step cur_{};
    bool good_{false};
};

class CompiledProgramListener final : public ProgramBodyListenerBase<CompiledProgramListener> {
public:
    CompiledProgramListener(uint8_t expectId, CompiledStep* steps, uint8_t maxSteps, uint8_t* cnt, char* name,
                            size_t nameCap)
        : expectId_(expectId), steps_(steps), maxSteps_(maxSteps), cnt_(cnt), name_(name), nameCap_(nameCap) {}

    void onStartDoc() {
        if (cnt_) *cnt_ = 0;
        if (name_ && nameCap_) name_[0] = '\0';
    }
    void onEndDoc() { good_ = !failed_; }
    void onBeginStepObject() {
        if (!steps_ || !cnt_) {
            failed_ = true;
            return;
        }
        if (*cnt_ >= maxSteps_) {
            failed_ = true;
            return;
        }
        cur_ = CompiledStep{};
    }
    void onEndStepObject() {
        if (!steps_ || !cnt_) return;
        steps_[*cnt_] = cur_;
        (*cnt_)++;
    }
    void onRootValue(const char* v) {
        if (streq(pending_, "id")) {
            if (parseU8(v, 0) != expectId_) failed_ = true;
        } else if (streq(pending_, "name")) {
            if (name_ && nameCap_) strlcpy(name_, v ? v : "", nameCap_);
        }
    }
    void onStepValue(const char* v) {
        if (streq(pending_, "action")) cur_.action = actionIdFromString(v);
        else if (streq(pending_, "relay_id")) cur_.relay_id = parseU16(v, 0);
        else if (streq(pending_, "ms")) cur_.ms = parseU32(v, 0);
        else if (streq(pending_, "timeout_ms")) cur_.timeout_ms = parseU32(v, 0);
        else if (streq(pending_, "input_id")) cur_.input_id = parseU16(v, 0);
        else if (streq(pending_, "sensor_id")) cur_.sensor_id = parseU16(v, 0);
        else if (streq(pending_, "input_trigger_id")) cur_.input_trigger_id = parseU16(v, 0);
        else if (streq(pending_, "temp_trigger_id")) cur_.temp_trigger_id = parseU16(v, 0);
        else if (streq(pending_, "program_id")) cur_.program_id = parseU8(v, 0);
        else if (streq(pending_, "retries")) cur_.retries = parseU8(v, 1);
        else if (streq(pending_, "expected_state")) cur_.expected_state = parseU8(v, 1);
        else if (streq(pending_, "comparison")) cur_.comparison = parseComparisonLocal(v);
        else if (streq(pending_, "threshold")) cur_.threshold = parseF(v, 0.0f);
        else if (streq(pending_, "engine_state")) cur_.engine_state = parseU8(v, 1);
        else if (streq(pending_, "timeout_action")) cur_.timeout_action = parseU8(v, 0);
        else if (streq(pending_, "skip_count")) cur_.skip_count = parseU8(v, 0);
    }

    void value(const char* val) override {
        const char* v = val;
        valueRoot(v);
        valueStep(v);
    }

    bool good() const { return good_ && !failed_; }

private:
    uint8_t expectId_{0};
    CompiledStep* steps_{nullptr};
    uint8_t maxSteps_{0};
    uint8_t* cnt_{nullptr};
    char* name_{nullptr};
    size_t nameCap_{0};
    CompiledStep cur_{};
    bool good_{false};
};

void emitProgramBody(const Program& p, Print& pout) {
    bool c = false;
    pout.print('{');
    comma(pout, &c), pout.print("\"id\":"), pout.print(p.id);
    comma(pout, &c), pout.print("\"name\":"), writeEscaped(pout, p.name);
    comma(pout, &c), pout.print("\"steps\":[");
    for (uint8_t i = 0; i < p.step_count; i++) {
        if (i != 0) pout.print(',');
        const Step& s = p.steps[i];
        pout.print('{');
        bool sc = false;
        comma(pout, &sc), pout.print("\"step\":"), pout.print(s.step);
        comma(pout, &sc), pout.print("\"action\":"), writeEscaped(pout, s.action);
        if (strncmp(s.action, "RELAY_", 6) == 0) {
            if (s.relay_id) comma(pout, &sc), pout.print("\"relay_id\":"), pout.print(s.relay_id);
        }
        if (s.ms) comma(pout, &sc), pout.print("\"ms\":"), pout.print(s.ms);
        if (s.timeout_ms) comma(pout, &sc), pout.print("\"timeout_ms\":"), pout.print(s.timeout_ms);
        if (s.input_id != 0) comma(pout, &sc), pout.print("\"input_id\":"), pout.print(s.input_id);
        if (s.sensor_id != 0) comma(pout, &sc), pout.print("\"sensor_id\":"), pout.print(s.sensor_id);
        if (s.sensor_name[0] != '\0') comma(pout, &sc), pout.print("\"sensor_name\":"), writeEscaped(pout, s.sensor_name);
        if (s.input_trigger_id != 0) comma(pout, &sc), pout.print("\"input_trigger_id\":"), pout.print(s.input_trigger_id);
        if (s.temp_trigger_id != 0) comma(pout, &sc), pout.print("\"temp_trigger_id\":"), pout.print(s.temp_trigger_id);
        if (s.program_id) comma(pout, &sc), pout.print("\"program_id\":"), pout.print(s.program_id);
        if (s.retries != 1) comma(pout, &sc), pout.print("\"retries\":"), pout.print(s.retries);
        if (s.expected_state == 0) comma(pout, &sc), pout.print("\"expected_state\":"), pout.print(0);
        if (s.comparison[0] && strcmp(s.comparison, "above") != 0)
            comma(pout, &sc), pout.print("\"comparison\":"), writeEscaped(pout, s.comparison);
        if (s.threshold != 0.0f) comma(pout, &sc), pout.print("\"threshold\":"), pout.print(s.threshold);
        if (s.engine_state == 0) comma(pout, &sc), pout.print("\"engine_state\":"), pout.print(0);
        if (s.timeout_action) comma(pout, &sc), pout.print("\"timeout_action\":"), pout.print(s.timeout_action);
        if (s.skip_count) comma(pout, &sc), pout.print("\"skip_count\":"), pout.print(s.skip_count);
        pout.print('}');
    }
    pout.print(']');
    pout.print('}');
}

void emitIndexArrayBody(const IndexRow* rows, size_t n, Print& p) {
    p.print('[');
    for (size_t i = 0; i < n; i++) {
        if (i != 0) p.print(',');
        p.print('{');
        p.print("\"id\":");
        p.print(rows[i].id);
        p.print(",\"name\":");
        writeEscaped(p, rows[i].name);
        p.print('}');
    }
    p.print(']');
}

} // namespace

bool parseProgramObjectFile(File& f, Program& out) {
    FullProgramListener listener(&out);
    jsonStreamingParseWholeFile(f, listener);
    return listener.good();
}

bool parseProgramCompiledFile(File& f, uint8_t expectedId, CompiledStep* outSteps, uint8_t maxSteps,
                              uint8_t* outCount, char* outName, size_t outNameSize) {
    if (!outSteps || maxSteps == 0 || !outCount || !outName || outNameSize == 0) return false;
    CompiledProgramListener listener(expectedId, outSteps, maxSteps, outCount, outName, outNameSize);
    jsonStreamingParseWholeFile(f, listener);
    return listener.good();
}

bool parseProgramHeaderFile(File& f, uint8_t pathId, uint8_t* outId, char* outName, size_t outNameSize) {
    HeaderListener listener(pathId, outId, outName, outNameSize);
    jsonStreamingParseWholeFile(f, listener);
    return listener.okResult();
}

bool parseProgramIndexFile(File& f, IndexRow* rows, size_t rowCap, size_t* outCount) {
    return parseProgramIndexDispatch(f, rows, rowCap, outCount);
}

size_t emitProgramToPrint(const Program& p, Print& out) {
    ForwardCountPrint fc(out);
    emitProgramBody(p, fc);
    return fc.n;
}

size_t emitProgramIndexArrayPrint(const IndexRow* rows, size_t n, Print& out) {
    ForwardCountPrint fc(out);
    emitIndexArrayBody(rows, n, fc);
    return fc.n;
}

size_t emitProgramListWrappedPrint(const IndexRow* rows, size_t n, Print& out) {
    ForwardCountPrint fc(out);
    fc.print("{\"programs\":");
    emitIndexArrayBody(rows, n, fc);
    fc.print('}');
    return fc.n;
}

} // namespace program_json
