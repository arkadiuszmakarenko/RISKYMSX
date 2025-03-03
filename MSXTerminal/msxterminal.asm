   OUTPUT "msxterm.bin"

        ORG #4000
        DW #4241,INIT        ; Important part of ROM cartridge header
        ;DW 0,0,0,0,0,0      ; This part of ROM cartridge header we skip as we never exit so it will not be handled.

INIT:
        LD A,32              ; 32-characters wide screen (default 29)
 LD (#F3AF),A
        LD HL,15             ; White (15) on black (0 * 256) (default depends of machine. Usually white on blue)
        LD (#F3EA),HL        ; set border color
        LD (#F3E9),HL        ; set rest of the colors
        LD A,1
        CALL #5F             ; Set screen mode
        LD HL,ARROW          ; Our custom cursor
        LD DE,#3800          ; destination
        LD BC,8              ; bytes
        CALL #5C             ; Move
        LD A,8               ; Color = 8 (Red)
        LD HL,#1B03
        CALL #4D

        LD HL,MOVE
        LD DE,UP
        LD BC,UP_END-UP
        PUSH DE
        LDIR
        RET

MOVE:
        PHASE #E000
UP:

LOOP:
        LD A,(#7FFF)         ; Check request from WCH CH32V303V
        CP 3                 ; #03 ?
        JP Z,0               ; Yes -> Nothing to do anymore, please reset computer
        CP 4                 ; #04
        JR Z,SPRITE
        AND A                ; #00 = There is nothing to print
        CALL Z,HLT           ; ... so wait until next screen refresh
        CALL NZ,#A2          ; In other case print it
        CALL #9C             ; Check if incoming byte from keyboard?
        JR Z,LOOP            ; Nothing on keyboard buffer -> LOOP
        CALL #9F             ; Read ASCII code from keyboard
        LD (#7FFD),A         ; Send it to WCH CH32V303V
        JR LOOP              ; -> LOOP

HLT     HALT                 ; Wait until next screen refresh
        RET


SPRITE: LD A,(#7FFF)
        LD HL,#1B01
        CALL #4D
        LD A,(#7FFF)
        DEC A
        DEC L
        CALL #4D
        JR LOOP
UP_END:
        DEPHASE


ARROW:  DB %00100000
        DB %00110000
        DB %00111000
        DB %00111100        ; Arrow >
        DB %00111000
        DB %00110000
        DB %00100000
        DB %00000000