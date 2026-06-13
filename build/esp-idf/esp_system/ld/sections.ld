/* Automatically generated file; DO NOT EDIT */
/* Espressif IoT Development Framework Linker Script */
/* Generated from: C:\Users\mati\Documents\GitHub\M5MonsterC5-Tab5\build\esp-idf\esp_system\ld\sections.ld.in */

/*

 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD

 *

 * SPDX-License-Identifier: Apache-2.0

 */
/*

 * Automatically generated file. DO NOT EDIT.

 * Espressif IoT Development Framework (ESP-IDF) 5.4.1 Configuration Header

 */
       
/* List of deprecated options */
/*

 * SPDX-FileCopyrightText: 2021-2024 Espressif Systems (Shanghai) CO LTD

 *

 * SPDX-License-Identifier: Apache-2.0

 */
/* CPU instruction prefetch padding size for flash mmap scenario */
/*

 * PMP region granularity size

 * Software may determine the PMP granularity by writing zero to pmp0cfg, then writing all ones

 * to pmpaddr0, then reading back pmpaddr0. If G is the index of the least-significant bit set,

 * the PMP granularity is 2^G+2 bytes.

 */
/* CPU instruction prefetch padding size for memory protection scenario */
/* Memory alignment size for PMS */
    /* rtc timer data (s_rtc_timer_retain_mem, see esp_clk.c files). For rtc_timer_data_in_rtc_mem section. */
/* Default entry point */
ENTRY(call_start_cpu0);
SECTIONS
{
  /**

   * RTC fast memory holds RTC wake stub code,

   * including from any source file named rtc_wake_stub*.c

   */
  .rtc.text :
  {
    /* Align the start of RTC code region as per PMP granularity

     * this ensures we do not overwrite the permissions for the previous

     * region (ULP mem/RTC reserved) regardless of their end alignment

     */
    
 . = ALIGN(128); 
 _rtc_fast_start = ABSOLUTE(.);
    
 . = ALIGN(128); 
 _rtc_text_start = ABSOLUTE(.);
    *(.rtc.literal .rtc.text .rtc.text.*)
    *rtc_wake_stub*.*(.text .text.*)
    *(.rtc_text_end_test)
    /* Align the end of RTC code region as per PMP granularity */
    . = ALIGN(128);
    
 . = ALIGN(4); 
 _rtc_text_end = ABSOLUTE(.);
  } > lp_ram_seg
  /**

   * This section located in RTC FAST Memory area.

   * It holds data marked with RTC_FAST_ATTR attribute.

   * See the file "esp_attr.h" for more information.

   */
  .rtc.force_fast :
  {
    
 . = ALIGN(4); 
 _rtc_force_fast_start = ABSOLUTE(.);
    _coredump_rtc_fast_start = ABSOLUTE(.);
    *(.rtc.fast.coredump .rtc.fast.coredump.*)
    _coredump_rtc_fast_end = ABSOLUTE(.);
    *(.rtc.force_fast .rtc.force_fast.*)
    
 . = ALIGN(4); 
 _rtc_force_fast_end = ABSOLUTE(.);
  } > lp_ram_seg
  /**

   * RTC data section holds RTC wake stub

   * data/rodata, including from any source file

   * named rtc_wake_stub*.c and the data marked with

   * RTC_DATA_ATTR, RTC_RODATA_ATTR attributes.

   */
  .rtc.data :
  {
    _rtc_data_start = ABSOLUTE(.);
    _coredump_rtc_start = ABSOLUTE(.);
    *(.rtc.coredump .rtc.coredump.*)
    _coredump_rtc_end = ABSOLUTE(.);
    *(.rtc.data .rtc.data.*)
    *(.rtc.rodata .rtc.rodata.*)
    *rtc_wake_stub*.*(.data .rodata .data.* .rodata.* .srodata.*)
    _rtc_data_end = ABSOLUTE(.);
  } > lp_ram_seg
  /* RTC bss, from any source file named rtc_wake_stub*.c */
  .rtc.bss (NOLOAD) :
  {
    _rtc_bss_start = ABSOLUTE(.);
    *rtc_wake_stub*.*(.bss .bss.* .sbss .sbss.*)
    *rtc_wake_stub*.*(COMMON)
    *(.rtc.bss)
    _rtc_bss_end = ABSOLUTE(.);
  } > lp_ram_seg
  /**

   * This section holds data that should not be initialized at power up

   * and will be retained during deep sleep.

   * User data marked with RTC_NOINIT_ATTR will be placed

   * into this section. See the file "esp_attr.h" for more information.

   */
  .rtc_noinit (NOLOAD):
  {
    
 . = ALIGN(4); 
 _rtc_noinit_start = ABSOLUTE(.);
    *(.rtc_noinit .rtc_noinit.*)
    
 . = ALIGN(4); 
 _rtc_noinit_end = ABSOLUTE(.);
  } > lp_ram_seg
  /**

   * This section located in RTC SLOW Memory area.

   * It holds data marked with RTC_SLOW_ATTR attribute.

   * See the file "esp_attr.h" for more information.

   */
  .rtc.force_slow :
  {
    
 . = ALIGN(4); 
 _rtc_force_slow_start = ABSOLUTE(.);
    *(.rtc.force_slow .rtc.force_slow.*)
    
 . = ALIGN(4); 
 _rtc_force_slow_end = ABSOLUTE(.);
  } > lp_ram_seg
  /**

   * This section holds RTC data that should have fixed addresses.

   * The data are not initialized at power-up and are retained during deep

   * sleep.

   */
  .rtc_reserved (NOLOAD):
  {
    
 . = ALIGN(4); 
 _rtc_reserved_start = ABSOLUTE(.);
    KEEP(*(.bootloader_data_rtc_mem .bootloader_data_rtc_mem.*))
    *(.rtc_timer_data_in_rtc_mem .rtc_timer_data_in_rtc_mem.*)
    /**

     * New data can only be added here to ensure existing data are not moved.

     * Because data have adhered to the beginning of the segment and code is relied

     * on it.

     * >> put new data here <<

     */
    _rtc_reserved_end = ABSOLUTE(.);
  } > rtc_reserved_seg
  _rtc_reserved_length = _rtc_reserved_end - _rtc_reserved_start;
  _rtc_ulp_memory_start = _rtc_reserved_start + LENGTH(rtc_reserved_seg);
  ASSERT((_rtc_reserved_length <= LENGTH(rtc_reserved_seg)),
          "RTC reserved segment data does not fit.")
  /* Get size of rtc slow data based on rtc_data_location alias */
  _rtc_slow_length = (ORIGIN(rtc_slow_seg) == ORIGIN(rtc_data_location))
                        ? (_rtc_force_slow_end - _rtc_data_start)
                        : (_rtc_force_slow_end - _rtc_force_slow_start);
  _rtc_fast_length = (ORIGIN(rtc_slow_seg) == ORIGIN(rtc_data_location))
                        ? (_rtc_force_fast_end - _rtc_fast_start)
                        : (_rtc_noinit_end - _rtc_fast_start);
  ASSERT((_rtc_slow_length <= LENGTH(rtc_slow_seg)),
          "RTC_SLOW segment data does not fit.")
  ASSERT((_rtc_fast_length <= LENGTH(rtc_data_seg)),
          "RTC_FAST segment data does not fit.")
  .tcm.text :
  {
    /* Code marked as running out of TCM */
    _tcm_text_start = ABSOLUTE(.);
    *(.tcm.text .tcm.text.*)
    _tcm_text_end = ABSOLUTE(.);
  } > tcm_idram_seg
  .tcm.data :
  {
    _tcm_data_start = ABSOLUTE(.);
    *(.tcm.data .tcm.data.*)
    _tcm_data_end = ABSOLUTE(.);
  } > tcm_idram_seg
  .iram0.text :
  {
    _iram_start = ABSOLUTE(.);
    /* Vectors go to start of IRAM */
    ASSERT(ABSOLUTE(.) % 0x40 == 0, "vector address must be 64 byte aligned");
    KEEP(*(.exception_vectors_table.text));
    KEEP(*(.exception_vectors.text));
    
 . = ALIGN(4); 
 _invalid_pc_placeholder = ABSOLUTE(.);
    /* Code marked as running out of IRAM */
    _iram_text_start = ABSOLUTE(.);
    *(.iram1 .iram1.*)
    *libapp_trace.a:app_trace.*(.literal .literal.* .text .text.*)
    *libapp_trace.a:app_trace_util.*(.literal .literal.* .text .text.*)
    *libapp_trace.a:port_uart.*(.literal .literal.* .text .text.*)
    *libclang_rt.builtins.a:_divsf3.*(.literal .literal.* .text .text.*)
    *libclang_rt.builtins.a:restore.*(.literal .literal.* .text .text.*)
    *libclang_rt.builtins.a:save.*(.literal .literal.* .text .text.*)
    *libesp_driver_gptimer.a:gptimer.*(.literal.gptimer_default_isr .text.gptimer_default_isr)
    *libesp_event.a:default_event_loop.*(.literal.esp_event_isr_post .text.esp_event_isr_post)
    *libesp_event.a:esp_event.*(.literal.esp_event_isr_post_to .text.esp_event_isr_post_to)
    *libesp_hw_support.a:clk_utils.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_compare_and_set .text.esp_cpu_compare_and_set)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_reset .text.esp_cpu_reset)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_stall .text.esp_cpu_stall)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_unstall .text.esp_cpu_unstall)
    *libesp_hw_support.a:cpu.*(.literal.esp_cpu_wait_for_intr .text.esp_cpu_wait_for_intr)
    *libesp_hw_support.a:dma2d.*(.literal._dma2d_default_rx_isr .text._dma2d_default_rx_isr)
    *libesp_hw_support.a:dma2d.*(.literal._dma2d_default_tx_isr .text._dma2d_default_tx_isr)
    *libesp_hw_support.a:dma2d.*(.literal.acquire_free_channels_for_trans .text.acquire_free_channels_for_trans)
    *libesp_hw_support.a:dma2d.*(.literal.dma2d_default_isr .text.dma2d_default_isr)
    *libesp_hw_support.a:dma2d.*(.literal.free_up_channels .text.free_up_channels)
    *libesp_hw_support.a:dw_gdma.*(.literal.dw_gdma_channel_default_isr .text.dw_gdma_channel_default_isr)
    *libesp_hw_support.a:esp_ldo_regulator.*(.literal.esp_ldo_acquire_channel .text.esp_ldo_acquire_channel)
    *libesp_hw_support.a:esp_memory_utils.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:gdma.*(.literal.gdma_append .text.gdma_append)
    *libesp_hw_support.a:gdma.*(.literal.gdma_default_rx_isr .text.gdma_default_rx_isr)
    *libesp_hw_support.a:gdma.*(.literal.gdma_default_tx_isr .text.gdma_default_tx_isr)
    *libesp_hw_support.a:gdma.*(.literal.gdma_reset .text.gdma_reset)
    *libesp_hw_support.a:gdma.*(.literal.gdma_start .text.gdma_start)
    *libesp_hw_support.a:gdma.*(.literal.gdma_stop .text.gdma_stop)
    *libesp_hw_support.a:mspi_timing_by_dqs.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:mspi_timing_config.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:mspi_timing_tuning.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:periph_ctrl.*(.literal.periph_module_reset .text.periph_module_reset)
    *libesp_hw_support.a:pmu_init.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:pmu_param.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:pmu_sleep.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:rtc_clk.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:rtc_time.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:sar_periph_ctrl.*(.literal.sar_periph_ctrl_power_enable .text.sar_periph_ctrl_power_enable)
    *libesp_hw_support.a:sleep_console.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:sleep_usb.*(.literal .literal.* .text .text.*)
    *libesp_hw_support.a:systimer.*(.literal .literal.* .text .text.*)
    *libesp_mm.a:esp_cache.*(.literal .literal.* .text .text.*)
    *libesp_mm.a:esp_mmu_map.*(.literal.esp_mmu_map_init .text.esp_mmu_map_init)
    *libesp_mm.a:ext_mem_layout.*(.literal .literal.* .text .text.*)
    *libesp_psram.a:esp_psram.*(.literal.esp_psram_init .text.esp_psram_init)
    *libesp_psram.a:esp_psram_impl_ap_hex.*(.literal .literal.* .text .text.*)
    *libesp_psram.a:mmu_psram_flash_v2.*(.literal .literal.* .text .text.*)
    *libesp_ringbuf.a:(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_print.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_spiflash.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_sys.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_systimer.*(.literal .literal.* .text .text.*)
    *libesp_rom.a:esp_rom_wdt.*(.literal .literal.* .text .text.*)
    *libesp_system.a:esp_err.*(.literal .literal.* .text .text.*)
    *libesp_system.a:esp_system_chip.*(.literal.esp_system_abort .text.esp_system_abort)
    *libesp_system.a:image_process.*(.literal .literal.* .text .text.*)
    *libesp_system.a:ubsan.*(.literal .literal.* .text .text.*)
    *libfreertos.a:(EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:freertos_compatibility.* *libfreertos.a:idf_additions.* *libfreertos.a:tasks.*) .literal EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:freertos_compatibility.* *libfreertos.a:idf_additions.* *libfreertos.a:tasks.*) .literal.* EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:freertos_compatibility.* *libfreertos.a:idf_additions.* *libfreertos.a:tasks.*) .text EXCLUDE_FILE(*libfreertos.a:app_startup.* *libfreertos.a:freertos_compatibility.* *libfreertos.a:idf_additions.* *libfreertos.a:tasks.*) .text.*)
    *libfreertos.a:tasks.*(.text .text.__getreent .text.eTaskGetState .text.pcTaskGetName .text.prvAddCurrentTaskToDelayedList .text.prvAddNewTaskToReadyList .text.prvDeleteTCB .text.prvIdleTask .text.prvInitialiseNewTask.constprop.0 .text.prvIsYieldRequiredSMP .text.prvListTasksWithinSingleList.part.0 .text.prvReleaseKernelLock .text.prvSearchForNameWithinSingleList .text.prvTakeKernelLock .text.prvTaskCreateDynamicPinnedToCoreWithCaps .text.prvTaskPriorityRaise .text.prvTaskPriorityRestore .text.pvTaskGetCurrentTCBForCore .text.pvTaskGetThreadLocalStoragePointer .text.pvTaskIncrementMutexHeldCount .text.pxTaskGetStackStart .text.ulTaskGenericNotifyTake .text.ulTaskGenericNotifyValueClear .text.ulTaskGetIdleRunTimeCounter .text.ulTaskGetIdleRunTimeCounterForCore .text.ulTaskGetIdleRunTimePercent .text.ulTaskGetIdleRunTimePercentForCore .text.uxTaskGetNumberOfTasks .text.uxTaskGetStackHighWaterMark .text.uxTaskGetStackHighWaterMark2 .text.uxTaskGetSystemState .text.uxTaskGetTaskNumber .text.uxTaskPriorityGet .text.uxTaskPriorityGetFromISR .text.uxTaskResetEventItemValue .text.vTaskDelay .text.vTaskDelete .text.vTaskEndScheduler .text.vTaskGenericNotifyGiveFromISR .text.vTaskGetInfo .text.vTaskGetRunTimeStats .text.vTaskGetSnapshot .text.vTaskInternalSetTimeOutState .text.vTaskList .text.vTaskMissedYield .text.vTaskPlaceOnEventList .text.vTaskPlaceOnEventListRestricted .text.vTaskPlaceOnUnorderedEventList .text.vTaskPriorityDisinheritAfterTimeout .text.vTaskPrioritySet .text.vTaskRemoveFromUnorderedEventList .text.vTaskResume .text.vTaskSetTaskNumber .text.vTaskSetThreadLocalStoragePointer .text.vTaskSetThreadLocalStoragePointerAndDelCallback .text.vTaskSetTimeOutState .text.vTaskStartScheduler .text.vTaskSuspend .text.vTaskSuspendAll .text.vTaskSwitchContext .text.xTaskAbortDelay .text.xTaskCatchUpTicks .text.xTaskCheckForTimeOut .text.xTaskCreatePinnedToCore .text.xTaskCreateStaticPinnedToCore .text.xTaskDelayUntil .text.xTaskGenericNotify .text.xTaskGenericNotifyFromISR .text.xTaskGenericNotifyStateClear .text.xTaskGenericNotifyWait .text.xTaskGetCoreID .text.xTaskGetCurrentTaskHandle .text.xTaskGetCurrentTaskHandleForCore .text.xTaskGetHandle .text.xTaskGetIdleTaskHandle .text.xTaskGetIdleTaskHandleForCore .text.xTaskGetSchedulerState .text.xTaskGetStaticBuffers .text.xTaskGetTickCount .text.xTaskGetTickCountFromISR .text.xTaskIncrementTick .text.xTaskIncrementTickOtherCores .text.xTaskPriorityDisinherit .text.xTaskPriorityInherit .text.xTaskRemoveFromEventList .text.xTaskResumeAll .text.xTaskResumeFromISR .text.xTimerCreateTimerTask)
    *libgcc.a:_divsf3.*(.literal .literal.* .text .text.*)
    *libgcc.a:lib2funcs.*(.literal .literal.* .text .text.*)
    *libgcc.a:save-restore.*(.literal .literal.* .text .text.*)
    *libgcov.a:(.literal .literal.* .text .text.*)
    *libhal.a:cache_hal.*(.literal .literal.* .text .text.*)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_append .text.gdma_ahb_hal_append)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_clear_intr .text.gdma_ahb_hal_clear_intr)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_get_eof_desc_addr .text.gdma_ahb_hal_get_eof_desc_addr)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_read_intr_status .text.gdma_ahb_hal_read_intr_status)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_reset .text.gdma_ahb_hal_reset)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_start_with_desc .text.gdma_ahb_hal_start_with_desc)
    *libhal.a:gdma_hal_ahb_v2.*(.literal.gdma_ahb_hal_stop .text.gdma_ahb_hal_stop)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_append .text.gdma_axi_hal_append)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_clear_intr .text.gdma_axi_hal_clear_intr)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_get_eof_desc_addr .text.gdma_axi_hal_get_eof_desc_addr)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_read_intr_status .text.gdma_axi_hal_read_intr_status)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_reset .text.gdma_axi_hal_reset)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_start_with_desc .text.gdma_axi_hal_start_with_desc)
    *libhal.a:gdma_hal_axi.*(.literal.gdma_axi_hal_stop .text.gdma_axi_hal_stop)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_append .text.gdma_hal_append)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_clear_intr .text.gdma_hal_clear_intr)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_get_eof_desc_addr .text.gdma_hal_get_eof_desc_addr)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_read_intr_status .text.gdma_hal_read_intr_status)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_reset .text.gdma_hal_reset)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_start_with_desc .text.gdma_hal_start_with_desc)
    *libhal.a:gdma_hal_top.*(.literal.gdma_hal_stop .text.gdma_hal_stop)
    *libhal.a:gpio_hal.*(.literal.gpio_hal_isolate_in_sleep .text.gpio_hal_isolate_in_sleep)
    *libhal.a:i2c_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:ledc_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:mmu_hal.*(.literal .literal.* .text .text.*)
    *libhal.a:pmu_hal.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_encrypt_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_hal_gpspi.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_flash_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:spi_slave_hal_iram.*(.literal .literal.* .text .text.*)
    *libhal.a:timer_hal.*(.literal.timer_hal_capture_and_get_counter_value .text.timer_hal_capture_and_get_counter_value)
    *libheap.a:multi_heap.*(.literal.assert_valid_block .text.assert_valid_block)
    *libheap.a:multi_heap.*(.literal.multi_heap_aligned_alloc_impl .text.multi_heap_aligned_alloc_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_aligned_alloc_impl_offs .text.multi_heap_aligned_alloc_impl_offs)
    *libheap.a:multi_heap.*(.literal.multi_heap_aligned_alloc_offs .text.multi_heap_aligned_alloc_offs)
    *libheap.a:multi_heap.*(.literal.multi_heap_free_impl .text.multi_heap_free_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_get_allocated_size_impl .text.multi_heap_get_allocated_size_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_get_block_address_impl .text.multi_heap_get_block_address_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_get_first_block .text.multi_heap_get_first_block)
    *libheap.a:multi_heap.*(.literal.multi_heap_get_next_block .text.multi_heap_get_next_block)
    *libheap.a:multi_heap.*(.literal.multi_heap_internal_lock .text.multi_heap_internal_lock)
    *libheap.a:multi_heap.*(.literal.multi_heap_internal_unlock .text.multi_heap_internal_unlock)
    *libheap.a:multi_heap.*(.literal.multi_heap_is_free .text.multi_heap_is_free)
    *libheap.a:multi_heap.*(.literal.multi_heap_malloc_impl .text.multi_heap_malloc_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_realloc_impl .text.multi_heap_realloc_impl)
    *libheap.a:multi_heap.*(.literal.multi_heap_set_lock .text.multi_heap_set_lock)
    *libheap.a:tlsf.*(.literal.tlsf_alloc_overhead .text.tlsf_alloc_overhead)
    *libheap.a:tlsf.*(.literal.tlsf_block_size .text.tlsf_block_size)
    *libheap.a:tlsf.*(.literal.tlsf_free .text.tlsf_free)
    *libheap.a:tlsf.*(.literal.tlsf_get_pool .text.tlsf_get_pool)
    *libheap.a:tlsf.*(.literal.tlsf_malloc .text.tlsf_malloc)
    *libheap.a:tlsf.*(.literal.tlsf_memalign .text.tlsf_memalign)
    *libheap.a:tlsf.*(.literal.tlsf_memalign_offs .text.tlsf_memalign_offs)
    *libheap.a:tlsf.*(.literal.tlsf_realloc .text.tlsf_realloc)
    *libheap.a:tlsf.*(.literal.tlsf_size .text.tlsf_size)
    *liblog.a:log_lock.*(.literal .literal.* .text .text.*)
    *liblog.a:log_timestamp.*(.literal.esp_log_early_timestamp .text.esp_log_early_timestamp)
    *liblog.a:log_timestamp.*(.literal.esp_log_timestamp .text.esp_log_timestamp)
    *liblog.a:log_write.*(.literal.esp_log_write .text.esp_log_write)
    *libnewlib.a:abort.*(.literal .literal.* .text .text.*)
    *libnewlib.a:assert.*(.literal .literal.* .text .text.*)
    *libnewlib.a:heap.*(.literal .literal.* .text .text.*)
    *libnewlib.a:stdatomic.*(.literal .literal.* .text .text.*)
    *libriscv.a:interrupt.*(.text .text._global_interrupt_handler .text.intr_handler_get_arg)
    *libriscv.a:vectors.*(.literal .literal.* .text .text.*)
    *libsoc.a:lldesc.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:flash_brownout_hook.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:memspi_host_driver.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_boya.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_gd.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_generic.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_issi.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_mxic.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_th.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_chip_winbond.*(.literal .literal.* .text .text.*)
    *libspi_flash.a:spi_flash_wrap.*(.literal .literal.* .text .text.*)
  } > sram_low
  /* Marks the end of IRAM code segment */
  .iram0.text_end (NOLOAD) :
  {
    /* Align the end of code region as per PMP region granularity */
    . = ALIGN(128);
    
 . = ALIGN(4); 
 _iram_text_end = ABSOLUTE(.);
  } > sram_low
  .iram0.data :
  {
    
 . = ALIGN(16); 
 _iram_data_start = ABSOLUTE(.);
    _coredump_iram_start = ABSOLUTE(.);
    *(.iram2.coredump .iram2.coredump.*)
    _coredump_iram_end = ABSOLUTE(.);
    *(.iram.data .iram.data.*)
    _iram_data_end = ABSOLUTE(.);
  } > sram_low
  .iram0.bss (NOLOAD) :
  {
    
 . = ALIGN(16); 
 _iram_bss_start = ABSOLUTE(.);
    *(.iram.bss .iram.bss.*)
    _iram_bss_end = ABSOLUTE(.);
    
 . = ALIGN(16); 
 _iram_end = ABSOLUTE(.);
  } > sram_low
  .dram0.data :
  {
    _data_start_low = ABSOLUTE(.);
    *(.gnu.linkonce.d.*)
    *(.data1)
    __global_pointer$ = . + 0x800;
    *(.sdata)
    *(.sdata.*)
    *(.gnu.linkonce.s.*)
    *(.gnu.linkonce.s2.*)
    *(.jcr)
    _coredump_dram_start = ABSOLUTE(.);
    *(.dram2.coredump .dram2.coredump.*)
    _coredump_dram_end = ABSOLUTE(.);
    *(.data .data.*)
    *(.dram1 .dram1.*)
    *libapp_trace.a:app_trace.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:app_trace_util.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:port_uart.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:_divsf3.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:restore.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:save.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:clk_utils.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:esp_memory_utils.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_by_dqs.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_config.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_tuning.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_init.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_param.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_sleep.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:rtc_clk.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:sleep_console.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:sleep_usb.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_mm.a:esp_cache.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_mm.a:ext_mem_layout.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_psram.a:esp_psram_impl_ap_hex.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_psram.a:mmu_psram_flash_v2.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_print.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_spiflash.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_sys.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_wdt.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:esp_err.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:image_process.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:ubsan.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:_divsf3.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:save-restore.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcov.a:(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:cache_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:i2c_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:ledc_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:mmu_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:pmu_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_encrypt_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_gpspi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_slave_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *liblog.a:log_lock.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:abort.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:assert.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:heap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:stdatomic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libsoc.a:lldesc.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:flash_brownout_hook.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:memspi_host_driver.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_boya.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_gd.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_generic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_issi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_mxic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_th.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_winbond.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_wrap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    _data_end_low = ABSOLUTE(.);
  } > sram_low
  .dram1.data :
  {
    _data_start_high = ABSOLUTE(.);
    *(.data .data.*)
    *(.dram1 .dram1.*)
    *libapp_trace.a:app_trace.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:app_trace_util.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libapp_trace.a:port_uart.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:_divsf3.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:restore.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libclang_rt.builtins.a:save.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:clk_utils.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:esp_memory_utils.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_by_dqs.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_config.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:mspi_timing_tuning.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_init.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_param.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:pmu_sleep.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:rtc_clk.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:sleep_console.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:sleep_usb.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_hw_support.a:systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_mm.a:esp_cache.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_mm.a:ext_mem_layout.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_psram.a:esp_psram_impl_ap_hex.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_psram.a:mmu_psram_flash_v2.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_print.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_spiflash.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_sys.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_systimer.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_rom.a:esp_rom_wdt.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:esp_err.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:image_process.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libesp_system.a:ubsan.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:_divsf3.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcc.a:save-restore.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libgcov.a:(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:cache_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:i2c_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:ledc_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:mmu_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:pmu_hal.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_encrypt_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_gpspi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_flash_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libhal.a:spi_slave_hal_iram.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *liblog.a:log_lock.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:abort.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:assert.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:heap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libnewlib.a:stdatomic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libsoc.a:lldesc.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:flash_brownout_hook.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:memspi_host_driver.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_boya.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_gd.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_generic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_issi.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_mxic.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_th.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_chip_winbond.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    *libspi_flash.a:spi_flash_wrap.*(.rodata .rodata.* .sdata2 .sdata2.* .srodata .srodata.*)
    _data_end_high = ABSOLUTE(.);
  } > sram_high
  /**

   * This section holds data that should not be initialized at power up.

   * The section located in Internal SRAM memory region. The macro _NOINIT

   * can be used as attribute to place data into this section.

   * See the "esp_attr.h" file for more information.

   */
  .noinit (NOLOAD):
  {
    
 . = ALIGN(4); 
 _noinit_start = ABSOLUTE(.);
    *(.noinit .noinit.*)
    
 . = ALIGN(4); 
 _noinit_end = ABSOLUTE(.);
  } > sram_low
  .flash.text :
  {
    _stext = .;
    /**

     * Mark the start of flash.text.

     * This can be used by the MMU driver to maintain the virtual address.

     */
    _instruction_reserved_start = ABSOLUTE(.);
    _text_start = ABSOLUTE(.);
    *(EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_driver_gptimer.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:dma2d.* *libesp_hw_support.a:dw_gdma.* *libesp_hw_support.a:esp_ldo_regulator.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:gdma.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:esp_mmu_map.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:gdma_hal_ahb_v2.* *libhal.a:gdma_hal_axi.* *libhal.a:gdma_hal_top.* *libhal.a:gpio_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *libheap.a:tlsf.* *liblog.a:log_lock.* *liblog.a:log_timestamp.* *liblog.a:log_write.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .literal EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_driver_gptimer.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:dma2d.* *libesp_hw_support.a:dw_gdma.* *libesp_hw_support.a:esp_ldo_regulator.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:gdma.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:esp_mmu_map.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:gdma_hal_ahb_v2.* *libhal.a:gdma_hal_axi.* *libhal.a:gdma_hal_top.* *libhal.a:gpio_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *libheap.a:tlsf.* *liblog.a:log_lock.* *liblog.a:log_timestamp.* *liblog.a:log_write.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .literal.* EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_driver_gptimer.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:dma2d.* *libesp_hw_support.a:dw_gdma.* *libesp_hw_support.a:esp_ldo_regulator.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:gdma.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:esp_mmu_map.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:gdma_hal_ahb_v2.* *libhal.a:gdma_hal_axi.* *libhal.a:gdma_hal_top.* *libhal.a:gpio_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *libheap.a:tlsf.* *liblog.a:log_lock.* *liblog.a:log_timestamp.* *liblog.a:log_write.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .text EXCLUDE_FILE(*libesp_ringbuf.a *libfreertos.a *libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_driver_gptimer.a:gptimer.* *libesp_event.a:default_event_loop.* *libesp_event.a:esp_event.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:cpu.* *libesp_hw_support.a:dma2d.* *libesp_hw_support.a:dw_gdma.* *libesp_hw_support.a:esp_ldo_regulator.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:gdma.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:periph_ctrl.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:rtc_time.* *libesp_hw_support.a:sar_periph_ctrl.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:esp_mmu_map.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:esp_system_chip.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:lib2funcs.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:gdma_hal_ahb_v2.* *libhal.a:gdma_hal_axi.* *libhal.a:gdma_hal_top.* *libhal.a:gpio_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *libhal.a:timer_hal.* *libheap.a:multi_heap.* *libheap.a:tlsf.* *liblog.a:log_lock.* *liblog.a:log_timestamp.* *liblog.a:log_write.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libriscv.a:interrupt.* *libriscv.a:vectors.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .text.*)
    *libesp_driver_gptimer.a:gptimer.*(.text .text.gptimer_del_timer .text.gptimer_destroy .text.gptimer_disable .text.gptimer_enable .text.gptimer_get_captured_count .text.gptimer_get_raw_count .text.gptimer_get_resolution .text.gptimer_new_timer .text.gptimer_register_event_callbacks .text.gptimer_set_alarm_action .text.gptimer_set_raw_count .text.gptimer_start .text.gptimer_stop)
    *libesp_event.a:default_event_loop.*(.text .text.esp_event_handler_instance_register .text.esp_event_handler_instance_unregister .text.esp_event_handler_register .text.esp_event_handler_unregister .text.esp_event_loop_create_default .text.esp_event_loop_delete_default .text.esp_event_post)
    *libesp_event.a:esp_event.*(.text .text.base_node_add_handler .text.esp_event_dump .text.esp_event_handler_instance_register_with .text.esp_event_handler_instance_unregister_with .text.esp_event_handler_register_with .text.esp_event_handler_register_with_internal .text.esp_event_handler_unregister_with .text.esp_event_handler_unregister_with_internal .text.esp_event_loop_create .text.esp_event_loop_delete .text.esp_event_loop_run .text.esp_event_loop_run_task .text.esp_event_post_to .text.esp_event_post_to.part.0 .text.handler_instances_add .text.handler_instances_remove .text.handler_instances_remove_all .text.loop_node_add_handler .text.loop_remove_handler)
    *libesp_hw_support.a:cpu.*(.text .text.esp_cpu_clear_breakpoint .text.esp_cpu_clear_watchpoint .text.esp_cpu_set_breakpoint .text.esp_cpu_set_watchpoint)
    *libesp_hw_support.a:dma2d.*(.text .text.dma2d_acquire_pool .text.dma2d_append .text.dma2d_apply_strategy .text.dma2d_configure_color_space_conversion .text.dma2d_configure_dscr_port_mode .text.dma2d_connect .text.dma2d_enqueue .text.dma2d_force_end .text.dma2d_get_trans_elm_size .text.dma2d_register_rx_event_callbacks .text.dma2d_register_tx_event_callbacks .text.dma2d_release_pool .text.dma2d_reset .text.dma2d_set_desc_addr .text.dma2d_set_transfer_ability .text.dma2d_start .text.dma2d_stop)
    *libesp_hw_support.a:dw_gdma.*(.text .text.channel_destroy.isra.0 .text.dw_gdma_channel_abort .text.dw_gdma_channel_config_transfer .text.dw_gdma_channel_continue .text.dw_gdma_channel_enable_ctrl .text.dw_gdma_channel_get_id .text.dw_gdma_channel_lock .text.dw_gdma_channel_register_event_callbacks .text.dw_gdma_channel_set_block_markers .text.dw_gdma_channel_suspend_ctrl .text.dw_gdma_channel_unlock .text.dw_gdma_channel_use_link_list .text.dw_gdma_del_channel .text.dw_gdma_del_link_list .text.dw_gdma_link_list_get_item .text.dw_gdma_lli_config_transfer .text.dw_gdma_lli_set_block_markers .text.dw_gdma_lli_set_next .text.dw_gdma_new_channel .text.dw_gdma_new_link_list .text.dw_gdma_release_group_handle)
    *libesp_hw_support.a:esp_ldo_regulator.*(.text .text.esp_ldo_channel_adjust_voltage .text.esp_ldo_dump .text.esp_ldo_release_channel)
    *libesp_hw_support.a:gdma.*(.text .text.do_allocate_gdma_channel .text.gdma_apply_strategy .text.gdma_config_transfer .text.gdma_connect .text.gdma_del_channel .text.gdma_del_rx_channel .text.gdma_del_tx_channel .text.gdma_disconnect .text.gdma_get_alignment_constraints .text.gdma_get_free_m2m_trig_id_mask .text.gdma_get_group_channel_id .text.gdma_new_ahb_channel .text.gdma_new_axi_channel .text.gdma_register_rx_event_callbacks .text.gdma_register_tx_event_callbacks .text.gdma_release_group_handle .text.gdma_release_pair_handle .text.gdma_set_priority)
    *libesp_hw_support.a:periph_ctrl.*(.text .text.periph_module_disable .text.periph_module_enable .text.periph_rcc_acquire_enter .text.periph_rcc_acquire_exit .text.periph_rcc_release_enter .text.periph_rcc_release_exit)
    *libesp_hw_support.a:sar_periph_ctrl.*(.text .text.s_sar_power_acquire .text.s_sar_power_release .text.sar_periph_ctrl_adc_continuous_power_acquire .text.sar_periph_ctrl_adc_continuous_power_release .text.sar_periph_ctrl_adc_oneshot_power_acquire .text.sar_periph_ctrl_adc_oneshot_power_release .text.sar_periph_ctrl_init .text.sar_periph_ctrl_power_disable .text.sar_periph_ctrl_pwdet_power_acquire .text.sar_periph_ctrl_pwdet_power_release)
    *libesp_mm.a:esp_mmu_map.*(.text .text.esp_mmu_map .text.esp_mmu_map_dump_mapped_blocks .text.esp_mmu_map_get_max_consecutive_free_block_size .text.esp_mmu_map_reserve_block_with_caps .text.esp_mmu_paddr_to_vaddr .text.esp_mmu_unmap .text.esp_mmu_vaddr_to_paddr .text.s_get_bus_mask.part.0)
    *libesp_psram.a:esp_psram.*(.text .text.__esp_system_init_fn_add_psram_to_heap .text.esp_psram_bss_init .text.esp_psram_extram_add_to_heap_allocator .text.esp_psram_extram_reserve_dma_pool .text.esp_psram_extram_test .text.esp_psram_get_size .text.esp_psram_io_get_cs_io .text.s_test_psram.constprop.0)
    *libesp_system.a:esp_system_chip.*(.text .text.esp_get_free_heap_size .text.esp_get_free_internal_heap_size .text.esp_get_idf_version .text.esp_get_minimum_free_heap_size)
    *libfreertos.a:app_startup.*(.literal .literal.* .text .text.*)
    *libfreertos.a:freertos_compatibility.*(.literal .literal.* .text .text.*)
    *libfreertos.a:idf_additions.*(.literal .literal.* .text .text.*)
    *libfreertos.a:tasks.*(.literal.uxTaskGetSnapshotAll .text.uxTaskGetSnapshotAll)
    *libfreertos.a:tasks.*(.literal.xTaskGetNext .text.xTaskGetNext)
    *libhal.a:gdma_hal_ahb_v2.*(.text .text.gdma_ahb_hal_clear_crc .text.gdma_ahb_hal_connect_peri .text.gdma_ahb_hal_disconnect_peri .text.gdma_ahb_hal_enable_burst .text.gdma_ahb_hal_enable_etm_task .text.gdma_ahb_hal_enable_intr .text.gdma_ahb_hal_get_crc_result .text.gdma_ahb_hal_get_intr_status_reg .text.gdma_ahb_hal_init .text.gdma_ahb_hal_set_crc_poly .text.gdma_ahb_hal_set_priority .text.gdma_ahb_hal_set_strategy)
    *libhal.a:gdma_hal_axi.*(.text .text.gdma_axi_hal_clear_crc .text.gdma_axi_hal_connect_peri .text.gdma_axi_hal_disconnect_peri .text.gdma_axi_hal_enable_access_encrypt_mem .text.gdma_axi_hal_enable_burst .text.gdma_axi_hal_enable_etm_task .text.gdma_axi_hal_enable_intr .text.gdma_axi_hal_get_crc_result .text.gdma_axi_hal_get_intr_status_reg .text.gdma_axi_hal_init .text.gdma_axi_hal_set_burst_size .text.gdma_axi_hal_set_crc_poly .text.gdma_axi_hal_set_priority .text.gdma_axi_hal_set_strategy)
    *libhal.a:gdma_hal_top.*(.text .text.gdma_hal_clear_crc .text.gdma_hal_connect_peri .text.gdma_hal_deinit .text.gdma_hal_disconnect_peri .text.gdma_hal_enable_access_encrypt_mem .text.gdma_hal_enable_burst .text.gdma_hal_enable_etm_task .text.gdma_hal_enable_intr .text.gdma_hal_get_crc_result .text.gdma_hal_get_intr_status_reg .text.gdma_hal_set_burst_size .text.gdma_hal_set_crc_poly .text.gdma_hal_set_priority .text.gdma_hal_set_strategy)
    *libhal.a:gpio_hal.*(.text .text.gpio_hal_hysteresis_soft_enable .text.gpio_hal_intr_disable .text.gpio_hal_intr_enable_on_core)
    *libhal.a:timer_hal.*(.text .text.timer_hal_deinit .text.timer_hal_init .text.timer_hal_set_counter_value)
    *libheap.a:multi_heap.*(.text .text.multi_heap_check .text.multi_heap_dump .text.multi_heap_dump_tlsf .text.multi_heap_free_size_impl .text.multi_heap_get_info_impl .text.multi_heap_get_info_tlsf .text.multi_heap_minimum_free_size_impl .text.multi_heap_register_impl .text.multi_heap_reset_minimum_free_bytes .text.multi_heap_restore_minimum_free_bytes .text.multi_heap_walk)
    *libheap.a:tlsf.*(.text .text.default_walker .text.integrity_walker .text.tlsf_add_pool .text.tlsf_check .text.tlsf_check_pool .text.tlsf_create .text.tlsf_create_with_pool .text.tlsf_destroy .text.tlsf_fit_size .text.tlsf_malloc_addr .text.tlsf_pool_overhead .text.tlsf_remove_pool .text.tlsf_walk_pool)
    *liblog.a:log_timestamp.*(.text)
    *liblog.a:log_write.*(.text .text.esp_log_set_vprintf .text.esp_log_writev)
    *libriscv.a:interrupt.*(.literal.intr_handler_get .text.intr_handler_get)
    *libriscv.a:interrupt.*(.literal.intr_handler_set .text.intr_handler_set)
    *(.stub)
    *(.gnu.linkonce.t.*)
    *(.gnu.warning)
    *(.irom0.text) /* catch stray ICACHE_RODATA_ATTR */
    /**

     * CPU will try to prefetch up to 16 bytes of of instructions.

     * This means that any configuration (e.g. MMU, PMS) must allow

     * safe access to up to 16 bytes after the last real instruction, add

     * dummy bytes to ensure this

     */
    . += 16;
    _text_end = ABSOLUTE(.);
    /**

     * Mark the flash.text end.

     * This can be used for MMU driver to maintain virtual address.

     */
    _instruction_reserved_end = ABSOLUTE(.);
    _etext = .;
    /**

     * Similar to _iram_start, this symbol goes here so it is

     * resolved by addr2line in preference to the first symbol in

     * the flash.text segment.

     */
    _flash_cache_start = ABSOLUTE(0);
  } > text_seg_low
  /**

   * Dummy section represents the .flash.text section but in default_rodata_seg.

   * Thus, it must have its alignment and (at least) its size.

   */
  .flash_rodata_dummy (NOLOAD):
  {
    _flash_rodata_dummy_start = .;
    . = ALIGN(ALIGNOF(.flash.text)) + SIZEOF(.flash.text);
    /* Add alignment of MMU page size + 0x20 bytes for the mapping header. */
    . = ALIGN(0x10000) + 0x20;
  } > rodata_seg_low
  .flash.appdesc : ALIGN(0x10)
  {
    /**

     * Mark flash.rodata start.

     * This can be used for mmu driver to maintain virtual address

     */
    _rodata_reserved_start = ABSOLUTE(.);
    _rodata_start = ABSOLUTE(.);
    /* !DO NOT PUT ANYTHING BEFORE THIS! */
    /* Should be the first.  App version info. */
    *(.rodata_desc .rodata_desc.*)
    /* Should be the second. Custom app version info. */
    *(.rodata_custom_desc .rodata_custom_desc.*)
    /**

     * Create an empty gap within this section. Thanks to this, the end of this

     * section will match .flash.rodata's begin address. Thus, both sections

     * will be merged when creating the final bin image.

     */
    . = ALIGN(ALIGNOF(.flash.rodata));
  } > rodata_seg_low
  ASSERT((ADDR(.flash.rodata) == ADDR(.flash.appdesc) + SIZEOF(.flash.appdesc)), "The gap between .flash.appdesc and .flash.rodata must not exist to produce the final bin image.")
  .flash.rodata : ALIGN(0x10)
  {
    _flash_rodata_start = ABSOLUTE(.);
    *(EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .rodata EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .rodata.* EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .sdata2 EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .sdata2.* EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .srodata EXCLUDE_FILE(*libgcov.a *libapp_trace.a:app_trace.* *libapp_trace.a:app_trace_util.* *libapp_trace.a:port_uart.* *libclang_rt.builtins.a:_divsf3.* *libclang_rt.builtins.a:restore.* *libclang_rt.builtins.a:save.* *libesp_hw_support.a:clk_utils.* *libesp_hw_support.a:esp_memory_utils.* *libesp_hw_support.a:mspi_timing_by_dqs.* *libesp_hw_support.a:mspi_timing_config.* *libesp_hw_support.a:mspi_timing_tuning.* *libesp_hw_support.a:pmu_init.* *libesp_hw_support.a:pmu_param.* *libesp_hw_support.a:pmu_sleep.* *libesp_hw_support.a:rtc_clk.* *libesp_hw_support.a:sleep_console.* *libesp_hw_support.a:sleep_usb.* *libesp_hw_support.a:systimer.* *libesp_mm.a:esp_cache.* *libesp_mm.a:ext_mem_layout.* *libesp_psram.a:esp_psram_impl_ap_hex.* *libesp_psram.a:mmu_psram_flash_v2.* *libesp_rom.a:esp_rom_print.* *libesp_rom.a:esp_rom_spiflash.* *libesp_rom.a:esp_rom_sys.* *libesp_rom.a:esp_rom_systimer.* *libesp_rom.a:esp_rom_wdt.* *libesp_system.a:esp_err.* *libesp_system.a:image_process.* *libesp_system.a:ubsan.* *libgcc.a:_divsf3.* *libgcc.a:save-restore.* *libhal.a:cache_hal.* *libhal.a:i2c_hal_iram.* *libhal.a:ledc_hal_iram.* *libhal.a:mmu_hal.* *libhal.a:pmu_hal.* *libhal.a:spi_flash_encrypt_hal_iram.* *libhal.a:spi_flash_hal_gpspi.* *libhal.a:spi_flash_hal_iram.* *libhal.a:spi_hal_iram.* *libhal.a:spi_slave_hal_iram.* *liblog.a:log_lock.* *libnewlib.a:abort.* *libnewlib.a:assert.* *libnewlib.a:heap.* *libnewlib.a:stdatomic.* *libsoc.a:lldesc.* *libspi_flash.a:flash_brownout_hook.* *libspi_flash.a:memspi_host_driver.* *libspi_flash.a:spi_flash_chip_boya.* *libspi_flash.a:spi_flash_chip_gd.* *libspi_flash.a:spi_flash_chip_generic.* *libspi_flash.a:spi_flash_chip_issi.* *libspi_flash.a:spi_flash_chip_mxic.* *libspi_flash.a:spi_flash_chip_th.* *libspi_flash.a:spi_flash_chip_winbond.* *libspi_flash.a:spi_flash_wrap.*) .srodata.*)
    *(.irom1.text) /* catch stray ICACHE_RODATA_ATTR */
    *(.gnu.linkonce.r.*)
    *(.rodata1)
    *(.gcc_except_table .gcc_except_table.*)
    *(.gnu.linkonce.e.*)
    . = ALIGN(ALIGNOF(.flash.init_array));
  } > rodata_seg_low
  ASSERT((ADDR(.flash.init_array) == ADDR(.flash.rodata) + SIZEOF(.flash.rodata)), "The gap between .flash.rodata and .flash.init_array must not exist to produce the final bin image.")
  .flash.init_array :
  {
    /**

     * C++ constructor tables.

     *

     * Excluding crtbegin.o/crtend.o since IDF doesn't use the toolchain crt.

     *

     * RISC-V gcc is configured with --enable-initfini-array so it emits

     * .init_array section instead. But the init_priority sections will be

     * sorted for iteration in ascending order during startup.

     * The rest of the init_array sections is sorted for iteration in descending

     * order during startup, however. Hence a different section is generated for

     * the init_priority functions which is iterated in ascending order during

     * startup. The corresponding code can be found in startup.c.

     */
    
 . = ALIGN(4); 
 __init_priority_array_start = ABSOLUTE(.);
    KEEP (*(EXCLUDE_FILE (*crtend.* *crtbegin.*) .init_array.*))
    __init_priority_array_end = ABSOLUTE(.);
    
 . = ALIGN(4); 
 __init_array_start = ABSOLUTE(.);
    KEEP (*(EXCLUDE_FILE (*crtend.* *crtbegin.*) .init_array))
    __init_array_end = ABSOLUTE(.);
    /* Addresses of memory regions reserved via SOC_RESERVE_MEMORY_REGION() */
    
 . = ALIGN(4); 
 soc_reserved_memory_region_start = ABSOLUTE(.);
    KEEP (*(.reserved_memory_address))
    soc_reserved_memory_region_end = ABSOLUTE(.);
    /* System init functions registered via ESP_SYSTEM_INIT_FN */
    
 . = ALIGN(4); 
 _esp_system_init_fn_array_start = ABSOLUTE(.);
    KEEP (*(SORT_BY_INIT_PRIORITY(.esp_system_init_fn.*)))
    _esp_system_init_fn_array_end = ABSOLUTE(.);
    _rodata_end = ABSOLUTE(.);
    . = ALIGN(ALIGNOF(.eh_frame_hdr));
  } > rodata_seg_low
  ASSERT((ADDR(.eh_frame_hdr) == ADDR(.flash.init_array) + SIZEOF(.flash.init_array)), "The gap between .flash.init_array and .eh_frame_hdr must not exist to produce the final bin image.")
  .eh_frame_hdr :
  {
    . = ALIGN(ALIGNOF(.eh_frame));
  } > rodata_seg_low
  ASSERT((ADDR(.eh_frame) == ADDR(.eh_frame_hdr) + SIZEOF(.eh_frame_hdr)), "The gap between .eh_frame_hdr and .eh_frame must not exist to produce the final bin image.")
  .eh_frame :
  {
    . = ALIGN(ALIGNOF(.flash.tdata));
  } > rodata_seg_low
  ASSERT((ADDR(.flash.tdata) == ADDR(.eh_frame) + SIZEOF(.eh_frame)), "The gap between .eh_frame and .flash.tdata must not exist to produce the final bin image.")
  .flash.tdata :
  {
    _thread_local_data_start = ABSOLUTE(.);
    *(.tdata .tdata.* .gnu.linkonce.td.*)
    . = ALIGN(ALIGNOF(.flash.tbss));
    _thread_local_data_end = ABSOLUTE(.);
  } > rodata_seg_low
  ASSERT((ADDR(.flash.tbss) == ADDR(.flash.tdata) + SIZEOF(.flash.tdata)), "The gap between .flash.tdata and .flash.tbss must not exist to produce the final bin image.")
  .flash.tbss (NOLOAD) :
  {
    _thread_local_bss_start = ABSOLUTE(.);
    *(.tbss .tbss.* .gnu.linkonce.tb.*)
    *(.tcommon .tcommon.*)
    _thread_local_bss_end = ABSOLUTE(.);
  } > rodata_seg_low
  /**

   * This section contains all the rodata that is not used

   * at runtime, helping to avoid an increase in binary size.

   */
  .flash.rodata_noload (NOLOAD) :
  {
    /**

     * This symbol marks the end of flash.rodata. It can be utilized by the MMU

     * driver to maintain the virtual address.

     * NOLOAD rodata may not be included in this section.

     */
    _rodata_reserved_end = ADDR(.flash.tbss);
  } > rodata_seg_low
  /**

   * This section is required to skip flash sections, because `extern_ram_seg`

   * and `drom_seg` / `irom_seg` are on the same bus when xip on psram

   */
  .ext_ram.dummy (NOLOAD):
  {
    . = ORIGIN(ext_ram_seg) + (_rodata_reserved_end - _flash_rodata_dummy_start);
    . = ALIGN (0x10000);
  } > ext_ram_seg
  .dram0.bss (NOLOAD) :
  {
    
 . = ALIGN(4); 
 _bss_start_low = ABSOLUTE(.);
    /**

     * ldgen places all bss-related data to mapping[dram0_bss]

     * (See components/esp_system/app.lf).

     */
    *(.bss .bss.*)
    *(.dynbss .dynsbss .gnu.linkonce.b .gnu.linkonce.b.* .gnu.linkonce.sb .gnu.linkonce.sb.* .gnu.linkonce.sb2 .gnu.linkonce.sb2.* .sbss .sbss.* .sbss2 .sbss2.* .scommon .share.mem)
    *(.ext_ram.bss .ext_ram.bss.*)
    *(COMMON)
    
 . = ALIGN(4); 
 _bss_end_low = ABSOLUTE(.);
  } > sram_low
  .dram1.bss (NOLOAD) :
  {
    
 . = ALIGN(4); 
 _bss_start_high = ABSOLUTE(.);
    /**

     * ldgen places all bss-related data to mapping[dram0_bss]

     * (See components/esp_system/app.lf).

     */
    *(.bss .bss.*)
    *(.dynbss .dynsbss .gnu.linkonce.b .gnu.linkonce.b.* .gnu.linkonce.sb .gnu.linkonce.sb.* .gnu.linkonce.sb2 .gnu.linkonce.sb2.* .sbss .sbss.* .sbss2 .sbss2.* .scommon .share.mem)
    *(.ext_ram.bss .ext_ram.bss.*)
    *(COMMON)
    
 . = ALIGN(4); 
 _bss_end_high = ABSOLUTE(.);
  } > sram_high
  /* Marks the end of data, bss and possibly rodata */
  .dram0.heap_start_low (NOLOAD) :
  {
    
 . = ALIGN(16); 
 _heap_start_low = ABSOLUTE(.);
  } > sram_low
    /* Marks the end of data, bss and possibly rodata */
  .dram1.heap_start_high (NOLOAD) :
  {
    
 . = ALIGN(16); 
 _heap_start_high = ABSOLUTE(.);
  } > sram_high
  /* DWARF 1 */
  .debug 0 : { *(.debug) }
  .line 0 : { *(.line) }
  /* GNU DWARF 1 extensions */
  .debug_srcinfo 0 : { *(.debug_srcinfo) }
  .debug_sfnames 0 : { *(.debug_sfnames) }
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges 0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  /* DWARF 2 */
  .debug_info 0 : { *(.debug_info .gnu.linkonce.wi.*) }
  .debug_abbrev 0 : { *(.debug_abbrev) }
  .debug_line 0 : { *(.debug_line) }
  .debug_frame 0 : { *(.debug_frame) }
  .debug_str 0 : { *(.debug_str) }
  .debug_loc 0 : { *(.debug_loc) }
  .debug_macinfo 0 : { *(.debug_macinfo) }
  .debug_pubtypes 0 : { *(.debug_pubtypes) }
  /* DWARF 3 */
  .debug_ranges 0 : { *(.debug_ranges) }
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames 0 : { *(.debug_varnames) }
  /* GNU DWARF 2 extensions */
  .debug_gnu_pubnames 0 : { *(.debug_gnu_pubnames) }
  .debug_gnu_pubtypes 0 : { *(.debug_gnu_pubtypes) }
  /* DWARF 4 */
  .debug_types 0 : { *(.debug_types) }
  /* DWARF 5 */
  .debug_addr 0 : { *(.debug_addr) }
  .debug_line_str 0 : { *(.debug_line_str) }
  .debug_loclists 0 : { *(.debug_loclists) }
  .debug_macro 0 : { *(.debug_macro) }
  .debug_names 0 : { *(.debug_names) }
  .debug_rnglists 0 : { *(.debug_rnglists) }
  .debug_str_offsets 0 : { *(.debug_str_offsets) }
  .comment 0 : { *(.comment) }
  .note.GNU-stack 0: { *(.note.GNU-stack) }
  .riscv.attributes 0: { *(.riscv.attributes) }
  /DISCARD/ :
  {
   /**

    * Discarding .rela.* sections results in the following mapping:

    * .rela.text.* -> .text.*

    * .rela.data.* -> .data.*

    * And so forth...

    */
   *(.rela.*)
   *(.got .got.plt) /* TODO: GCC-382 */
   *(.eh_frame_hdr)
   *(.eh_frame)
  }
}
