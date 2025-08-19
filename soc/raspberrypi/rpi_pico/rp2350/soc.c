/*
 * Copyright (c) 2024 Andrew Featherstone
 * Copyright (c) 2025 Dan Collins <dan@collinsnz.com>
 * Copyright (c) 2025 Dmitrii Sharshakov <d3dx12.xx@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for Raspberry Pi RP235xx MCUs
 *
 * This module provides routines to initialize and support board-level hardware
 * for the Raspberry Pi RP235xx (RP2350A, RP2350B, RP2354A, RP2354B).
 */

#if CONFIG_SOC_RESET_HOOK
#include <pico/runtime_init.h>


void soc_reset_hook(void)
{
	runtime_init_per_core_enable_coprocessors();
}

#endif /* CONFIG_SOC_RESET_HOOK */

/* TODO: move this to soc/raspberrypi/rpi_pico/common/soc.c when tested on RP2040 */
#if defined(CONFIG_SOC_RP2350_CPU1_ENABLE)

#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>

#include <hardware/structs/sio.h>
#include <hardware/structs/psm.h>

LOG_MODULE_REGISTER(soc_rp2350, CONFIG_SOC_LOG_LEVEL);

#define CPU1_SRAM_ADDR DT_REG_ADDR(DT_NODELABEL(sram0_cpu1))
#define CPU1_SRAM_SIZE DT_REG_SIZE(DT_NODELABEL(sram0_cpu1))

static inline void rpi_pico_mailbox_put_blocking(sio_hw_t *const sio_regs, uint32_t value)
{
	while (!(sio_regs->fifo_st & SIO_FIFO_ST_RDY_BITS)) {
		k_busy_wait(1);
	}

	sio_regs->fifo_wr = value;

	/* Inform other CPU about FIFO update. */
	__SEV();
}

static inline uint32_t rpi_pico_mailbox_pop_blocking(sio_hw_t *const sio_regs)
{
	while (!(sio_regs->fifo_st & SIO_FIFO_ST_VLD_BITS)) {
		/*
		 * Wait for a message to be available in the FIFO.
		 * Before IRQ is enables, this is signalled by an event
		 */
		__WFE();
	}

	return sio_regs->fifo_rd;
}

#if DT_NODE_EXISTS(DT_NODELABEL(cpu1_slot0_partition))
static void rpi_pico_load_cpu1_image(void)
{
#if DT_NODE_HAS_COMPAT(DT_GPARENT(DT_NODELABEL(cpu1_slot0_partition)), soc_nv_flash)
/* Flash partitions have addresses relative to the flash base */
#define CPU1_CODE_ADDR                                                                             \
	DT_REG_ADDR(DT_GPARENT(DT_NODELABEL(cpu1_slot0_partition))) +                              \
		DT_REG_ADDR(DT_NODELABEL(cpu1_slot0_partition))
#else
/* Code in RAM, e.g. a section or debug build */
#define CPU1_CODE_ADDR DT_REG_ADDR(DT_NODELABEL(cpu1_slot0_partition))
#endif /* DT_NODE_HAS_COMPAT(DT_GPARENT(DT_NODELABEL(cpu1_slot0_partition)), soc_nv_flash) */
#define CPU1_CODE_SIZE DT_REG_SIZE(DT_NODELABEL(cpu1_slot0_partition))

	BUILD_ASSERT((CPU1_SRAM_SIZE >= CPU1_CODE_SIZE),
		     "Image size must not exceed execution memory size");

	BUILD_ASSERT(((CPU1_SRAM_ADDR >= CPU1_CODE_ADDR + CPU1_CODE_SIZE) ||
		      (CPU1_CODE_ADDR >= CPU1_SRAM_ADDR + CPU1_SRAM_SIZE)),
		     "Image source memory must not overlap with execution memory");

	void *src_mem = (void *)CPU1_CODE_ADDR;
	void *exec_mem = (void *)CPU1_SRAM_ADDR;

	LOG_DBG("Copying image from %p to %p", src_mem, exec_mem);

	memcpy(exec_mem, src_mem, MIN(CPU1_SRAM_SIZE, CPU1_CODE_SIZE));
}
#endif /* DT_NODE_EXISTS(DT_NODELABEL(cpu1_slot0_partition)) */

#if CONFIG_SOC_RP2350_CPU1_ENABLE_CHECK_VTOR
static inline bool address_in_range(uint32_t addr, uint32_t base, uint32_t size)
{
	return addr >= base && addr < base + size;
}

static inline int rpi_pico_validate_vtor(uint32_t cpu1_sp, uint32_t cpu1_pc)
{
	/* Stack pointer shall point within RAM assigned to the core. */
	if (!address_in_range(cpu1_sp, CPU1_SRAM_ADDR, CPU1_SRAM_SIZE)) {
		LOG_ERR("CPU1 stack pointer 0x%08x invalid.", cpu1_sp);
		return -EINVAL;
	}

	LOG_DBG("CPU1 stack pointer: 0x%08x", cpu1_sp);

	/* Initial program counter shall point to the loaded CPU1 code. */
	if (!address_in_range(cpu1_pc, CPU1_SRAM_ADDR, CPU1_SRAM_SIZE)) {
		LOG_ERR("CPU1 reset pointer 0x%08x invalid.", cpu1_pc);
		return -EINVAL;
	}

	LOG_DBG("CPU1 reset pointer: 0x%08x", cpu1_pc);
	return 0;
}
#endif /* CONFIG_SOC_RP2350_CPU1_ENABLE_CHECK_VTOR */

static int rpi_pico_reset_cpu1(sio_hw_t *const sio_regs, psm_hw_t *const psm_regs)
{
	uint32_t val;

	/* Power off, and wait for it to take effect. */
	hw_set_bits(&psm_regs->frce_off, PSM_FRCE_OFF_PROC1_BITS);
	while (!(psm_regs->frce_off & PSM_FRCE_OFF_PROC1_BITS)) {
		k_busy_wait(1);
	}

	/*
	 * Power back on, and we can wait for a '0' in the FIFO to know
	 * that it has come back.
	 */
	hw_clear_bits(&psm_regs->frce_off, PSM_FRCE_OFF_PROC1_BITS);
	val = rpi_pico_mailbox_pop_blocking(sio_regs);

	return val == 0 ? 0 : -EIO;
}

K_MSGQ_DEFINE(ip_msgq, sizeof(int), 4, 1);

static void platform_ipm_callback(const struct device *dev, void *context,
				uint32_t id, volatile void *data)
{
	LOG_DBG("%s: msg received from mb %d: 0x%p (%d)\n", __func__, id, data, *(int *)data);
	k_msgq_put(&ip_msgq, (const void *)data, K_NO_WAIT);
}

static const struct device *const ipm_handle =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static void rpi_pico_boot_cpu1(uint32_t vector_table_addr,
			uint32_t stack_ptr, uint32_t pc)
{
	/* We synchronise with CPU1 and then we can hand over the memory addresses. */
	uint32_t cmds[] = {0, 0, 1, vector_table_addr, stack_ptr, pc};

	int i = 0;
	while (i < sizeof(cmds) / sizeof(cmds[0])) {
		int recv;

		ipm_send(ipm_handle, 0, 0, &cmds[i], sizeof(cmds[i]));
		k_msgq_get(&ip_msgq, &recv, K_FOREVER);
		i = cmds[i] == recv ? i + 1 : 0;
	}
}

void soc_late_init_hook(void)
{
	int status;
#if DT_NODE_EXISTS(DT_NODELABEL(cpu1_slot0_partition))
	rpi_pico_load_cpu1_image();
#endif /* DT_NODE_EXISTS(DT_NODELABEL(cpu1_slot0_partition)) */

	uint32_t cpu1_image_base = CPU1_SRAM_ADDR;

	uint32_t *cpu1_vector_table = (void *)cpu1_image_base;
	uint32_t cpu1_sp = cpu1_vector_table[0];
	uint32_t cpu1_pc = cpu1_vector_table[1];

#if CONFIG_SOC_RP2350_CPU1_ENABLE_CHECK_VTOR
	if (rpi_pico_validate_vtor(cpu1_sp, cpu1_pc) != 0) {
		return;
	}
#endif /* CONFIG_SOC_RP2350_CPU1_ENABLE_CHECK_VTOR */

	/* setup IPM */
	if (!device_is_ready(ipm_handle)) {
		LOG_ERR("IPM device is not ready\n");
		return;
	}
	ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
	status = ipm_set_enabled(ipm_handle, 1);
	if (status) {
		LOG_ERR("ipm_set_enabled failed\n");
		return;
	}

	LOG_DBG("Launching CPU1 with vector table at 0x%p", (void *)cpu1_vector_table);

	if (rpi_pico_reset_cpu1(sio_hw, psm_hw) != 0) {
		LOG_ERR("CPU1 reset failed.");
		return;
	}

	rpi_pico_boot_cpu1((uint32_t)cpu1_vector_table, cpu1_sp, cpu1_pc);
}
#endif /* defined(CONFIG_SOC_RP2350_CPU1_ENABLE) */
