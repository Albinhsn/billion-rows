
global MaxASM
global MinASM


section .text

; RCX is A, RDX is B

MinASM:
  xor eax, eax
  mov r8d, edx
  cmp edx, ecx
  setl al
  neg eax
  xor r8d, ecx
  and eax, r8d
  xor eax, ecx
  ret


MaxASM:
  xor eax, eax
  mov r8d, edx
  cmp edx, ecx
  setg al
  neg eax
  xor r8d, ecx
  and eax, r8d
  xor eax, ecx
  ret
  
