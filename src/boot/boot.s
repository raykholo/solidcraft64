; boot.s - the SOLIDCRAFT boot loader.
;
; LOAD"SOLIDCRAFT",8,1 and RUN. It asks about a fast loader cartridge, then
; reads SC64.BIN through the KERNAL a byte at a time, at whatever speed the
; drive manages (about 400 bytes a second for a 1541), changing the border
; colour with every byte, which gives the stripes. There is no fast loader in here. The joke is
; that the cartridge changes nothing, and says so.

        .import __CODE_LOAD__, __CODE_RUN__, __CODE_SIZE__

CHROUT  = $FFD2
GETIN   = $FFE4
SETNAM  = $FFBD
SETLFS  = $FFBA
OPEN    = $FFC0
CLOSE   = $FFC3
CHKIN   = $FFC6
CLRCHN  = $FFCC
CHRIN   = $FFCF
READST  = $FFB7

VBORDER = $D020
VBG     = $D021

src     = $FB
dst     = $FD
val     = $FB

        .segment "LOADADDR"
        .word   $0801

        .segment "STUB"
        .word   next, 10        ; 10 SYS 2061
        .byte   $9E, "2061", 0
next:   .word   0
        ; at $080D: copy the code to $CC00 and run it there
        lda     #<__CODE_LOAD__
        sta     src
        lda     #>__CODE_LOAD__
        sta     src+1
        lda     #<__CODE_RUN__
        sta     dst
        lda     #>__CODE_RUN__
        sta     dst+1
        ldx     #>(__CODE_SIZE__ + 255)
        ldy     #0
:       lda     (src),y
        sta     (dst),y
        iny
        bne     :-
        inc     src+1
        inc     dst+1
        dex
        bne     :-
        jmp     __CODE_RUN__

        .segment "CODE"
start:
        lda     #$93            ; clear
        jsr     CHROUT
        lda     #<m_title
        ldx     #>m_title
        jsr     print
ask:    jsr     GETIN
        cmp     #'N'
        beq     no
        cmp     #'Y'
        bne     ask
        jsr     CHROUT
        lda     #<m_fast
        ldx     #>m_fast
        jsr     print
        jmp     go
failj:  jmp     fail
no:     jsr     CHROUT
        lda     #13
        jsr     CHROUT
go:
        lda     #<m_load
        ldx     #>m_load
        jsr     print

        lda     $DC0F           ; stopwatch: the CIA's time-of-day clock, which
        and     #$7F            ; keeps time even when the serial bus has the
        sta     $DC0F           ; interrupts switched off (and the jiffy clock stops)
        lda     #0
        sta     $DC0B
        sta     $DC0A
        sta     $DC09
        sta     $DC08           ; writing the tenths starts it
        lda     VBORDER
        sta     oldborder

        lda     #fname_end-fname
        ldx     #<fname
        ldy     #>fname
        jsr     SETNAM
        lda     #2
        ldx     #8
        ldy     #2
        jsr     SETLFS
        jsr     OPEN
        bcs     failj
        ldx     #2
        jsr     CHKIN
        bcs     failj
        jsr     CHRIN           ; the file's load address
        sta     dst
        jsr     CHRIN
        sta     dst+1
        jsr     READST
        bne     failj            ; file not found, or the drive isn't there

        ; One byte at a time through the KERNAL, the border changing colour
        ; with every one: the stripes. The speed is the drive's.
rd:     jsr     CHRIN
        ldy     #0
        sta     (dst),y
        inc     dst
        bne     :+
        inc     dst+1
:       lda     cnt
        and     #$07
        tay
        lda     colours,y
        sta     VBORDER
        inc     cnt
        jsr     READST
        beq     rd
        and     #$BF            ; end of file is fine
        bne     failj
        jsr     CLRCHN
        lda     #2
        jsr     CLOSE
        lda     oldborder
        sta     VBORDER

        lda     $DC0B           ; seconds taken -> $033C/$033D, flagged at $033B
        lda     $DC0A
        jsr     bcd
        tax
        lda     #0
        sta     $033C
        sta     $033D
        cpx     #0
        beq     nomin
minl:   clc
        lda     $033C
        adc     #60
        sta     $033C
        bcc     :+
        inc     $033D
:       dex
        bne     minl
nomin:  lda     $DC09
        jsr     bcd
        clc
        adc     $033C
        sta     $033C
        bcc     :+
        inc     $033D
:       lda     $DC08           ; unlatch
        lda     #$53
        sta     $033B

        ; SYS the address in the program's own BASIC line
        lda     #0
        sta     val
        sta     val+1
        ldy     #5
sysl:   lda     $0801,y
        iny
        cmp     #$20
        beq     sysl
        sec
        sbc     #'0'
        cmp     #10
        bcs     sysgo
        pha
        asl     val             ; val = val * 10
        rol     val+1
        lda     val
        ldx     val+1
        asl     val
        rol     val+1
        asl     val
        rol     val+1
        clc
        adc     val
        sta     val
        txa
        adc     val+1
        sta     val+1
        pla
        clc
        adc     val
        sta     val
        bcc     sysl
        inc     val+1
        jmp     sysl
sysgo:  jmp     (val)

fail:   jsr     CLRCHN
        lda     #2
        jsr     CLOSE
        lda     oldborder
        sta     VBORDER
        lda     #<m_fail
        ldx     #>m_fail
        jsr     print
        rts

bcd:    sta     tmp             ; BCD in A -> binary in A
        lsr
        lsr
        lsr
        lsr
        asl
        sta     tmp2
        asl
        asl
        clc
        adc     tmp2
        sta     tmp2
        lda     tmp
        and     #$0F
        clc
        adc     tmp2
        rts

print:  sta     src
        stx     src+1
        ldy     #0
:       lda     (src),y
        beq     :+
        jsr     CHROUT
        iny
        bne     :-
:       rts

colours:    .byte 2, 0, 6, 0, 5, 0, 14, 0     ; red, black, blue, black, green, ...

cnt:        .byte 0
oldborder:  .byte 0
tmp:        .byte 0
tmp2:       .byte 0

fname:      .byte "SC64.BIN"
fname_end:

; PETSCII, upper case/graphics: A-Z are $41-$5A.
m_title:    .byte 13, "SOLIDCRAFT 64 BOOT LOADER V1.0", 13
            .byte "(C) 1984 SOLIDCRAFT 64", 13, 13
            .byte "FAST LOADER CARTRIDGE FITTED? (Y/N) ", 0
m_fast:     .byte 13, 13, "ZAPLOAD 64 CARTRIDGE V2.1", 13
            .byte "(C) 1984 BYTEWISE LTD", 13
            .byte "SPEEDUP: 1.00X (SEE MANUAL)", 13, 0
m_load:     .byte 13, "SEARCHING FOR SC64.BIN", 13, "LOADING", 13, 0
m_fail:     .byte "?LOAD ERROR", 13, 0
