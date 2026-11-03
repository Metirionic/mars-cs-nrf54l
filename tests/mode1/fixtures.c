#include <string.h>

#include "cs_step_parse.h"
static void finish(unsigned status)
{
    uint32_t          args[]           = {0x20026, status};
    register uint32_t op __asm("r0")   = 0x20;
    register void *   data __asm("r1") = args;
    __asm volatile("bkpt 0xab" : : "r"(op), "r"(data) : "memory");
    for (;;)
    {
    }
}
#define CHECK(expr)           \
    do                        \
    {                         \
        if (!(expr))          \
            finish(__LINE__); \
    } while (0)
struct bt_le_cs_iq_sample bt_le_cs_parse_pct(const uint8_t pct[3])
{
    int16_t i = (int16_t)(((uint16_t)pct[0] | ((uint16_t)pct[1] << 8)) << 4);
    int16_t q = (int16_t)(((uint16_t)pct[1] | ((uint16_t)pct[2] << 8)) & 0xfff0);
    return (struct bt_le_cs_iq_sample){i, q};
}
static SubeventResultEvent_t    local_event, peer_event;
static struct cs_local_subevent headers[2];
static uint8_t                  local_raw[4096], peer_raw[4096];
static struct net_buf_simple    local_buf, peer_buf;
static unsigned                 emitted;
static bool                     expect_empty;
static void                     emit(SubeventResultEvent_t * local, SubeventResultEvent_t * peer)
{
    const unsigned i = emitted++;
    CHECK(i < 2);
    CHECK(local->has_initial_meta && peer->has_initial_meta);
    CHECK(local->initial_meta.procedure_counter == 42 && peer->initial_meta.procedure_counter == 42);
    CHECK(local->initial_meta.reference_power_level.value == -10 - (int)i);
    CHECK(peer->initial_meta.reference_power_level.value == -20 - (int)i);
    CHECK(local->initial_meta.frequency_compensation.value == 100 + i);
    CHECK(peer->initial_meta.frequency_compensation.value == 0xc000);
    CHECK(local->procedure_done_status == (i == 0 ? DONE_STATUS_PARTIAL : DONE_STATUS_ALL_COMPLETE));
    CHECK(peer->procedure_done_status == local->procedure_done_status);
    if (expect_empty && i == 0)
    {
        CHECK(local->step_count == 2 && peer->step_count == 0);
        CHECK(local->steps.idx[0].mode == 0);
        CHECK(local->steps.idx[1].mode == 2);
        CHECK(local->subevent_done_status == DONE_STATUS_ALL_COMPLETE);
        CHECK(peer->subevent_done_status == DONE_STATUS_ABORTED);
        CHECK(peer->subevent_abort_reason == SUBEVENT_ABORT_REASON_UNSPECIFIED);
        return;
    }
    CHECK(local->step_count == 2 && peer->step_count == 2);
    CHECK(local->steps.idx[0].mode == 0 && local->steps.idx[1].mode == 2);
    CHECK(local->steps.idx[1].channel == 30 + i);
}
static void step(
    unsigned mode, unsigned channel, const void * local, size_t local_len, const void * peer, size_t peer_len)
{
    net_buf_simple_add_u8(&local_buf, mode);
    net_buf_simple_add_u8(&local_buf, channel);
    net_buf_simple_add_u8(&local_buf, local_len);
    net_buf_simple_add_mem(&local_buf, local, local_len);
    net_buf_simple_add_u8(&peer_buf, mode);
    net_buf_simple_add_mem(&peer_buf, peer, peer_len);
}
static void setup(void)
{
    emitted      = 0;
    expect_empty = false;
    net_buf_simple_init_with_data(&local_buf, local_raw, sizeof(local_raw));
    net_buf_simple_init_with_data(&peer_buf, peer_raw, sizeof(peer_raw));
    net_buf_simple_reset(&local_buf);
    net_buf_simple_reset(&peer_buf);
    struct ras_ranging_header ranging = {.ranging_counter = 42, .config_id = 1, .antenna_paths_mask = 1};
    net_buf_simple_add_mem(&peer_buf, &ranging, sizeof(ranging));
    for (unsigned i = 0; i < 2; i++)
    {
        headers[i] = (struct cs_local_subevent){
            .result.header = {
                              .config_id              = 1,
                              .procedure_counter      = 42,
                              .start_acl_conn_event   = 100 + i,
                              .frequency_compensation = 100 + i,
                              .reference_power_level  = -10 - i,
                              .num_steps_reported     = 2,
                              .num_antenna_paths      = 1,
                              .procedure_done_status = i == 0 ? BT_CONN_LE_CS_PROCEDURE_INCOMPLETE : BT_CONN_LE_CS_PROCEDURE_COMPLETE,
                              .subevent_done_status  = BT_CONN_LE_CS_SUBEVENT_COMPLETE}
        };
        struct ras_subevent_header sub = {.start_acl_conn_event = 100 + i,
                                          .freq_compensation    = 0xc000,
                                          .ranging_done_status  = i == 0 ? 1 : 0,
                                          .ref_power_level      = -20 - i,
                                          .num_steps_reported   = 2};
        net_buf_simple_add_mem(&peer_buf, &sub, sizeof(sub));
        size_t        before       = local_buf.len;
        const uint8_t mode0local[] = {0, 200, 1, 100, 0}, mode0peer[] = {0, 200, 1};
        step(0, 10 + i, mode0local, sizeof(mode0local), mode0peer, sizeof(mode0peer));
        const uint8_t mode2[] = {0, 0, 4, 0, 0, 0, 4, 0, 0};
        step(2, 30 + i, mode2, sizeof(mode2), mode2, sizeof(mode2));
        headers[i].data_len = local_buf.len - before;
    }
}
static bool parse(void)
{
    return subevent_populate(&local_event,
                             &peer_event,
                             1,
                             2,
                             headers,
                             2,
                             &local_buf,
                             &peer_buf,
                             BT_CONN_LE_CS_ROLE_INITIATOR,
                             emit);
}
static void emit_capacity(SubeventResultEvent_t * local, SubeventResultEvent_t * peer)
{
    emitted++;
    CHECK(local->step_count == 160 && peer->step_count == 160);
    CHECK(local->steps.idx[159].mode == 2);
}
static void test_capacity(unsigned first_count)
{
    setup();
    net_buf_simple_reset(&local_buf);
    net_buf_simple_reset(&peer_buf);
    struct ras_ranging_header ranging = {.ranging_counter = 42, .config_id = 1, .antenna_paths_mask = 1};
    net_buf_simple_add_mem(&peer_buf, &ranging, sizeof(ranging));
    const uint8_t mode2[] = {0, 0, 4, 0, 0, 0, 4, 0, 0};
    for (unsigned i = 0; i < 2; i++)
    {
        unsigned count                              = i == 0 ? first_count : 160;
        headers[i].result.header.num_steps_reported = count;
        headers[i].data_len                         = count * 12;
        struct ras_subevent_header sub              = {.start_acl_conn_event = 100 + i, .num_steps_reported = count};
        net_buf_simple_add_mem(&peer_buf, &sub, sizeof(sub));
        for (unsigned j = 0; j < count; j++) step(2, 20, mode2, sizeof(mode2), mode2, sizeof(mode2));
    }
    bool ok = subevent_populate(&local_event,
                                &peer_event,
                                1,
                                2,
                                headers,
                                2,
                                &local_buf,
                                &peer_buf,
                                BT_CONN_LE_CS_ROLE_INITIATOR,
                                emit_capacity);
    CHECK(ok == (first_count == 160));
    CHECK(emitted == (first_count == 160 ? 2 : 0));
}
void test_main(void)
{
    test_capacity(160); /* 320 steps in one procedure, 160 in each event. */
    test_capacity(161); /* Reject an oversized subevent, never truncate it. */
    setup();
    CHECK(parse());
    CHECK(emitted == 2);
    setup();
    headers[1].result.header.start_acl_conn_event++;
    CHECK(!parse());
    CHECK(emitted == 1);
    setup();
    peer_buf.len--;
    CHECK(!parse());
    CHECK(emitted == 1);
    setup();
    headers[1].data_len--;
    CHECK(!parse());
    CHECK(emitted == 1);
    setup();
    headers[1].result.header.num_steps_reported++;
    CHECK(!parse());
    CHECK(emitted == 1);
    setup();
    local_raw[3 + 5 + 2] = 4;
    CHECK(!parse());
    CHECK(emitted == 0); /* Truncated Mode 2 payload. */
    setup();
    peer_raw[sizeof(struct ras_ranging_header)]++;
    CHECK(!parse());
    CHECK(emitted == 0);
    setup();
    /* Peer aborts first subevent: its RAS data has a header but no steps. */
    struct ras_subevent_header * sub              = (void *)(peer_raw + sizeof(struct ras_ranging_header));
    size_t                       first_peer_steps = (1 + 3) + (1 + 9);
    memmove((uint8_t *)sub + sizeof(*sub),
            (uint8_t *)sub + sizeof(*sub) + first_peer_steps,
            peer_buf.len - sizeof(struct ras_ranging_header) - sizeof(*sub) - first_peer_steps);
    peer_buf.len -= first_peer_steps;
    sub->num_steps_reported    = 0;
    sub->subevent_done_status  = 15;
    sub->subevent_abort_reason = 15;
    expect_empty               = true;
    CHECK(parse());
    CHECK(emitted == 2);
    /* Distinct role-specific RTT fields survive paired parsing. */
    uint8_t payload[] = {0x21, 7, 200, 0x2e, 0xfb, 1};
    uint8_t other[] = {0x43, 9, 190, 0x34, 0x12, 2};
    struct bt_le_cs_subevent_step x = {.mode = 1, .channel = 10, .data_len = 6, .data = payload};
    struct bt_le_cs_subevent_step y = {.mode = 1, .channel = 10, .data_len = 6, .data = other};
    struct cs_step_parse_context ctx = {.p_local_event = &local_event, .p_peer_event = &peer_event};
    local_event.origin = ORIGIN_INITIATOR;
    peer_event.origin = ORIGIN_REFLECTOR;
    CHECK(cs_step_parse_step(&ctx, &x, &y));
    CHECK(ctx.step_index == 1);
    CHECK(local_event.steps.idx[0].info.kind == MODE_ROLE_SPECIFIC_INFO_KIND_MODE1_INITIATOR);
    CHECK(peer_event.steps.idx[0].info.kind == MODE_ROLE_SPECIFIC_INFO_KIND_MODE1_REFLECTOR);
    Mode1Data_t * m = &local_event.steps.idx[0].info.mode1;
    CHECK(m->timing.role_specific_timing_value == -1234);
    CHECK(m->packet.packet_quality.access_address_check_result == 1);
    CHECK(m->packet.packet_quality.payload_bit_error_count == 2);
    CHECK(m->packet.packet_received_signal_strength_indicator == -56);
    CHECK(m->packet.packet_normalized_attack_detector_metric == 7);
    CHECK(peer_event.steps.idx[0].info.mode1.timing.role_specific_timing_value == 0x1234);
    CHECK(!m->has_packet_phase_correction_terms);
    x.data_len = 5;
    CHECK(!cs_step_parse_step(&ctx, &x, &y) && ctx.step_index == 1);
    x.data_len = 6; y.data_len = 7;
    CHECK(!cs_step_parse_step(&ctx, &x, &y) && ctx.step_index == 1);
    y.data_len = 6;
    uint8_t extended[14] = {0, 1, 201, 0, 0x80, 3, 0x01, 0, 0, 0, 0, 0x10, 0, 0};
    x.data = extended; x.data_len = sizeof(extended);
    CHECK(cs_step_parse_step(&ctx, &x, &y));
    CHECK(local_event.steps.idx[1].info.kind == MODE_ROLE_SPECIFIC_INFO_KIND_MODE1_INITIATOR_PBR_RTT);
    m = &local_event.steps.idx[1].info.mode1;
    CHECK(m->has_packet_phase_correction_terms);
    CHECK(m->timing.role_specific_timing_value == INT16_MIN);
    CHECK(m->packet_phase_correction_terms.first_phase_correction_term.i > 0);
    CHECK(m->packet_phase_correction_terms.second_phase_correction_term.q > 0);
    /* Reflector-local role is selected from the event, not array position. */
    local_event.origin = ORIGIN_REFLECTOR; peer_event.origin = ORIGIN_INITIATOR;
    CHECK(cs_step_parse_step(&ctx, &y, &y));
    CHECK(local_event.steps.idx[2].info.kind == MODE_ROLE_SPECIFIC_INFO_KIND_MODE1_REFLECTOR);
    CHECK(peer_event.steps.idx[2].info.kind == MODE_ROLE_SPECIFIC_INFO_KIND_MODE1_INITIATOR);
    local_event.origin = ORIGIN_INITIATOR; peer_event.origin = ORIGIN_REFLECTOR;
    CHECK(cs_step_parse_local_step(&ctx, &x));
    CHECK(peer_event.steps.idx[3].info.mode1.timing.kind == ROUND_TRIP_TIME_ROLE_TIMING_KIND_UNAVAILABLE);
    uint8_t inline_raw[9] = {1, 10, 6, 0x21, 7, 200, 0x2e, 0xfb, 1};
    struct net_buf_simple inline_buf;
    net_buf_simple_init_with_data(&inline_buf, inline_raw, sizeof(inline_raw));
    cs_step_parse_inline(&local_event, &peer_event, &inline_buf, BT_CONN_LE_CS_ROLE_INITIATOR);
    CHECK(local_event.step_count == 1 && peer_event.step_count == 1);
    CHECK(local_event.steps.idx[0].info.mode1.timing.role_specific_timing_value == -1234);
    CHECK(peer_event.steps.idx[0].info.mode1.timing.kind == ROUND_TRIP_TIME_ROLE_TIMING_KIND_UNAVAILABLE);
    register unsigned     op __asm("r0")   = 4;
    register const char * text __asm("r1") = "PASS: mode 0/1/2, RTT payloads, IPT unavailable peer, and subevent boundaries\n";
    __asm volatile("bkpt 0xab" : : "r"(op), "r"(text) : "memory");
    finish(0);
}
void Reset_Handler(void)
{
    *(volatile uint32_t *)0xe000ed88 |= 0xf << 20;
    __asm volatile("dsb; isb");
    test_main();
    finish(1);
}
__attribute__((section(".vectors"), used)) const uintptr_t vectors[] = {0x20400000, (uintptr_t)Reset_Handler};
