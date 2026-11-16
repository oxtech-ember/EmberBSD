/* Section layout of the radio object: the vendor's special sections fold
 * into the ordinary ones, which is all the kernel's own script knows. */
SECTIONS
{
	.text : {
		espwifi_assoc_req_text_start = .;
		*(.text.ieee80211_assoc_req_construct)
		espwifi_assoc_req_text_end = .;
		*(.text .text.* .iram1 .iram1.* .wifi0iram .wifi0iram.*
		  .wifirxiram .wifirxiram.* .wifislpiram .wifislpiram.*
		  .wifislprxiram .wifislprxiram.* .wifiextrairam
		  .wifiextrairam.* .wifiorslpiram .wifiorslpiram.*
		  .phyiram .phyiram.*)
	}
	.rodata : { *(.rodata .rodata.* .rodata_wlog_* .srodata .srodata.*) }
	.data : { *(.data .data.* .dram1 .dram1.* .sdata .sdata.*) }
	.bss : { *(.bss .bss.* .sbss .sbss.* COMMON) }
	/DISCARD/ : { *(.comment .eh_frame .note.*) }
}
