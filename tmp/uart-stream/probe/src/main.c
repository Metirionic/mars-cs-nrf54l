/* SPDX-License-Identifier: MIT
 * [DEBUG-ust1] Does a freshly-rewritten TX buffer survive EasyDMA?
 *
 * Phase A: fill 0x41 once at boot, then burst 39,950 B every 36 ms.
 * Phase B: refill 0x42 immediately before each burst, same cadence.
 * If A arrives and B does not, the CPU data cache is not coherent with
 * EasyDMA -- and any firmware that serializes into a buffer and transmits it
 * straight away (initiator/src/serialize.c included) feeds the UART stale RAM.
 */
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static const struct device *const u20 = DEVICE_DT_GET(DT_NODELABEL(uart20));
static K_SEM_DEFINE(done, 0, 1);
static uint8_t buf[40000];

static void cb(const struct device *dev, struct uart_event *evt, void *ud)
{
	ARG_UNUSED(dev); ARG_UNUSED(ud);
	if (evt->type == UART_TX_DONE || evt->type == UART_TX_ABORTED) {
		k_sem_give(&done);
	}
}

static void phase(const char *name, uint8_t fill)
{
	printk("[DEBUG-ust1] PHASE %s start\n", name);
	for (int i = 0; i < 20; i++) {
		if (fill) {
			for (size_t k = 0; k < sizeof(buf); k++) {
				buf[k] = fill;
			}
		}
		k_sem_reset(&done);
		if (uart_tx(u20, buf, sizeof(buf), SYS_FOREVER_MS)) {
			printk("[DEBUG-ust1] tx err\n");
			return;
		}
		k_sem_take(&done, K_FOREVER);
		k_msleep(36);
	}
	printk("[DEBUG-ust1] PHASE %s end\n", name);
}

int main(void)
{
	printk("[DEBUG-ust1] cache probe boot\n");
	printk("[DEBUG-ust1] cb reg=%d\n", uart_callback_set(u20, cb, NULL));

	for (size_t k = 0; k < sizeof(buf); k++) {
		buf[k] = 0x41;
	}
	phase("A-static", 0);
	k_msleep(1000);
	phase("B-rewrite-each", 0x42);
	k_msleep(1000);
	printk("[DEBUG-ust1] probe done\n");
	return 0;
}
