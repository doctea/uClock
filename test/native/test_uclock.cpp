#include <vector>

#include <unity.h>

#include "uClock.h"

static uint32_t fake_micros = 0;
static umodular::clock::uClockClass *callback_clock = nullptr;
static std::vector<uint32_t> fired_steps;
static std::vector<uint32_t> fired_ticks;
static uint32_t sync_24_count = 0;
static uint32_t sync_4_count = 0;

uint32_t micros()
{
    return fake_micros;
}

uint32_t millis()
{
    return fake_micros / 1000;
}

static void record_step(uint32_t step)
{
    fired_steps.push_back(step);
    uint32_t callback_tick = callback_clock->tick;
    fired_ticks.push_back(callback_tick);
}

static void record_sync_24(uint32_t)
{
    ++sync_24_count;
}

static void record_sync_4(uint32_t)
{
    ++sync_4_count;
}

static void run_until_tick(umodular::clock::uClockClass &clock, uint32_t end_tick)
{
    while (clock.tick < end_tick) {
        fake_micros += 1000;
        clock.handleInternalClock();
    }
}

void setUp()
{
    fake_micros = 0;
    callback_clock = nullptr;
    fired_steps.clear();
    fired_ticks.clear();
    sync_24_count = 0;
    sync_4_count = 0;
}

void tearDown()
{
}

static void test_sync_callback_growth_preserves_existing_entries()
{
    umodular::clock::uClockClass clock;
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_4, record_sync_4);
    clock.init();

    TEST_ASSERT_EQUAL_UINT8(2, clock.sync_callback_size);
    TEST_ASSERT_EQUAL_UINT16(4, clock.sync_callbacks[0].sync_ref);
    TEST_ASSERT_EQUAL_UINT16(24, clock.sync_callbacks[1].sync_ref);

    clock.setOnSync(umodular::clock::uClockClass::PPQN_1, record_sync_4);
    TEST_ASSERT_EQUAL_UINT8(2, clock.sync_callback_size);

    clock.clock_state = umodular::clock::uClockClass::STARTED;
    run_until_tick(clock, 8);

    TEST_ASSERT_EQUAL_UINT32(2, sync_24_count);
    TEST_ASSERT_EQUAL_UINT32(1, sync_4_count);
}

static void test_reduced_ppqn_recovers_counters_above_new_references()
{
    umodular::clock::uClockClass clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.mod_clock_counter = 3;
    clock.tracks[0].mod_step_counter = 20;
    clock.sync_callbacks[0].mod_counter = 3;

    clock.setOutputPPQN(umodular::clock::uClockClass::PPQN_48);
    clock.clock_state = umodular::clock::uClockClass::STARTED;
    run_until_tick(clock, 1);

    TEST_ASSERT_EQUAL_UINT16(1, clock.mod_clock_counter);
    TEST_ASSERT_EQUAL_UINT8(1, clock.tracks[0].mod_step_counter);
    TEST_ASSERT_EQUAL_UINT16(1, clock.sync_callbacks[0].mod_counter);
    TEST_ASSERT_EQUAL_UINT32(1, sync_24_count);
    TEST_ASSERT_EQUAL_UINT32(1, fired_steps.size());
}

static void test_mid_cycle_shuffle_activation_does_not_add_a_step()
{
    umodular::clock::uClockClass clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    clock.clock_state = umodular::clock::uClockClass::STARTED;
    run_until_tick(clock, 10);

    int8_t shuffle_template[] = {8, -8};
    clock.setShuffleTemplate(shuffle_template, 2);
    clock.setShuffle(true);
    run_until_tick(clock, 40);

    const uint32_t expected_steps[] = {0, 1};
    const uint32_t expected_ticks[] = {0, 16};
    TEST_ASSERT_EQUAL_UINT32(2, fired_steps.size());
    TEST_ASSERT_EQUAL_UINT32(2, fired_ticks.size());
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_steps, fired_steps.data(), 2);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_ticks, fired_ticks.data(), 2);
}

static void test_live_shuffle_update_preserves_latched_step()
{
    umodular::clock::uClockClass clock;
    callback_clock = &clock;
    clock.setOnStep(record_step);
    clock.init();
    clock.clock_state = umodular::clock::uClockClass::STARTED;

    int8_t initial_template[] = {8, -8};
    clock.setShuffleTemplate(initial_template, 2);
    clock.setShuffle(true);
    run_until_tick(clock, 10);

    int8_t updated_template[] = {8, 8};
    clock.setShuffleTemplate(updated_template, 2);
    run_until_tick(clock, 60);

    const uint32_t expected_steps[] = {0, 1, 2};
    const uint32_t expected_ticks[] = {8, 16, 56};
    TEST_ASSERT_EQUAL_UINT32(3, fired_steps.size());
    TEST_ASSERT_EQUAL_UINT32(3, fired_ticks.size());
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_steps, fired_steps.data(), 3);
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_ticks, fired_ticks.data(), 3);
}

static void test_trace_freezes_after_first_anomaly()
{
    using Clock = umodular::clock::uClockClass;
    Clock clock;
    clock.clearTrace();

    clock.traceEvent(Clock::TRACE_STEP_FIRE, 0, 4);
    clock.traceEvent(Clock::TRACE_INVALID_STATE, 0, 5);
    clock.traceEvent(Clock::TRACE_STEP_FIRE, 0, 6);

    TEST_ASSERT_TRUE(clock.isTraceFrozen());
    TEST_ASSERT_EQUAL_UINT32(1, clock.getTraceDroppedCount());

    Clock::TraceEvent event;
    TEST_ASSERT_TRUE(clock.popTraceEvent(event));
    TEST_ASSERT_EQUAL_UINT8(Clock::TRACE_STEP_FIRE, event.type);
    TEST_ASSERT_TRUE(clock.popTraceEvent(event));
    TEST_ASSERT_EQUAL_UINT8(Clock::TRACE_INVALID_STATE, event.type);
    TEST_ASSERT_FALSE(clock.popTraceEvent(event));

    clock.clearTrace();
    TEST_ASSERT_FALSE(clock.isTraceFrozen());
    TEST_ASSERT_EQUAL_UINT32(0, clock.getTraceDroppedCount());
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_sync_callback_growth_preserves_existing_entries);
    RUN_TEST(test_reduced_ppqn_recovers_counters_above_new_references);
    RUN_TEST(test_mid_cycle_shuffle_activation_does_not_add_a_step);
    RUN_TEST(test_live_shuffle_update_preserves_latched_step);
    RUN_TEST(test_trace_freezes_after_first_anomaly);
    return UNITY_END();
}