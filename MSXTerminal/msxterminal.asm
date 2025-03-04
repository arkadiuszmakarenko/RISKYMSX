   OUTPUT "msxterm.bin"

  ORG #4000            

        DW #4241,INIT        ; Important part of ROM cartridge header
        DW 0,0,0,0,0,0       ; Unused part of standard ROM header
        DB "RISKY_BOOT",0    ; #4010 = ID string
        DB "1.0"             ; #401B = Version string
        DS #4020-$,0         ; #4020 (reserved for jump table)
        LD A,4               ; #4020 Switch ROM
        LD (#7FFE),A
        RET
INIT:

        LD HL,MOVE           ; Move code to RAM
        LD DE,UP
        LD BC,UP_END-UP
        PUSH DE
        LDIR
        RET

MOVE:
        PHASE #E000
UP:
        LD A,6
        CALL #141            ; Check if GRPH-key pressed (no waiting)
        AND 4
        LD (#7FFE),A         ; Send to RISKY: 0 = Continue to menu, non zero (4) = go to ROM-mode
        JR NZ,ROM_START

        LD A,32              ; 32-characters wide screen (default 29)
        LD (#F3AF),A         ; set width of screen
        LD HL,15             ; White (15) on black (0 * 256) (default depends of machine. Usually white on blue)
        LD (#F3EA),HL        ; set border color
        LD (#F3E9),HL        ; set rest of the colors
        LD A,1
        CALL #5F             ; Set screen mode
        LD HL,ARROW          ; Our custom cursor
        LD DE,#3800          ; destination in VRAM
        LD BC,8              ; bytes
        CALL #5C             ; Move
        LD A,8               ; Color = 8 (Red)
        LD HL,#1B03
        CALL #4D

LOOP:
        LD A,(#7FFF)         ; Check request from WCH CH32V303V
        CP 3                 ; #03 ?
        JR Z,ROM_START       ; Yes -> Please start ROM
        CP 4                 ; #04
        JR Z,SPRITE          ; Yes -> Please move arrow
        AND A                ; #00 = There is nothing to print
        CALL Z,WAIT           ; ... so wait until next screen refresh
        CALL NZ,#A2          ; In other case print it
        CALL #9C             ; Check if incoming byte from keyboard?
        JR Z,LOOP            ; Nothing on keyboard buffer -> LOOP
        CALL #9F             ; Read ASCII code from keyboard
        LD (#7FFD),A         ; Send it to WCH CH32V303V
        JR LOOP              ; -> LOOP


SPRITE: LD A,(#7FFF)         ; Read X
        LD HL,#1B01
        CALL #4D             ; Write X
        LD A,(#7FFF)         ; Read Y
        DEC A
        DEC L
        CALL #4D             ; Write Y
        JR LOOP              ; -> LOOP

ROM_START:
        CALL WAIT
        CALL WAIT            ; Minimum of 18ms wait for RISKY to be ready
        LD HL,(#F6B1)        ; Top of stack
        LD DE,-12
        ADD HL,DE
        PUSH HL
        LD E,(HL)
        INC HL
        LD D,(HL)
        LD HL,#7DA3          ; Expected return address
        RST #20              ; Compare
        POP HL
        JR NZ,.UNKNOWN       ; Not known return address
        LD DE,.UGLY_PATCH
        LD (HL),E            ; Replace return address
        INC HL
        LD (HL),D
        RET

.UGLY_PATCH
        POP BC               ; Modify stack
        POP HL
        JP #7D84             ; Just search this slot again
       
.UNKNOWN
        LD E,"R"             ; RISKY not able to restart slot
        CALL #FFB1           ; Call H.ERROR hook (Normally just a RET)
        RST 0                ; Reboot

WAIT:                        ; Wait until next screen refresh
        PUSH AF
        EI
        LD HL,#FC9E          ; JIFFY (time counter)
        LD A,(HL)
.LOOP   CP (HL)
        JR Z,.LOOP           ; Wait until time changes
        POP AF
        RET

UP_END:
        DEPHASE

ARROW:  DB %00100000
        DB %00110000
        DB %00111000
        DB %00111100        ; Picture of ">" Arrow
        DB %00111000
        DB %00110000
        DB %00100000
        DB %00000000