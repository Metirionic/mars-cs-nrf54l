/* SPDX-License-Identifier: MIT */
#include "ras_worker.h"

#if IS_ENABLED(CONFIG_BT_RAS_RREQ)
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>

#include "serialize.h"
#include "subevent.h"

LOG_MODULE_DECLARE(app_main, LOG_LEVEL_INF);

/* A bounded snapshot lets Bluetooth receive while the worker parses and
 * serializes. Drop a whole new procedure if the slot is still owned. */
struct ras_procedure
{
    struct cs_local_subevent subevents[BT_RAS_MAX_STEPS_PER_PROCEDURE];
    uint8_t                  local_steps[LOCAL_PROCEDURE_MEM];
    uint8_t                  peer_steps[BT_RAS_PROCEDURE_MEM];
    size_t                   count;
    size_t                   local_len;
    size_t                   peer_len;
    uint64_t                 local_mac;
    uint64_t                 peer_mac;
    enum bt_conn_le_cs_role  role;
};

struct ras_work
{
    struct ras_procedure * procedure;
    char                   message[64];
};

K_MEM_SLAB_DEFINE(ras_procedures, sizeof(struct ras_procedure), 1, 8);
K_MSGQ_DEFINE(ras_work_queue, sizeof(struct ras_work), 8, 4);
static uint32_t backlog_drops;

bool ras_worker_submit(const struct cs_local_subevent * subevents,
                       size_t                           count,
                       const struct net_buf_simple *    local_steps,
                       const struct net_buf_simple *    peer_steps,
                       uint64_t                         local_mac,
                       uint64_t                         peer_mac,
                       enum bt_conn_le_cs_role          role)
{
    if (subevents == NULL || count == 0 || count > BT_RAS_MAX_STEPS_PER_PROCEDURE || local_steps == NULL ||
        peer_steps == NULL || local_steps->len > LOCAL_PROCEDURE_MEM || peer_steps->len > BT_RAS_PROCEDURE_MEM)
    {
        return false;
    }
    struct ras_procedure * procedure;
    if (k_mem_slab_alloc(&ras_procedures, (void **)&procedure, K_NO_WAIT) != 0)
    {
        backlog_drops++;
        if (backlog_drops == 1 || backlog_drops % 128 == 0)
        {
            LOG_WRN("RAS worker backlog: dropped %u whole procedures", backlog_drops);
        }
        return false;
    }
    procedure->count     = count;
    procedure->local_len = local_steps->len;
    procedure->peer_len  = peer_steps->len;
    procedure->local_mac = local_mac;
    procedure->peer_mac  = peer_mac;
    procedure->role      = role;
    memcpy(procedure->subevents, subevents, count * sizeof(*subevents));
    for (size_t i = 0; i < count; i++)
    {
        procedure->subevents[i].result.step_data_buf = NULL;
    }
    memcpy(procedure->local_steps, local_steps->data, local_steps->len);
    memcpy(procedure->peer_steps, peer_steps->data, peer_steps->len);
    const struct ras_work work = {.procedure = procedure};
    if (k_msgq_put(&ras_work_queue, &work, K_NO_WAIT) != 0)
    {
        k_mem_slab_free(&ras_procedures, procedure);
        LOG_WRN("RAS work queue full; dropping whole procedure");
        return false;
    }
    return true;
}

void ras_worker_log(const char * message)
{
    struct ras_work work = {0};
    snprintf(work.message, sizeof(work.message), "%s", message);
    if (k_msgq_put(&ras_work_queue, &work, K_NO_WAIT) != 0)
    {
        LOG_WRN("RAS work queue full; dropping diagnostic");
    }
}

void ras_worker_process(void)
{
    struct ras_work work;
    if (k_msgq_get(&ras_work_queue, &work, K_FOREVER) != 0)
    {
        return;
    }
    if (work.procedure == NULL)
    {
        (void)serialize_send_log_message(work.message);
        return;
    }
    struct ras_procedure * procedure = work.procedure;
    struct net_buf_simple  local, peer;
    net_buf_simple_init_with_data(&local, procedure->local_steps, procedure->local_len);
    net_buf_simple_init_with_data(&peer, procedure->peer_steps, procedure->peer_len);
    static SubeventResultEvent_t local_event, peer_event;
    if (!subevent_populate(&local_event,
                           &peer_event,
                           procedure->local_mac,
                           procedure->peer_mac,
                           procedure->subevents,
                           procedure->count,
                           &local,
                           &peer,
                           procedure->role,
                           serialize_run))
    {
        LOG_ERR("Could not decode all subevents for procedure %u",
                procedure->subevents[0].result.header.procedure_counter);
        (void)serialize_send_log_message("RAS subevent decode error");
    }
    k_mem_slab_free(&ras_procedures, procedure);
}
#endif
