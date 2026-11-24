/* SPDX-License-Identifier: MIT */

#include "location.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(reflector_location, LOG_LEVEL_INF);

/* Coordinates are signed millimetres, exposed as decimal UTF-8 text for phone apps. */
#define LOCATION_UUID(value) BT_UUID_128_ENCODE(value, 0x7ea1, 0x4b73, 0x9bb1, 0x6f54940c3a10)
#define LOCATION_TEXT_SIZE   12 /* -2147483648 plus terminator */

static int32_t            coordinates[2];
static const char * const setting_keys[] = {"location/x", "location/y"};
static K_MUTEX_DEFINE(location_lock);

static int location_settings_set(const char * name, size_t len, settings_read_cb read_cb, void * cb_arg)
{
    int     axis;
    uint8_t stored[sizeof(int32_t)];

    if (strcmp(name, "x") == 0)
    {
        axis = 0;
    }
    else if (strcmp(name, "y") == 0)
    {
        axis = 1;
    }
    else
    {
        return -ENOENT;
    }

    if (len != sizeof(stored))
    {
        return -EINVAL;
    }

    int rc = read_cb(cb_arg, stored, sizeof(stored));
    if (rc < 0)
    {
        return rc;
    }
    if (rc != sizeof(stored))
    {
        return -EIO;
    }

    coordinates[axis] = (int32_t)sys_get_le32(stored);
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(location, "location", NULL, location_settings_set, NULL, NULL);

static ssize_t read_coordinate(
    struct bt_conn * conn, const struct bt_gatt_attr * attr, void * buf, uint16_t len, uint16_t offset)
{
    char            text[LOCATION_TEXT_SIZE];
    const int32_t * coordinate = attr->user_data;

    k_mutex_lock(&location_lock, K_FOREVER);
    int size = snprintf(text, sizeof(text), "%ld", (long)*coordinate);
    k_mutex_unlock(&location_lock);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, text, size);
}

static ssize_t write_coordinate(struct bt_conn *            conn,
                                const struct bt_gatt_attr * attr,
                                const void *                buf,
                                uint16_t                    len,
                                uint16_t                    offset,
                                uint8_t                     flags)
{
    ARG_UNUSED(conn);
    const uint8_t * text       = buf;
    int32_t *       coordinate = attr->user_data;
    bool            negative   = len > 0 && text[0] == '-';
    uint16_t        start      = negative ? 1 : 0;
    uint32_t        magnitude  = 0;
    uint32_t        limit      = negative ? (uint32_t)INT32_MAX + 1U : INT32_MAX;

    if (flags & BT_GATT_WRITE_FLAG_PREPARE)
    {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }
    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len <= start || len >= LOCATION_TEXT_SIZE)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    for (uint16_t i = start; i < len; i++)
    {
        if (text[i] < '0' || text[i] > '9')
        {
            return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
        }
        uint32_t digit = text[i] - '0';
        if (magnitude > (limit - digit) / 10U)
        {
            return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
        }
        magnitude = magnitude * 10U + digit;
    }

    int32_t value = negative ? (int32_t)(-(int64_t)magnitude) : (int32_t)magnitude;
    uint8_t stored[sizeof(value)];
    sys_put_le32((uint32_t)value, stored);

    /* Only acknowledge a write once it is durable; a failed save leaves RAM unchanged. */
    k_mutex_lock(&location_lock, K_FOREVER);
    int err = settings_save_one(setting_keys[coordinate - coordinates], stored, sizeof(stored));
    if (err == 0)
    {
        *coordinate = value;
    }
    k_mutex_unlock(&location_lock);
    if (err)
    {
        LOG_ERR("Coordinate save failed (err %d)", err);
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

BT_GATT_SERVICE_DEFINE(location_service,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_DECLARE_128(LOCATION_UUID(0xa00c0000))),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LOCATION_UUID(0xa00c0001)),
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_coordinate,
                                              write_coordinate,
                                              &coordinates[0]),
                       BT_GATT_CUD("x", BT_GATT_PERM_READ),
                       BT_GATT_CHARACTERISTIC(BT_UUID_DECLARE_128(LOCATION_UUID(0xa00c0002)),
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              read_coordinate,
                                              write_coordinate,
                                              &coordinates[1]),
                       BT_GATT_CUD("y", BT_GATT_PERM_READ));

int location_init(void)
{
    int err = settings_subsys_init();
    if (err)
    {
        return err;
    }

    return settings_load();
}
