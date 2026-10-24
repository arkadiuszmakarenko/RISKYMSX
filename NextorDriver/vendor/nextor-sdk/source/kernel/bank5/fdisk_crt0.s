	;--- crt0.asm for MSX-DOS 2.50 FDISK
	;    The C program is expected to have the following main declaration:
	;    void main(int bc, int hl)

	.globl	_main

	.area _HEADER (ABS)

        .org    0x4100

        ;--- Initialize globals

init:   ;call    gsinit

	;--- Prepare parameters for main

		ex de,hl
		push bc
		pop hl

        ;--- Step 3: Call the "main" function

	push de
	ld de,#_HEAP_start
	ld (_heap_top),de
	pop de

	call    _main

	;--- Terminate program

	ret

        ;--- Program code and data (global vars) start here

	;* Place data after program code, and data init code after data

    .area	_HOME
	.area	_CODE
	.area	_DATA
_heap_top::
	.dw 0

gsinit: .area   _GSINIT

        .area   _GSFINAL
        ret

	;* These doesn't seem to be necessary... (?)

        ;.area  _OVERLAY
	;.area	_HOME
        ;.area  _BSS
	.area	_HEAP

_HEAP_start::
