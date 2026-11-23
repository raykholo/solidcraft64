; line.s - the 6502 half of the wireframe: lines, screen clear, font copy.
;
; Bresenham, all octants, OR-plot into the hi-res bitmap at $E000.
; The caller (view.c) clips to 320 x 200 first; this code does not check.
;
; Call: set line_x0 (word), line_y0 (byte), line_x1 (word), line_y1 (byte),
; then call line_draw. The KERNAL is banked out while the line is drawn
; (the bitmap is under it and has to be read to be ORed), with interrupts off.
;
; Two loops per major axis, one for each minor direction. The pointer into the
; bitmap is stepped, never recomputed: the bit mask rotates, +/-8 moves a
; column, +/-1 moves a row inside a character cell and +/-313 crosses to the
; next cell row. Every bitmap row address is a multiple of 8, so the low three
; bits of the pointer are y AND 7.

        .export _line_draw, _line_clear, _font_load
        .export _line_x0, _line_y0, _line_x1, _line_y1
        .export _rowlo, _rowhi, _fontbuf
        .export _mulq, _mulk, _mq_a, _mq_b, _mk_k, _mk_sh
        .importzp ptr1, ptr2, ptr3, ptr4, tmp1, tmp2, tmp3, tmp4

ptr     = ptr1          ; into the bitmap
err     = ptr2          ; error term, 16 bits signed
cnt     = ptr3          ; steps left
dx      = ptr4          ; |x1 - x0|
mask    = tmp1          ; the pixel's bit
dy      = tmp2          ; |y1 - y0|
xneg    = tmp3          ; bit 7 set: x1 < x0
yup     = tmp4          ; $FF: y1 < y0

        .bss
_line_x0:  .res 2
_line_x1:  .res 2
_line_y0:  .res 1
_line_y1:  .res 1
svbank:    .res 1
_mq_a:     .res 2       ; multiplicand (signed)
_mq_b:     .res 2       ; mulq: signed coefficient, -127..127
_mk_k:     .res 2       ; mulk: unsigned multiplier
_mk_sh:    .res 1       ; mulk: shift right
_rowlo:    .res 200
_rowhi:    .res 200
_fontbuf:  .res 512     ; glyphs for screen codes 0..63

        .code

masktab:
        .byte $80, $40, $20, $10, $08, $04, $02, $01

; ptr += 8
        .macro ptr_add8
        .local skip
        clc
        lda ptr
        adc #8
        sta ptr
        bcc skip
        inc ptr+1
skip:
        .endmacro

; ptr -= 8
        .macro ptr_sub8
        .local skip
        sec
        lda ptr
        sbc #8
        sta ptr
        bcs skip
        dec ptr+1
skip:
        .endmacro

; one pixel row down (y + 1)
        .macro row_down
        .local cross, done
        lda ptr
        and #7
        cmp #7
        beq cross
        inc ptr
        bne done                ; low bits were < 7, so ptr cannot have wrapped to 0
cross:  clc
        lda ptr
        adc #<313
        sta ptr
        lda ptr+1
        adc #>313
        sta ptr+1
done:
        .endmacro

; one pixel row up (y - 1)
        .macro row_up
        .local cross, done
        lda ptr
        and #7
        beq cross
        dec ptr
        jmp done
cross:  sec
        lda ptr
        sbc #<313
        sta ptr
        lda ptr+1
        sbc #>313
        sta ptr+1
done:
        .endmacro

        .macro plot
        lda mask
        ora (ptr),y
        sta (ptr),y
        .endmacro

; err = err - 8 bit value at \1 (16 bit signed)
        .macro err_sub src
        sec
        lda err
        sbc src
        sta err
        lda err+1
        sbc #0
        sta err+1
        .endmacro

; ---------------------------------------------------------------------------
; ptr and mask from (line_x0, line_y0)
setptr:
        ldy _line_y0
        lda _line_x0
        and #$F8
        clc
        adc _rowlo,y
        sta ptr
        lda _line_x0+1
        adc _rowhi,y
        sta ptr+1
        lda _line_x0
        and #7
        tax
        lda masktab,x
        sta mask
        rts

swapends:
        ldx _line_x0
        lda _line_x1
        sta _line_x0
        stx _line_x1
        ldx _line_x0+1
        lda _line_x1+1
        sta _line_x0+1
        stx _line_x1+1
        ldx _line_y0
        lda _line_y1
        sta _line_y0
        stx _line_y1
        rts

; ---------------------------------------------------------------------------
_line_draw:
        ; dx = x1 - x0, remembering its sign
        sec
        lda _line_x1
        sbc _line_x0
        sta dx
        lda _line_x1+1
        sbc _line_x0+1
        sta dx+1
        sta xneg
        bpl dxpos
        sec
        lda #0
        sbc dx
        sta dx
        lda #0
        sbc dx+1
        sta dx+1
dxpos:
        ; dy = |y1 - y0|
        lda _line_y1
        sec
        sbc _line_y0
        ldx #0
        bcs ydown
        eor #$FF
        adc #1                  ; carry is clear here
        ldx #$FF
ydown:  sta dy
        stx yup

        php
        sei
        lda $01
        sta svbank
        lda #$35
        sta $01
        ldy #0

        lda dx+1
        bne xmajor
        lda dx
        cmp dy
        bcs xmajor
        jmp ymajor

; ---------------------------------------------------------------------------
; x is the major axis, x0 <= x1
xmajor:
        bit xneg
        bpl xm_ok
        jsr swapends
        lda yup
        eor #$FF
        sta yup
xm_ok:  jsr setptr
        ldy #0
        lda dx+1
        lsr
        sta err+1
        lda dx
        ror
        sta err
        plot
        lda dx
        sta cnt
        lda dx+1
        sta cnt+1
        ora dx
        bne xm_go
        jmp finish              ; a single point
xm_go:  lda cnt
        beq xm_c
        inc cnt+1
xm_c:   bit yup
        bmi xm_up

xm_down:
        lsr mask
        bcc xd_nox
        ror mask
        ptr_add8
xd_nox: err_sub dy
        bpl xd_noy
        clc
        lda err
        adc dx
        sta err
        lda err+1
        adc dx+1
        sta err+1
        row_down
xd_noy: plot
        dec cnt
        beq xd_c1
        jmp xm_down
xd_c1:  dec cnt+1
        beq xd_done
        jmp xm_down
xd_done:
        jmp finish

xm_up:
        lsr mask
        bcc xu_nox
        ror mask
        ptr_add8
xu_nox: err_sub dy
        bpl xu_noy
        clc
        lda err
        adc dx
        sta err
        lda err+1
        adc dx+1
        sta err+1
        row_up
xu_noy: plot
        dec cnt
        beq xu_c1
        jmp xm_up
xu_c1:  dec cnt+1
        beq xu_done
        jmp xm_up
xu_done:
        jmp finish

; ---------------------------------------------------------------------------
; y is the major axis, y0 < y1 (dy > dx, so dx fits in a byte)
ymajor:
        bit yup
        bpl ym_ok
        jsr swapends
        lda xneg
        eor #$80
        sta xneg
ym_ok:  jsr setptr
        ldy #0
        lda dy
        lsr
        sta err
        lda #0
        sta err+1
        sta cnt+1
        lda dy
        sta cnt
        plot
        bit xneg
        bmi ym_left

ym_right:
        row_down
        err_sub dx
        bpl yr_nox
        clc
        lda err
        adc dy
        sta err
        lda err+1
        adc #0
        sta err+1
        lsr mask
        bcc yr_nox
        ror mask
        ptr_add8
yr_nox: plot
        dec cnt
        beq ydone
        jmp ym_right

ym_left:
        row_down
        err_sub dx
        bpl yl_nox
        clc
        lda err
        adc dy
        sta err
        lda err+1
        adc #0
        sta err+1
        asl mask
        bcc yl_nox
        rol mask
        ptr_sub8
yl_nox: plot
        dec cnt
        beq ydone
        jmp ym_left
ydone:

finish:
        lda svbank
        sta $01
        plp
        rts

; ---------------------------------------------------------------------------
; Clear the bitmap, $E000-$FF3F. Writes land in the RAM under the KERNAL.
_line_clear:
        lda #0
        tax
cl_loop:
        .repeat 31, page
        sta $E000 + page * 256,x
        .endrepeat
        inx
        bne cl_loop
        ldx #$3F
cl_tail:
        sta $FF00,x
        dex
        bpl cl_tail
        rts

; ---------------------------------------------------------------------------
; Copy the first 64 glyphs (screen codes $00-$3F: @, A-Z, punctuation, digits)
; of the character ROM into _fontbuf. The ROM is only visible with $01 = $33.
_font_load:
        php
        sei
        lda $01
        pha
        lda #$33
        sta $01
        ldx #0
fl_loop:
        lda $D000,x
        sta _fontbuf,x
        lda $D100,x
        sta _fontbuf+256,x
        inx
        bne fl_loop
        pla
        sta $01
        plp
        rts

; ---------------------------------------------------------------------------
; Fixed-point multiplies for the wireframe. cc65's long multiply costs a few
; thousand cycles; these cost a few hundred. Both round towards zero.

; int mulq(void): (mq_a * mq_b) >> 7, mq_b being a sine-table value
; (the caller's wrapper in view.c loads the globals).
_mulq:
        lda _mq_a+1
        eor _mq_b
        sta tmp1                ; sign of the result in bit 7
        lda _mq_a
        sta ptr1
        lda _mq_a+1
        sta ptr1+1
        bpl mq_a_pos
        sec
        lda #0
        sbc ptr1
        sta ptr1
        lda #0
        sbc ptr1+1
        sta ptr1+1
mq_a_pos:
        lda _mq_b
        bpl mq_b_pos
        eor #$FF
        clc
        adc #1
mq_b_pos:
        sta ptr3                ; multiplier, shifted out as the product shifts in
        lda #0
        sta ptr2
        sta ptr2+1
        ldx #8
mq_loop:
        lda ptr3
        lsr
        bcc mq_skip
        clc
        lda ptr2
        adc ptr1
        sta ptr2
        lda ptr2+1
        adc ptr1+1
        sta ptr2+1
mq_skip:
        ror ptr2+1
        ror ptr2
        ror ptr3
        dex
        bne mq_loop
        asl ptr3                ; product >> 7 is the top two bytes of product << 1
        rol ptr2
        rol ptr2+1
        lda ptr2
        ldx ptr2+1
        bit tmp1
        bpl mq_done
        eor #$FF
        clc
        adc #1
        pha
        txa
        eor #$FF
        adc #0
        tax
        pla
mq_done:
        rts

; int mulk(void): (mq_a * mk_k) >> mk_sh, mq_a signed, mk_k unsigned 16 bits
_mulk:
        lda _mq_a+1
        sta tmp1                ; sign
        lda _mq_a
        sta ptr1
        lda _mq_a+1
        sta ptr1+1
        bpl mk_a_pos
        sec
        lda #0
        sbc ptr1
        sta ptr1
        lda #0
        sbc ptr1+1
        sta ptr1+1
mk_a_pos:
        lda _mk_k
        sta ptr3
        lda _mk_k+1
        sta ptr3+1
        lda #0
        sta ptr2
        sta ptr2+1
        ldx #16
mk_loop:
        lda ptr3
        lsr
        bcc mk_skip
        clc
        lda ptr2
        adc ptr1
        sta ptr2
        lda ptr2+1
        adc ptr1+1
        sta ptr2+1
mk_skip:
        ror ptr2+1
        ror ptr2
        ror ptr3+1
        ror ptr3
        dex
        bne mk_loop
        ldx _mk_sh
        beq mk_shifted
mk_shift:
        lsr ptr2+1
        ror ptr2
        ror ptr3+1
        ror ptr3
        dex
        bne mk_shift
mk_shifted:
        lda ptr3
        ldx ptr3+1
        bit tmp1
        bpl mk_done
        eor #$FF
        clc
        adc #1
        pha
        txa
        eor #$FF
        adc #0
        tax
        pla
mk_done:
        rts
