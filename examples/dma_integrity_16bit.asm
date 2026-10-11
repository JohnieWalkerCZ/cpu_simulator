; 16-Bit RISC: DMA copy, XOR protection, integrity check, and terminal report.
; Load this with configs/16bit.json. It uses the SecureDMA_and_RNG MMIO block
; at 0xF200 and writes its report to the Terminal at 0xF000.

.equ TERMINAL  0xF000
.equ DMA_SRC   0xF200
.equ DMA_DST   0xF202
.equ DMA_SIZE  0xF204
.equ DMA_KEY   0xF206
.equ DMA_CTRL  0xF208
.equ DMA_STAT  0xF20A
.equ PAYLOAD_BYTES 24
.equ PAYLOAD_WORDS 12

JMP main
NOP                         ; align the 16-bit payload after the 3-byte JMP

payload:
  .dw 0x4D4D, 0x494F, 0x2D44, 0x4D41, 0x2D44, 0x454D
  .dw 0x4F21, 0x1234, 0xBEEF, 0xCAFE, 0x0BAD, 0xF00D

banner:  .string "DMA integrity utility"
working: .string "Encrypt-copy-decrypt: "
passed:  .string "PASS  checksum=0x"
failed:  .string "FAIL"
newline: .string "\n"

main:
  LDI R0, banner
  CALL puts
  LDI R0, working
  CALL puts

  ; Encrypt the ROM payload into RAM at 0x9000.
  LDI R0, payload
  LDI R1, 0x9000
  LDI R2, PAYLOAD_BYTES
  LDI R3, 0x5AA5
  CALL dma_copy

  ; Decrypt it into a second RAM buffer at 0x9100.
  LDI R0, 0x9000
  LDI R1, 0x9100
  LDI R2, PAYLOAD_BYTES
  LDI R3, 0x5AA5
  CALL dma_copy

  ; The decrypted buffer must match the original payload word-for-word.
  LDI R0, payload
  LDI R1, PAYLOAD_WORDS
  CALL checksum
  MOV R6, R2

  LDI R0, 0x9100
  LDI R1, PAYLOAD_WORDS
  CALL checksum
  SUB R2, R6
  JZ verified

  LDI R0, failed
  CALL puts
  JMP finish

verified:
  LDI R0, passed
  CALL puts
  MOV R0, R6
  CALL print_hex

finish:
  LDI R0, newline
  CALL puts
  HLT

; dma_copy(R0=source, R1=destination, R2=byte count, R3=XOR key)
; CONTROL=3 starts the transfer and enables XOR with XOR_KEY.
dma_copy:
  PUSH R4
  PUSH R5
  LDI R4, DMA_SRC
  STORE R0, R4
  LDI R4, DMA_DST
  STORE R1, R4
  LDI R4, DMA_SIZE
  STORE R2, R4
  LDI R4, DMA_KEY
  STORE R3, R4
  LDI R4, DMA_CTRL
  LDI R5, 3
  STORE R5, R4
dma_wait:
  LDI R4, DMA_STAT
  LOAD R5, R4
  SUBI R5, 2              ; STATUS=2 means the DMA engine is done
  JNZ dma_wait
  POP R5
  POP R4
  RET

; checksum(R0=word pointer, R1=word count) -> R2=sum of 16-bit words.
checksum:
  PUSH R3
  PUSH R4
  LDI R2, 0
  LDI R4, 2
checksum_loop:
  LOAD R3, R0
  ADD R2, R3
  ADD R0, R4
  SUBI R1, 1
  JNZ checksum_loop
  POP R4
  POP R3
  RET

; puts(R0=zero-terminated byte string) writes characters to Terminal MMIO.
puts:
  PUSH R0
  PUSH R1
  PUSH R2
  PUSH R3
  LDI R1, TERMINAL
  LDI R2, 0x00FF
puts_loop:
  LOAD R3, R0
  AND R3, R2
  JZ puts_done
  STORE R3, R1
  ADDI R0, 1
  JMP puts_loop
puts_done:
  POP R3
  POP R2
  POP R1
  POP R0
  RET

; print_hex(R0=value) emits four uppercase hexadecimal digits.
print_hex:
  PUSH R0
  PUSH R2
  PUSH R4
  LDI R2, 0x000F
  MOV R4, R0
  SHR R4, 12
  CALL print_nibble
  MOV R4, R0
  SHR R4, 8
  CALL print_nibble
  MOV R4, R0
  SHR R4, 4
  CALL print_nibble
  MOV R4, R0
  CALL print_nibble
  POP R4
  POP R2
  POP R0
  RET

; print_nibble(R4=nibble, R2=0x000F) emits one hexadecimal digit.
print_nibble:
  PUSH R1
  AND R4, R2
  SUBI R4, 10
  JC decimal_digit
  ADDI R4, 65             ; 'A' through 'F'
  JMP emit_digit
decimal_digit:
  ADDI R4, 10             ; undo the underflow from SUBI
  ADDI R4, 48             ; '0' through '9'
emit_digit:
  LDI R1, TERMINAL
  STORE R4, R1
  POP R1
  RET
