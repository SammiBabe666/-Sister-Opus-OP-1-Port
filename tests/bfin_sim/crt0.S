/* A minimal start for the GNU Blackfin simulator (bfin-elf-run): a stack, then main, then exit(main's value).
 * Copyright (C) 2026 SammiBabe666. GPL-3.0-or-later with the OP-1 firmware permission (NOTICE). */
	.section .text.start, "ax"
	.global __start
__start:
	SP.L = __stack_top; SP.H = __stack_top;
	FP = 0;
	L0 = 0; L1 = 0; L2 = 0; L3 = 0;
	call _main;
	call _sim_exit;
1:	jump 1b;
