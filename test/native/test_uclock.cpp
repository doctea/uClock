#include <vector>

#include <unity.h>

#include "uClock.h"

static uint32_t fake_micros = 0;
static umodular::clock::uClockClass *callback_clock = nullptr;
static std::vector<uint32_t> fired_steps;
static std::vector<uint32_t> fired_ticks;
static std::vector<uint32_t> sync_24_ticks;
static uint32_t sync_24_count = 0;
static uint32_t sync_4_count = 0;
static bool request_reentrant_tick = false;
static std::vector<uint8_t> callback_order;

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

static void record_sync_24(uint32_t tick)
{
    sync_24_ticks.push_back(tick);
    ++sync_24_count;
    if (request_reentrant_tick) {
        request_reentrant_tick = false;
        callback_clock->handleInternalClock();
    }
}

static void record_sync_4(uint32_t)
{
    ++sync_4_count;
}

static void record_order_sync(uint32_t)
{
    callback_order.push_back(1);
}

static void record_order_step(uint32_t)
{
    callback_order.push_back(2);
}

static void record_order_end(uint32_t)
{
    callback_order.push_back(3);
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
    sync_24_ticks.clear();
    sync_24_count = 0;
    sync_4_count = 0;
    request_reentrant_tick = false;
    callback_order.clear();
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

static void test_output_tick_end_runs_after_sync_and_step_callbacks()
{
    umodular::clock::uClockClass clock;
    clock.setOnSync(umodular::clock::uClockClass::PPQN_96, record_order_sync);
    clock.setOnStep(record_order_step);
    clock.setOnOutputPPQNEnd(record_order_end);
    clock.init();
    clock.clock_state = umodular::clock::uClockClass::STARTED;

    clock.handleInternalClock();

    const uint8_t expected_order[] = {1, 2, 3};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected_order, callback_order.data(), 3);
}

static void test_strict_external_clock_allows_only_one_output_group_per_pulse()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.start();

    fake_micros = 1000;
    clock.clockMe();

    TEST_ASSERT_EQUAL_UINT32(1, sync_24_count);
    TEST_ASSERT_EQUAL_UINT32(1, clock.tick);

    for (uint8_t i = 0; i < 10; i++) {
        fake_micros += 1000;
        clock.handleInternalClock();
    }

    TEST_ASSERT_EQUAL_UINT32(4, clock.tick);
    TEST_ASSERT_EQUAL_UINT32(1, sync_24_count);
}

static void test_reentrant_internal_callback_does_not_advance_twice()
{
    umodular::clock::uClockClass clock;
    callback_clock = &clock;
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.clock_state = umodular::clock::uClockClass::STARTED;
    request_reentrant_tick = true;

    clock.handleInternalClock();

    TEST_ASSERT_EQUAL_UINT32(1, clock.tick);
    TEST_ASSERT_EQUAL_UINT32(1, sync_24_count);
    TEST_ASSERT_EQUAL_UINT16(0, clock.getIntOverflowCounter());
    TEST_ASSERT_FALSE(clock.isTraceFrozen());

    clock.traceEvent(umodular::clock::uClockClass::TRACE_EXTERNAL_PULSE);
    umodular::clock::uClockClass::TraceEvent event;
    bool found_following_pulse = false;
    while (clock.popTraceEvent(event)) {
        if (event.type == umodular::clock::uClockClass::TRACE_EXTERNAL_PULSE)
            found_following_pulse = true;
    }
    TEST_ASSERT_TRUE(found_following_pulse);
}

static void test_external_tempo_ignores_short_startup_interval()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.init();
    clock.start();

    fake_micros = 1000;
    clock.clockMe();
    fake_micros += 11354;
    clock.clockMe();
    fake_micros += 33333;
    clock.clockMe();
    fake_micros += 33334;
    clock.clockMe();

    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, clock.getTempo());
}

static void test_early_external_pulse_catches_up_without_skipping_callbacks()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.start();

    fake_micros = 1000;
    clock.clockMe();
    fake_micros = 21000;
    clock.clockMe();

    const uint32_t expected_ticks[] = {0, 1};
    TEST_ASSERT_EQUAL_UINT32(5, clock.tick);
    TEST_ASSERT_EQUAL_UINT32(2, sync_24_ticks.size());
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_ticks, sync_24_ticks.data(), 2);
    TEST_ASSERT_EQUAL_UINT16(3, clock.external_ticks_remaining);
}

static void test_external_clock_resumes_after_dropout_without_resetting_position()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.start();

    fake_micros = 1000;
    clock.clockMe();
    fake_micros = 21000;
    clock.clockMe();
    TEST_ASSERT_EQUAL_UINT32(20000, clock.getLastAcceptedExternalInterval());

    for (uint8_t i = 0; i < 10; i++) {
        fake_micros += 1000;
        clock.handleInternalClock();
    }
    TEST_ASSERT_EQUAL_UINT32(8, clock.tick);

    fake_micros += 81000;
    for (uint8_t i = 0; i < 10; i++)
        clock.handleInternalClock();
    TEST_ASSERT_EQUAL_UINT32(8, clock.tick);
    TEST_ASSERT_EQUAL_UINT8(umodular::clock::uClockClass::STARTED, clock.clock_state);
    TEST_ASSERT_TRUE(clock.isExternalClockStalled());

    clock.clockMe();
    TEST_ASSERT_FALSE(clock.isExternalClockStalled());
    TEST_ASSERT_EQUAL_UINT32(20000, clock.getLastAcceptedExternalInterval());

    fake_micros += 20000;
    clock.clockMe();

    const uint32_t expected_ticks[] = {0, 1, 2, 3};
    TEST_ASSERT_EQUAL_UINT32(13, clock.tick);
    TEST_ASSERT_EQUAL_UINT32(4, sync_24_ticks.size());
    TEST_ASSERT_EQUAL_UINT32_ARRAY(expected_ticks, sync_24_ticks.data(), 4);
    TEST_ASSERT_EQUAL_UINT32(20000, clock.getLastAcceptedExternalInterval());
}

static void test_external_tempo_changes_do_not_duplicate_or_skip_sync_ticks()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.start();

    const uint32_t pulse_intervals[] = {
        21000, 21000, 18000, 18000, 25000, 25000, 16000, 30000
    };
    for (uint32_t pulse = 0; pulse < 32; pulse++) {
        uint8_t timer_ticks_before_pulse = pulse % 4;
        for (uint8_t tick_index = 0; tick_index < timer_ticks_before_pulse; tick_index++) {
            fake_micros += 1000;
            clock.handleInternalClock();
        }
        fake_micros += pulse_intervals[pulse % 8];
        clock.clockMe();
    }

    TEST_ASSERT_EQUAL_UINT32(32, sync_24_ticks.size());
    for (uint32_t i = 0; i < sync_24_ticks.size(); i++)
        TEST_ASSERT_EQUAL_UINT32(i, sync_24_ticks[i]);
}

static void test_external_continue_preserves_position_and_start_rewinds()
{
    umodular::clock::uClockClass clock;
    clock.setClockMode(umodular::clock::uClockClass::EXTERNAL_CLOCK);
    clock.setStrictExternalMode(true);
    clock.setOnSync(umodular::clock::uClockClass::PPQN_24, record_sync_24);
    clock.init();
    clock.start();

    fake_micros = 1000;
    clock.clockMe();
    while (clock.getExternalTicksRemaining() > 0) {
        fake_micros += 1000;
        clock.handleInternalClock();
    }

    clock.pause();
    clock.pause();
    fake_micros += 20000;
    clock.clockMe();

    TEST_ASSERT_EQUAL_UINT32(2, sync_24_ticks.size());
    TEST_ASSERT_EQUAL_UINT32(1, sync_24_ticks[1]);

    clock.start();
    fake_micros += 20000;
    clock.clockMe();

    TEST_ASSERT_EQUAL_UINT32(3, sync_24_ticks.size());
    TEST_ASSERT_EQUAL_UINT32(0, sync_24_ticks[2]);
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
    RUN_TEST(test_output_tick_end_runs_after_sync_and_step_callbacks);
    RUN_TEST(test_strict_external_clock_allows_only_one_output_group_per_pulse);
    RUN_TEST(test_reentrant_internal_callback_does_not_advance_twice);
    RUN_TEST(test_external_tempo_ignores_short_startup_interval);
    RUN_TEST(test_early_external_pulse_catches_up_without_skipping_callbacks);
    RUN_TEST(test_external_clock_resumes_after_dropout_without_resetting_position);
    RUN_TEST(test_external_tempo_changes_do_not_duplicate_or_skip_sync_ticks);
    RUN_TEST(test_external_continue_preserves_position_and_start_rewinds);
    RUN_TEST(test_mid_cycle_shuffle_activation_does_not_add_a_step);
    RUN_TEST(test_live_shuffle_update_preserves_latched_step);
    RUN_TEST(test_trace_freezes_after_first_anomaly);
    return UNITY_END();
}