ENTRY(_start)

SECTIONS
{
    . = 0x4000000;
   .text : {
	*(.init)   
	*(.text)
   }
   .data : {
	*(.data)
   }
   .bss : {
	*(.bss)
   }
}
