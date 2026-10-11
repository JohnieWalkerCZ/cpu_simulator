LDI R0, 5
CALL increment
SUBI R0, 6
JZ passed
LDI R2, 1
JMP finish
passed:
LDI R2, 42
finish:
HLT
increment:
ADDI R0, 1
RET
