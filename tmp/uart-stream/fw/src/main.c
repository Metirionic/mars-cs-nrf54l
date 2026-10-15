/* SPDX-License-Identifier: MIT */
/*
 * [DEBUG-ust1] UART-only stream test for the #190 COBS corruption hunt.
 *
 * Streams COBS frames carrying a sequence number, the device's own cumulative
 * wire-byte count, an uptime stamp, a payload the host can regenerate from the
 * sequence number alone, and a CRC32 -- over uart20, the production COBS UART
 * (P1.04 -> onboard Interface MCU -> VCOM1). No Bluetooth, no Rust serializer,
 * no CS: the UART path is the only thing under test.
 *
 * The burst geometry mirrors production: one uart_tx() of N concatenated COBS
 * frames, then an idle gap (~89% duty at 921600). See Kconfig for the knobs.
 *
 * Wire format of one frame, after COBS encoding, with a 0x00 delimiter:
 *
 *   off  size field         notes
 *   0    4    magic         'M','S','T','1' (0x4D535431, little-endian bytes)
 *   4    1    version       1
 *   5    1    flags         BIT(0) first frame of burst, BIT(1) last
 *   6    4    seq           frame sequence, starts at 1
 *   10   2    frame_len     total DECODED length, header + payload + crc
 *   12   8    sent_total    wire bytes handed to uart_tx BEFORE this frame
 *   20   N    payload       PRNG stream seeded by seq (host regenerates)
 *   ..   4    crc32         zlib/IEEE-802.3 CRC of all preceding frame bytes
 *
 * Every byte of a frame is a pure function of (seq, payload bytes), so the host
 * regenerates the exact expected wire stream from the capture's own frame_len
 * and burst flags and can diff it byte-for-byte. A device-side timestamp field
 * would break that (COBS expansion depends on where a field's zero bytes land,
 * so a never-received frame's wire length would be unknown).
 *
 * Remove with the whole tmp/uart-stream directory.
 */

#include <errno.h>
#include <string.h>

#include <zephyr/data/cobs.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ustream, LOG_LEVEL_INF);

#define MAGIC          0x4D535431u /* "MST1" */
#define VERSION        1u
#define HDR_LEN        20u
#define CRC_LEN        4u
/** Largest DECODED frame any burst can produce: the second event frame is
 *  PAYLOAD_DELTA_B bytes longer than the first, so frame_buf must be sized for
 *  that one, not for the base payload. Sizing it for the base let build_frame
 *  write 3 bytes past the end of frame_buf, which corrupted the following
 *  frames on the wire without any fault -- the buffer is the hard bound.
 */
#define FRAME_LEN      (HDR_LEN + CONFIG_STREAM_PAYLOAD_BYTES + \
                        CONFIG_STREAM_PAYLOAD_DELTA_B + CRC_LEN)
/** Frames per uart_tx: the configured event frames plus one status frame. */
#define BURST_FRAMES   (CONFIG_STREAM_DATA_FRAMES_PER_BURST + 1)
/** Decoded length of the status frame (header + filler + CRC). */
#define STATUS_FRAME_LEN (HDR_LEN + CONFIG_STREAM_STATUS_PAYLOAD + CRC_LEN)
/** Status frames carry a constant nonzero filler, so their wire length moves
 *  exactly 1:1 with CONFIG_STREAM_STATUS_PAYLOAD -- no PRNG zero-run jitter.
 */
#define STATUS_FILLER 0xA5u

#define FLAG_BURST_FIRST BIT(0)
#define FLAG_BURST_LAST  BIT(1)
#define FLAG_STATUS      BIT(2) /* the small status frame that ends a burst */

/** Worst-case COBS wire length: the bound Zephyr's cobs_max_encoded_len()
 *  computes (n + n/254 + 1) plus the trailing delimiter. A macro rather than
 *  that function because a static array bound needs a constant expression; the
 *  encoder sink enforces the capacity anyway, so an undersized buffer fails
 *  loudly (-ENOMEM) instead of corrupting memory.
 */
#define COBS_WIRE_MAX(n) ((n) + (n) / 254u + 2u)

#define BURST_CAP (CONFIG_STREAM_DATA_FRAMES_PER_BURST * COBS_WIRE_MAX(FRAME_LEN) + \
                   COBS_WIRE_MAX(STATUS_FRAME_LEN))

BUILD_ASSERT(CONFIG_STREAM_PAYLOAD_BYTES > 0, "payload must be non-empty");
BUILD_ASSERT(CONFIG_STREAM_DATA_FRAMES_PER_BURST > 0,
             "a burst needs at least one event frame");
BUILD_ASSERT(STATUS_FRAME_LEN <= FRAME_LEN, "frame_buf must hold the status frame too");
BUILD_ASSERT(BURST_FRAMES <= 4u, "burst_cap sizing below assumes at most 4 frames");

static const struct device *const gp_uart = DEVICE_DT_GET(DT_CHOSEN(cobs_uart));

/** TX-completion gate, given by the UART callback. Starts at 0: the loop
 *  waits on it *after* uart_tx() rather than before, so the wait is always for
 *  a transfer that is actually in flight and the buffer is never reused while
 *  the UARTE is still DMA-ing out of it.
 */
static K_SEM_DEFINE(sem_tx_done, 0, 1);

static uint8_t burst_buf[BURST_CAP];
static uint8_t frame_buf[FRAME_LEN];

static uint32_t crc_table[256];
static uint64_t sent_total; /* wire bytes handed to uart_tx so far */
static uint32_t seq_next = 1u;

/* ------------------------------------------------------------------ */
/* CRC32 (IEEE 802.3, reflected, init/final 0xFFFFFFFF) = zlib.crc32  */
/* ------------------------------------------------------------------ */

static void crc_table_init(void)
{
	for (uint32_t i = 0; i < 256u; i++)
	{
		uint32_t c = i;

		for (int k = 0; k < 8; k++)
		{
			c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
		}
		crc_table[i] = c;
	}
}

static uint32_t crc32_ieee(const uint8_t * p, size_t n)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < n; i++)
	{
		crc = crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* Payload generation -- the host mirrors this exactly                */
/* ------------------------------------------------------------------ */

static uint32_t prng_seed(uint32_t seq)
{
	return 0x9E3779B9u ^ (seq * 0x85EBCA6Bu);
}

static uint32_t xorshift32(uint32_t s)
{
	s ^= s << 13;
	s ^= s >> 17;
	s ^= s << 5;
	return s;
}

/** @brief Fill a payload.
 *
 * @param filler  0: the PRNG stream seeded by @p seq (event frames).
 *                Non-zero: that constant byte repeated (status frames), so the
 *                frame's wire length is a step function of its length alone.
 */
static void payload_fill(uint8_t * p_dst, size_t n, uint32_t seq, uint8_t filler)
{
	if (filler != 0u)
	{
		memset(p_dst, filler, n);
		return;
	}

	uint32_t state = prng_seed(seq);
	uint32_t word = 0u;

	for (size_t i = 0u; i < n; i++)
	{
		if ((i & 3u) == 0u)
		{
			state = xorshift32(state);
			word = state;
		}
		p_dst[i] = (uint8_t)(word >> (8u * (i & 3u)));
	}
}

static void put_u16(uint8_t * p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void put_u32(uint8_t * p, uint32_t v)
{
	for (int i = 0; i < 4; i++)
	{
		p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
	}
}

static void put_u64(uint8_t * p, uint64_t v)
{
	for (int i = 0; i < 8; i++)
	{
		p[i] = (uint8_t)((v >> (8 * i)) & 0xFFu);
	}
}

/** @brief Render the decoded frame for @p seq into frame_buf.
 *
 * @param payload_n  payload bytes; 0 for the status frame, which is header,
 *                   CRC and nothing else.
 */
static void build_frame(uint32_t seq, uint8_t flags, size_t payload_n, uint8_t filler)
{
	size_t frame_len = HDR_LEN + payload_n + CRC_LEN;

	__ASSERT(frame_len <= sizeof(frame_buf), "frame_len %u exceeds frame_buf %u",
	         (unsigned)frame_len, (unsigned)sizeof(frame_buf));

	put_u32(&frame_buf[0], MAGIC);
	frame_buf[4] = (uint8_t)VERSION;
	frame_buf[5] = flags;
	put_u32(&frame_buf[6], seq);
	put_u16(&frame_buf[10], (uint16_t)frame_len);
	put_u64(&frame_buf[12], sent_total);

	payload_fill(&frame_buf[HDR_LEN], payload_n, seq, filler);

	put_u32(&frame_buf[frame_len - CRC_LEN], crc32_ieee(frame_buf, frame_len - CRC_LEN));
}

/* ------------------------------------------------------------------ */
/* COBS encoding                                                      */
/* ------------------------------------------------------------------ */

struct cobs_out
{
	size_t len; /* bytes already in burst_buf; sink appends from here */
};

static int cobs_sink(const uint8_t * p_buf, size_t len, void * p_user_data)
{
	struct cobs_out * p_out = p_user_data;

	if (p_out->len + len > sizeof(burst_buf))
	{
		return -ENOMEM;
	}

	memcpy(&burst_buf[p_out->len], p_buf, len);
	p_out->len += len;
	return 0;
}

/** @brief COBS-encode the frame in frame_buf onto the end of burst_buf.
 *
 * @param p_fill  in: burst_buf bytes already used; out: bytes used after.
 */
static int burst_append_frame(uint32_t seq, uint8_t flags, size_t payload_n, uint8_t filler,
                              size_t * p_fill)
{
	build_frame(seq, flags, payload_n, filler);
	size_t frame_len = HDR_LEN + payload_n + CRC_LEN;

	struct cobs_out out = { .len = *p_fill };
	struct cobs_encoder enc;
	int err;

	err = cobs_encoder_init(&enc, cobs_sink, &out, COBS_FLAG_TRAILING_DELIMITER);
	if (err)
	{
		LOG_ERR("cobs_encoder_init: %d", err);
		return err;
	}

	err = cobs_encoder_write(&enc, frame_buf, frame_len);
	if (err < 0)
	{
		LOG_ERR("cobs_encoder_write: %d", err);
		return err;
	}
	if ((size_t)err != frame_len)
	{
		LOG_ERR("cobs_encoder_write consumed %d of %u bytes", err, (unsigned)frame_len);
		return -EIO;
	}

	err = cobs_encoder_close(&enc);
	if (err)
	{
		LOG_ERR("cobs_encoder_close: %d", err);
		return err;
	}

	*p_fill = out.len;
	return 0;
}

/* ------------------------------------------------------------------ */
/* UART                                                               */
/* ------------------------------------------------------------------ */

static void uart_async_cb(const struct device * p_dev, struct uart_event * p_evt, void * p_user_data)
{
	ARG_UNUSED(p_dev);
	ARG_UNUSED(p_user_data);

	switch (p_evt->type)
	{
	case UART_TX_DONE:
		k_sem_give(&sem_tx_done);
		break;
	case UART_TX_ABORTED:
		LOG_WRN("uart20 TX aborted (%u bytes)", (unsigned)p_evt->data.tx.len);
		k_sem_give(&sem_tx_done);
		break;
	default:
		break;
	}
}

int main(void)
{
	if (!device_is_ready(gp_uart))
	{
		LOG_ERR("uart20 (cobs-uart) not ready");
		return -ENODEV;
	}

	int err = uart_callback_set(gp_uart, uart_async_cb, NULL);
	if (err)
	{
		LOG_ERR("uart_callback_set: %d", err);
		return err;
	}

	crc_table_init();

	LOG_INF("[DEBUG-ust1] uart-stream payload=%u+%u frame=%u status=%u data/burst=%u gap=%ums",
	        (unsigned)CONFIG_STREAM_PAYLOAD_BYTES, (unsigned)CONFIG_STREAM_PAYLOAD_DELTA_B,
	        (unsigned)FRAME_LEN, (unsigned)STATUS_FRAME_LEN,
	        (unsigned)CONFIG_STREAM_DATA_FRAMES_PER_BURST, (unsigned)CONFIG_STREAM_GAP_MS);

	uint32_t bursts = 0u;
	uint32_t frames = 0u;
	int64_t t_stats = k_uptime_get();

	while ((CONFIG_STREAM_BURST_COUNT == 0) || (bursts < (uint32_t)CONFIG_STREAM_BURST_COUNT))
	{
		size_t fill = 0u;
		size_t frame_off[BURST_FRAMES];
		size_t frame_len_bytes[BURST_FRAMES];

		/* Traffic-pattern phase: 0 = one uart_tx per burst (production's
		 * shape), 1 = one uart_tx per frame. Alternating within a single
		 * run is the only way to compare them: the loss comes in episodes,
		 * so two runs at different times cannot be compared.
		 */
		uint32_t phase = 0u;
		if (CONFIG_STREAM_ALT_PERIOD > 0)
		{
			phase = (bursts / (uint32_t)CONFIG_STREAM_ALT_PERIOD) % 2u;
		}

		for (uint32_t i = 0u; i < (uint32_t)BURST_FRAMES; i++)
		{
			bool is_status = (i == (uint32_t)CONFIG_STREAM_DATA_FRAMES_PER_BURST);
			uint8_t flags = 0u;
			size_t before = fill;

			if (i == 0u)
			{
				flags |= FLAG_BURST_FIRST;
			}
			if (is_status)
			{
				/* Production ends each CS procedure's transfer with a
				 * small status frame; mine carries the burst's own
				 * accounting and no payload.
				 */
				flags |= FLAG_BURST_LAST | FLAG_STATUS;
			}

			/* build_frame() stamps this frame with sent_total as it
			 * stands now, i.e. the wire bytes of every earlier frame;
			 * only then does this frame's own wire length count.
			 */
			/* Second event frame is larger, as in production. */
			size_t payload_n = CONFIG_STREAM_PAYLOAD_BYTES;
			uint8_t filler = 0u;

			if (is_status)
			{
				/* The status payload is the phase marker, not a constant
				 * filler: phase+1 keeps it non-zero (so the frame's wire
				 * length is unchanged) while telling the host which
				 * traffic pattern was in force.
				 */
				payload_n = CONFIG_STREAM_STATUS_PAYLOAD;
				if (payload_n > 0u)
				{
					filler = (uint8_t)(phase + 1u);
				}
			}
			else if (i == 1u)
			{
				payload_n += CONFIG_STREAM_PAYLOAD_DELTA_B;
			}

			frame_off[i] = fill;
			err = burst_append_frame(seq_next, flags, payload_n, filler, &fill);
			frame_len_bytes[i] = fill - frame_off[i];
			if (err)
			{
				LOG_ERR("burst_append_frame(seq %u): %d", seq_next, err);
				k_sem_give(&sem_tx_done);
				return err;
			}

			sent_total += (fill - before);
			seq_next++;
		}

		/* Started once per iteration and waited for once per iteration, so
		 * the semaphore is never over- or under-taken. (An earlier version
		 * took it both before building and after transmitting, which left
		 * the next iteration blocking forever -- one burst per boot.)
		 */
		/* All frames are built first either way, so the only thing that
		 * differs between the phases is the transfer granularity -- not the
		 * build timing.
		 */
		if (phase == 0u)
		{
			err = uart_tx(gp_uart, burst_buf, fill, SYS_FOREVER_MS);
			if (err)
			{
				LOG_ERR("uart_tx(%u): %d", (unsigned)fill, err);
			}
			else
			{
				k_sem_take(&sem_tx_done, K_FOREVER);
			}
		}
		else
		{
			for (uint32_t i = 0u; i < (uint32_t)BURST_FRAMES; i++)
			{
				err = uart_tx(gp_uart, &burst_buf[frame_off[i]], frame_len_bytes[i],
				              SYS_FOREVER_MS);
				if (err)
				{
					LOG_ERR("uart_tx[%u](%u): %d", i, (unsigned)frame_len_bytes[i], err);
					break;
				}
				k_sem_take(&sem_tx_done, K_FOREVER);
			}
		}

		bursts++;
		frames += (uint32_t)BURST_FRAMES;

		/* The idle gap belongs AFTER the transfer, not alongside it: sleeping
		 * while the UARTE is still shifting bytes just overlaps the two and
		 * the line never goes quiet, which ran this test at ~99% duty where
		 * production runs 89%.
		 */
		if (CONFIG_STREAM_GAP_MS > 0)
		{
			k_msleep(CONFIG_STREAM_GAP_MS);
		}

		if ((CONFIG_STREAM_STATS_PERIOD_S > 0) &&
		    (k_uptime_get() - t_stats >= (int64_t)CONFIG_STREAM_STATS_PERIOD_S * 1000))
		{
			t_stats = k_uptime_get();
			LOG_INF("[DEBUG-ust1] bursts=%u frames=%u wire=%llu", bursts, frames,
			        (unsigned long long)sent_total);
		}
	}

	LOG_INF("[DEBUG-ust1] done: bursts=%u frames=%u wire=%llu", bursts, frames,
	        (unsigned long long)sent_total);
	return 0;
}
