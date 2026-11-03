ENTRY( _start )

__stack_size = 1024;

PROVIDE( _stack_size = __stack_size );


MEMORY
{
	/*FLASH, RAM, and RAM-CODE can be freely configured according to project 
	requirements;Please refer to the datasheet manual for specific allocation*/

	/* ---------------------------------------------------------------------
	 * 可以设置的 APP 基地址 / The settable APP base address
	 * ---------------------------------------------------------------------
	 * 本工程编到 **槽 2**。三个槽的基址（见 SRC/Core/eeprom.h 的 APP_SLOT_ADDR）：
	 *   slot 0 : 0x006000 .. 0x052000   (304 KB)  CH32H417_Logic_Analyzer_APP
	 *   slot 1 : 0x052000 .. 0x09E000   (304 KB)  CH32H417_USB2UART_APP
	 *   slot 2 : 0x09E000 .. 0x0EA000   (304 KB)  本 example 工程
	 * 烧到哪个槽，ORIGIN 就要改成哪个槽的基址；IAP 跳转用的是同一张表。
	 *
	 * This project is built for **slot 2**.  To put it in another slot, change
	 * ORIGIN below to that slot's base (APP_SLOT_ADDR(n) in SRC/Core/eeprom.h)
	 * and burn it with `-b 08000000 + base`, e.g. `-b 0809E000` for slot 2.
	 *
	 * ⚠️ ORIGIN 必须 8 KB 对齐（擦除页就是 8 KB）；槽内只需要放得下本工程，
	 *    这里给 32 KB，其余留给将来的 V5F 镜像（如果有的话）。
	 * ⚠️ ORIGIN must be 8 KB aligned (that is the erase page).  32 KB is far
	 *    more than this example needs; the rest of the slot stays free.
	 * ------------------------------------------------------------------- */
	/* LENGTH 是这个核在该槽里能用的全部空间：从槽基址到 V5F 起点 = 0x1A000 (104 KB)。
	 * 不要照抄别的工程 —— 它跟着槽走，也跟着 V5F 的偏移走。
	 * LENGTH is the whole space this core owns in the slot: from the slot
	 * base up to where V5F starts = 0x1A000 (104 KB). */
	FLASH (rx) : ORIGIN = 0x0009E000, LENGTH = 104K             /* zero_wait flah ,and loaded into RAM_CODE for running */
	RAM_CODE (xrw) : ORIGIN = 0x20140000, LENGTH = 64K         /* RAM for running code*/
	/* The last 16 bytes of the RAM are left out: the APP keeps the IAP request
	   flag at 0x2017FFF0, which has to survive a software reset, so nothing
	   may be linked there (.data/.bss/stack all stop 16 bytes lower).        */
	RAM (xrw) : ORIGIN = (0x20150000+256), LENGTH = (256K-64K-256-16) /* RAM for .data .bss section and other*/
	RAM_LOAD (xrw) : ORIGIN = 0x20150000, LENGTH = 256         /* RAM for .load section*/
}

SECTIONS
{
	.init :
	{
		_sinit = .;
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.init)))
		. = ALIGN(4);
		_einit = .;
	} >FLASH AT>FLASH

    .loadcodelalign : 
    {       
        . = ALIGN(4);
        PROVIDE(_loadcode_lma = .); 
    } >FLASH AT>FLASH 

	.loadcode :
	{
		. = ALIGN(4);
        PROVIDE(_loadcode_vma_start = .);
		*(.load);
		. = ALIGN(4); 
		PROVIDE(_loadcode_vma_end = .);
	}>RAM_LOAD AT>FLASH

    .highcodelalign : 
    {       
        . = ALIGN(4);
        PROVIDE(_highcode_lma = .); 
    } >FLASH AT>FLASH 
    
    .highcode : 
    {
        . = ALIGN(4);
        PROVIDE(_highcode_vma_start = .);
        *(.vector);
		KEEP(*(SORT_NONE(.vector_handler)))
		*(.text)
		*(.text.*)
		*(.rodata)
		*(.rodata*)
        *(.sdata2.*)
		*(.glue_7)
		*(.glue_7t)
		*(.gnu.linkonce.t.*)
		. = ALIGN(4); 
        PROVIDE(_highcode_vma_end = .);
    } >RAM_CODE AT>FLASH

	.text :
	{
		. = ALIGN(4);
		KEEP(*(SORT_NONE(.handle_reset)))
		. = ALIGN(4);
	} >FLASH AT>FLASH 

    .highcodelalign1 : 
    {       
        . = ALIGN(4);
        PROVIDE(_highcode_lma1 = .); 
    } >FLASH AT>FLASH 

	.fini :
	{
	    . = ALIGN(4);
        PROVIDE(_highcode_vma_start1 = .);
		KEEP(*(SORT_NONE(.fini)))
		. = ALIGN(4);
	} >RAM_CODE AT>FLASH

	PROVIDE( _etext = . );
	PROVIDE( _eitcm = . );	

	.preinit_array  :
	{
	  PROVIDE_HIDDEN (__preinit_array_start = .);
	  KEEP (*(.preinit_array))
	  PROVIDE_HIDDEN (__preinit_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.init_array     :
	{
	  PROVIDE_HIDDEN (__init_array_start = .);
	  KEEP (*(SORT_BY_INIT_PRIORITY(.init_array.*) SORT_BY_INIT_PRIORITY(.ctors.*)))
	  KEEP (*(.init_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .ctors))
	  PROVIDE_HIDDEN (__init_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.fini_array     :
	{
	  PROVIDE_HIDDEN (__fini_array_start = .);
	  KEEP (*(SORT_BY_INIT_PRIORITY(.fini_array.*) SORT_BY_INIT_PRIORITY(.dtors.*)))
	  KEEP (*(.fini_array EXCLUDE_FILE (*crtbegin.o *crtbegin?.o *crtend.o *crtend?.o ) .dtors))
	  PROVIDE_HIDDEN (__fini_array_end = .);
	} >RAM_CODE AT>FLASH 
	
	.ctors          :
	{
	  /* gcc uses crtbegin.o to find the start of
	     the constructors, so we make sure it is
	     first.  Because this is a wildcard, it
	     doesn't matter if the user does not
	     actually link against crtbegin.o; the
	     linker won't look for a file to match a
	     wildcard.  The wildcard also means that it
	     doesn't matter which directory crtbegin.o
	     is in.  */
	  KEEP (*crtbegin.o(.ctors))
	  KEEP (*crtbegin?.o(.ctors))
	  /* We don't want to include the .ctor section from
	     the crtend.o file until after the sorted ctors.
	     The .ctor section from the crtend file contains the
	     end of ctors marker and it must be last */
	  KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .ctors))
	  KEEP (*(SORT(.ctors.*)))
	  KEEP (*(.ctors))
	} >RAM_CODE AT>FLASH 
	
	.dtors          :
	{
	  KEEP (*crtbegin.o(.dtors))
	  KEEP (*crtbegin?.o(.dtors))
	  KEEP (*(EXCLUDE_FILE (*crtend.o *crtend?.o ) .dtors))
	  KEEP (*(SORT(.dtors.*)))
	  KEEP (*(.dtors))
	  . = ALIGN(4); 
      PROVIDE(_highcode_vma_end1 = .);
	} >RAM_CODE AT>FLASH 

	.dalign :
	{
		. = ALIGN(4);
		PROVIDE(_data_vma = .);
	} >RAM AT>FLASH	

	.dlalign :
	{
		. = ALIGN(4); 
		PROVIDE(_data_lma = .);
	} >FLASH AT>FLASH

	.data :
	{
		. = ALIGN(4);
    	*(.gnu.linkonce.r.*)
    	*(.data .data.*)
    	*(.gnu.linkonce.d.*)
		. = ALIGN(8);
    	PROVIDE( __global_pointer$ = . + 0x800 );
    	*(.sdata .sdata.*)
		*(.sdata2.*)
    	*(.gnu.linkonce.s.*)
    	. = ALIGN(8);
    	*(.srodata.cst16)
    	*(.srodata.cst8)
    	*(.srodata.cst4)
    	*(.srodata.cst2)
    	*(.srodata .srodata.*)
    	. = ALIGN(4);
		PROVIDE( _edata = .);
	} >RAM AT>FLASH

	.bss :
	{
		. = ALIGN(4);
		PROVIDE( _sbss = .);
  	    *(.sbss*)
        *(.gnu.linkonce.sb.*)
		*(.bss*)
     	*(.gnu.linkonce.b.*)		
		*(COMMON*)
		. = ALIGN(4);
		PROVIDE( _ebss = .);
	} >RAM AT>FLASH

	PROVIDE( _end = _ebss);
	PROVIDE( end = . );

    .stack ORIGIN(RAM) + LENGTH(RAM) - __stack_size :
    {
        PROVIDE( _heap_end = . );    
        . = ALIGN(4);
        PROVIDE(_susrstack = . );
        . = . + __stack_size;
        PROVIDE( _eusrstack = .);
    } >RAM 

}



