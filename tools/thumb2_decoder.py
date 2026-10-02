"""
Comprehensive ARMv7-M Thumb2 instruction decoder.
Covers 16-bit and 32-bit Thumb instructions for Cortex-M4(F).
"""

def _reg(n):
    if n == 13: return "sp"
    if n == 14: return "lr"
    if n == 15: return "pc"
    return f"r{n}"

def _shift_type(t):
    return ["lsl", "lsr", "asr", "ror"][t & 3]

def _thumb_expand_imm(i_imm3_imm8):
    """ARMv7-M ThumbExpandImm: i:imm3:imm8 -> 32-bit constant"""
    val = i_imm3_imm8 & 0xFFF
    rot = (val >> 8) & 0xF
    imm8 = val & 0xFF
    if rot == 0:
        return imm8
    elif rot == 1:
        return (imm8 << 16) | imm8
    elif rot == 2:
        return (imm8 << 24) | (imm8 << 8)
    elif rot == 3:
        return (imm8 << 24) | (imm8 << 16) | (imm8 << 8) | imm8
    else:
        # ROR of 1:imm8 by 2*rot
        unrot = 0x80 | (imm8 & 0x7F)
        shift = rot * 2
        return ((unrot << (32 - shift)) | (unrot >> shift)) & 0xFFFFFFFF

def _reglist(mask, include_pc=False, include_lr=False):
    regs = []
    for i in range(13):
        if mask & (1 << i):
            regs.append(_reg(i))
    if include_lr and (mask & (1 << 14)):
        regs.append("lr")
    if include_pc and (mask & (1 << 15)):
        regs.append("pc")
    return "{" + ", ".join(regs) + "}"

def decode_16(hw, addr):
    """Decode a 16-bit Thumb instruction. Returns (mnemonic, operands_str) or None."""
    
    # PUSH {reglist}
    if (hw & 0xFE00) == 0xB400:
        regs = hw & 0xFF
        lr = (hw >> 8) & 1
        rlist = [_reg(i) for i in range(8) if regs & (1 << i)]
        if lr: rlist.append("lr")
        return f"push {{{', '.join(rlist)}}}"
    
    # POP {reglist}
    if (hw & 0xFE00) == 0xBC00:
        regs = hw & 0xFF
        pc = (hw >> 8) & 1
        rlist = [_reg(i) for i in range(8) if regs & (1 << i)]
        if pc: rlist.append("pc")
        return f"pop {{{', '.join(rlist)}}}"
    
    # LDR Rd, [PC, #imm] (literal pool load)
    if (hw & 0xF800) == 0x4800:
        rd = (hw >> 8) & 7
        imm = (hw & 0xFF) * 4
        pc_al = (addr + 4) & ~3
        target = pc_al + imm
        return f"ldr {_reg(rd)}, [pc, #{imm}]", target  # caller resolves literal
    
    # MOV Rd, #imm8
    if (hw & 0xF800) == 0x2000:
        rd = (hw >> 8) & 7
        imm = hw & 0xFF
        return f"movs {_reg(rd)}, #{imm}"
    
    # CMP Rn, #imm8
    if (hw & 0xF800) == 0x2800:
        rn = (hw >> 8) & 7
        imm = hw & 0xFF
        return f"cmp {_reg(rn)}, #{imm}"
    
    # ADD Rd, #imm8
    if (hw & 0xF800) == 0x3000:
        rd = (hw >> 8) & 7
        imm = hw & 0xFF
        return f"adds {_reg(rd)}, #{imm}"
    
    # SUB Rd, #imm8
    if (hw & 0xF800) == 0x3800:
        rd = (hw >> 8) & 7
        imm = hw & 0xFF
        return f"subs {_reg(rd)}, #{imm}"
    
    # STR Rd, [Rn, #imm5*4]
    if (hw & 0xF800) == 0x6000:
        imm = ((hw >> 6) & 0x1F) * 4
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"str {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # LDR Rd, [Rn, #imm5*4]
    if (hw & 0xF800) == 0x6800:
        imm = ((hw >> 6) & 0x1F) * 4
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"ldr {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # STRB Rd, [Rn, #imm5]
    if (hw & 0xF800) == 0x7000:
        imm = (hw >> 6) & 0x1F
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"strb {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # LDRB Rd, [Rn, #imm5]
    if (hw & 0xF800) == 0x7800:
        imm = (hw >> 6) & 0x1F
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"ldrb {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # STRH Rd, [Rn, #imm5*2]
    if (hw & 0xF800) == 0x8000:
        imm = ((hw >> 6) & 0x1F) * 2
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"strh {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # LDRH Rd, [Rn, #imm5*2]
    if (hw & 0xF800) == 0x8800:
        imm = ((hw >> 6) & 0x1F) * 2
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"ldrh {_reg(rd)}, [{_reg(rn)}, #{imm}]"
    
    # STR Rd, [SP, #imm8*4]
    if (hw & 0xF800) == 0x9000:
        rd = (hw >> 8) & 7
        imm = (hw & 0xFF) * 4
        return f"str {_reg(rd)}, [sp, #{imm}]"
    
    # LDR Rd, [SP, #imm8*4]
    if (hw & 0xF800) == 0x9800:
        rd = (hw >> 8) & 7
        imm = (hw & 0xFF) * 4
        return f"ldr {_reg(rd)}, [sp, #{imm}]"
    
    # ADD Rd, PC, #imm8*4 (ADR)
    if (hw & 0xF800) == 0xA000:
        rd = (hw >> 8) & 7
        imm = (hw & 0xFF) * 4
        return f"adr {_reg(rd)}, #{imm}"
    
    # ADD Rd, SP, #imm8*4
    if (hw & 0xF800) == 0xA800:
        rd = (hw >> 8) & 7
        imm = (hw & 0xFF) * 4
        return f"add {_reg(rd)}, sp, #{imm}"
    
    # ADD SP, #imm7*4
    if (hw & 0xFF80) == 0xB000:
        imm = (hw & 0x7F) * 4
        return f"add sp, #{imm}"
    
    # SUB SP, #imm7*4
    if (hw & 0xFF80) == 0xB080:
        imm = (hw & 0x7F) * 4
        return f"sub sp, #{imm}"
    
    # BX Rm
    if (hw & 0xFF87) == 0x4700:
        rm = (hw >> 3) & 0xF
        return f"bx {_reg(rm)}"
    
    # BLX Rm
    if (hw & 0xFF87) == 0x4780:
        rm = (hw >> 3) & 0xF
        return f"blx {_reg(rm)}"
    
    # MOV Rd, Rm (high registers)
    if (hw & 0xFF00) == 0x4600:
        rd = ((hw >> 4) & 8) | (hw & 7)
        rm = (hw >> 3) & 0xF
        return f"mov {_reg(rd)}, {_reg(rm)}"
    
    # CMP Rn, Rm (high registers)
    if (hw & 0xFF00) == 0x4500:
        rn = ((hw >> 4) & 8) | (hw & 7)
        rm = (hw >> 3) & 0xF
        return f"cmp {_reg(rn)}, {_reg(rm)}"
    
    # ADD Rd, Rm (high registers)
    if (hw & 0xFF00) == 0x4400:
        rd = ((hw >> 4) & 8) | (hw & 7)
        rm = (hw >> 3) & 0xF
        return f"add {_reg(rd)}, {_reg(rm)}"
    
    # Data processing (register-register), 16-bit
    if (hw & 0xFC00) == 0x4000:
        op = (hw >> 6) & 0xF
        rm = (hw >> 3) & 7
        rd = hw & 7
        ops = ["ands","eors","lsls","lsrs","asrs","adcs","sbcs","rors",
               "tst","rsbs","cmp","cmn","orrs","muls","bics","mvns"]
        return f"{ops[op]} {_reg(rd)}, {_reg(rm)}"
    
    # ADD/SUB 3-register
    if (hw & 0xFE00) == 0x1800:
        rm = (hw >> 6) & 7
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"adds {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
    if (hw & 0xFE00) == 0x1A00:
        rm = (hw >> 6) & 7
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"subs {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
    
    # ADD/SUB 3-bit immediate
    if (hw & 0xFE00) == 0x1C00:
        imm = (hw >> 6) & 7
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"adds {_reg(rd)}, {_reg(rn)}, #{imm}"
    if (hw & 0xFE00) == 0x1E00:
        imm = (hw >> 6) & 7
        rn = (hw >> 3) & 7
        rd = hw & 7
        return f"subs {_reg(rd)}, {_reg(rn)}, #{imm}"
    
    # LSL/LSR/ASR immediate
    if (hw & 0xF800) == 0x0000:
        imm = (hw >> 6) & 0x1F
        rm = (hw >> 3) & 7
        rd = hw & 7
        if imm == 0: return f"movs {_reg(rd)}, {_reg(rm)}"
        return f"lsls {_reg(rd)}, {_reg(rm)}, #{imm}"
    if (hw & 0xF800) == 0x0800:
        imm = (hw >> 6) & 0x1F
        if imm == 0: imm = 32
        rm = (hw >> 3) & 7
        rd = hw & 7
        return f"lsrs {_reg(rd)}, {_reg(rm)}, #{imm}"
    if (hw & 0xF800) == 0x1000:
        imm = (hw >> 6) & 0x1F
        if imm == 0: imm = 32
        rm = (hw >> 3) & 7
        rd = hw & 7
        return f"asrs {_reg(rd)}, {_reg(rm)}, #{imm}"
    
    # Conditional branch B<cond> (16-bit)
    if (hw & 0xF000) == 0xD000:
        cond = (hw >> 8) & 0xF
        if cond == 0xE: return f"udf #{hw & 0xFF}"  # permanently undefined
        if cond == 0xF: return f"svc #{hw & 0xFF}"   # SVC
        off = hw & 0xFF
        if off & 0x80: off -= 0x100
        target = addr + 4 + off * 2
        conds = ["eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le"]
        return f"b{conds[cond]} 0x{target:08x}"
    
    # Unconditional branch B (16-bit)
    if (hw & 0xF800) == 0xE000:
        off = hw & 0x7FF
        if off & 0x400: off -= 0x800
        target = addr + 4 + off * 2
        return f"b 0x{target:08x}"
    
    # SXTH/SXTB/UXTH/UXTB
    if (hw & 0xFFC0) == 0xB200:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"sxth {_reg(rd)}, {_reg(rm)}"
    if (hw & 0xFFC0) == 0xB240:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"sxtb {_reg(rd)}, {_reg(rm)}"
    if (hw & 0xFFC0) == 0xB280:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"uxth {_reg(rd)}, {_reg(rm)}"
    if (hw & 0xFFC0) == 0xB2C0:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"uxtb {_reg(rd)}, {_reg(rm)}"
    
    # REV/REV16/REVSH
    if (hw & 0xFFC0) == 0xBA00:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"rev {_reg(rd)}, {_reg(rm)}"
    if (hw & 0xFFC0) == 0xBA40:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"rev16 {_reg(rd)}, {_reg(rm)}"
    if (hw & 0xFFC0) == 0xBAC0:
        rm = (hw >> 3) & 7; rd = hw & 7
        return f"revsh {_reg(rd)}, {_reg(rm)}"
    
    # LDMIA
    if (hw & 0xF800) == 0xC800:
        rn = (hw >> 8) & 7
        regs = hw & 0xFF
        rlist = [_reg(i) for i in range(8) if regs & (1 << i)]
        wb = "!" if not (regs & (1 << rn)) else ""
        return f"ldmia {_reg(rn)}{wb}, {{{', '.join(rlist)}}}"
    
    # STMIA
    if (hw & 0xF800) == 0xC000:
        rn = (hw >> 8) & 7
        regs = hw & 0xFF
        rlist = [_reg(i) for i in range(8) if regs & (1 << i)]
        return f"stmia {_reg(rn)}!, {{{', '.join(rlist)}}}"
    
    # LDR Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5800:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"ldr {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # LDRH Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5A00:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"ldrh {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # LDRB Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5C00:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"ldrb {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # LDRSB Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5600:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"ldrsb {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # LDRSH Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5E00:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"ldrsh {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # STR Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5000:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"str {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # STRH Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5200:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"strh {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    # STRB Rd, [Rn, Rm]
    if (hw & 0xFE00) == 0x5400:
        rm = (hw >> 6) & 7; rn = (hw >> 3) & 7; rd = hw & 7
        return f"strb {_reg(rd)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # CBNZ / CBZ
    if (hw & 0xF500) == 0xB100:
        rn = hw & 7
        imm = ((hw >> 2) & 0x3E) | ((hw >> 3) & 0x40)
        target = addr + 4 + imm
        return f"cbnz {_reg(rn)}, 0x{target:08x}"
    if (hw & 0xF500) == 0xB000 and (hw & 0x0800):
        rn = hw & 7
        imm = ((hw >> 2) & 0x3E) | ((hw >> 3) & 0x40)
        target = addr + 4 + imm
        return f"cbz {_reg(rn)}, 0x{target:08x}"
    
    # IT block
    if (hw & 0xFF00) == 0xBF00 and (hw & 0xF):
        cond = (hw >> 4) & 0xF
        if cond >= 14:
            return None  # AL/undefined condition in IT block
        mask = hw & 0xF
        conds = ["eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le"]
        # Count trailing 1s in mask for then/else
        it = "i"
        bits = [(mask >> i) & 1 for i in range(3, -1, -1)]
        base = cond & 1
        suffix = ""
        for i in range(1, 4):
            if bits[i] == 0 and i < 4:
                if (mask >> (4 - i - 1)) & 1:
                    pass  # more to come
            b = (mask >> (3 - i)) & 1
            if i > 0 and (mask & (0xF >> i)):
                suffix += "t" if b == base else "e"
        return f"it{suffix} {conds[cond]}"
    
    # NOP, WFI, WFE, SEV, YIELD
    if hw == 0xBF00: return "nop"
    if hw == 0xBF20: return "wfe"
    if hw == 0xBF30: return "wfi"
    if hw == 0xBF40: return "sev"
    if hw == 0xBF10: return "yield"
    
    # BKPT
    if (hw & 0xFF00) == 0xBE00:
        return f"bkpt #{hw & 0xFF}"
    
    return None


def decode_32(hw1, hw2, addr):
    """Decode a 32-bit Thumb2 instruction. Returns string or None."""
    
    full = (hw1 << 16) | hw2
    
    # === BL / B.W ===
    if (hw1 & 0xF800) == 0xF000:
        s_bit = (hw1 >> 10) & 1
        # BL: hw2[15:14]=11, hw2[12]=1
        if (hw2 & 0xD000) == 0xD000:
            j1 = (hw2 >> 13) & 1; j2 = (hw2 >> 11) & 1
            imm10 = hw1 & 0x3FF; imm11 = hw2 & 0x7FF
            i1 = ~(j1 ^ s_bit) & 1; i2 = ~(j2 ^ s_bit) & 1
            offset = (s_bit << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
            if s_bit: offset |= 0xFE000000
            if offset & 0x80000000: offset -= 0x100000000
            target = addr + 4 + offset
            return f"bl 0x{target:08x}"
        # B.W: hw2[15:14]=10, hw2[12]=1
        if (hw2 & 0xD000) == 0x9000:
            j1 = (hw2 >> 13) & 1; j2 = (hw2 >> 11) & 1
            imm10 = hw1 & 0x3FF; imm11 = hw2 & 0x7FF
            i1 = ~(j1 ^ s_bit) & 1; i2 = ~(j2 ^ s_bit) & 1
            offset = (s_bit << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
            if s_bit: offset |= 0xFE000000
            if offset & 0x80000000: offset -= 0x100000000
            target = addr + 4 + offset
            return f"b.w 0x{target:08x}"
        # Conditional B.W: hw2[15:14]=10, hw2[12]=0
        if (hw2 & 0xD000) == 0x8000:
            cond = (hw1 >> 6) & 0xF
            s_bit = (hw1 >> 10) & 1
            j1 = (hw2 >> 13) & 1; j2 = (hw2 >> 11) & 1
            imm6 = hw1 & 0x3F; imm11 = hw2 & 0x7FF
            offset = (s_bit << 20) | (j2 << 19) | (j1 << 18) | (imm6 << 12) | (imm11 << 1)
            if s_bit: offset |= 0xFFE00000
            if offset & 0x80000000: offset -= 0x100000000
            target = addr + 4 + offset
            conds = ["eq","ne","cs","cc","mi","pl","vs","vc","hi","ls","ge","lt","gt","le"]
            if cond < 14:
                return f"b{conds[cond]}.w 0x{target:08x}"
    
    # === Data processing (modified immediate) F0xx-F1xx ===
    # AND, BIC, ORR, ORN, EOR, TEQ, ADD, ADC, SBC, SUB, RSB, CMP, CMN, MOV, MVN
    if (hw1 & 0xFA00) == 0xF000 and (hw2 & 0x8000) == 0:
        op = (hw1 >> 5) & 0xF  # bits[8:5] of hw1
        s = (hw1 >> 4) & 1     # S bit
        rn = hw1 & 0xF
        rd = (hw2 >> 8) & 0xF
        # Reconstruct i:imm3:imm8
        i = (hw1 >> 10) & 1
        imm3 = (hw2 >> 12) & 7
        imm8 = hw2 & 0xFF
        imm12 = (i << 11) | (imm3 << 8) | imm8
        imm32 = _thumb_expand_imm(imm12)
        sf = "s" if s else ""
        
        if op == 0:  # AND / TST
            if rd == 15 and s: return f"tst {_reg(rn)}, #0x{imm32:x}"
            return f"and{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 1:  # BIC
            return f"bic{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 2:  # ORR / MOV
            if rn == 15: return f"mov{sf}.w {_reg(rd)}, #0x{imm32:x}"
            return f"orr{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 3:  # ORN / MVN
            if rn == 15: return f"mvn{sf} {_reg(rd)}, #0x{imm32:x}"
            return f"orn{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 4:  # EOR / TEQ
            if rd == 15 and s: return f"teq {_reg(rn)}, #0x{imm32:x}"
            return f"eor{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 8:  # ADD / CMN
            if rd == 15 and s: return f"cmn {_reg(rn)}, #0x{imm32:x}"
            return f"add{sf}.w {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 10:  # ADC
            return f"adc{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 11:  # SBC
            return f"sbc{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 13:  # SUB / CMP
            if rd == 15 and s: return f"cmp.w {_reg(rn)}, #0x{imm32:x}"
            return f"sub{sf}.w {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
        elif op == 14:  # RSB
            return f"rsb{sf} {_reg(rd)}, {_reg(rn)}, #0x{imm32:x}"
    
    # === MOVW (imm16) ===
    if (hw1 & 0xFBF0) == 0xF240 and (hw2 & 0x8000) == 0:
        imm4 = hw1 & 0xF
        i = (hw1 >> 10) & 1
        imm3 = (hw2 >> 12) & 7
        imm8 = hw2 & 0xFF
        rd = (hw2 >> 8) & 0xF
        imm16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8
        return f"movw {_reg(rd)}, #0x{imm16:04x}"
    
    # === MOVT (imm16) ===
    if (hw1 & 0xFBF0) == 0xF2C0 and (hw2 & 0x8000) == 0:
        imm4 = hw1 & 0xF
        i = (hw1 >> 10) & 1
        imm3 = (hw2 >> 12) & 7
        imm8 = hw2 & 0xFF
        rd = (hw2 >> 8) & 0xF
        imm16 = (imm4 << 12) | (i << 11) | (imm3 << 8) | imm8
        return f"movt {_reg(rd)}, #0x{imm16:04x}"
    
    # === ADDW / SUBW (12-bit immediate) ===
    if (hw1 & 0xFBF0) == 0xF200 and (hw2 & 0x8000) == 0:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        i = (hw1 >> 10) & 1; imm3 = (hw2 >> 12) & 7; imm8 = hw2 & 0xFF
        imm12 = (i << 11) | (imm3 << 8) | imm8
        return f"addw {_reg(rd)}, {_reg(rn)}, #{imm12}"
    if (hw1 & 0xFBF0) == 0xF2A0 and (hw2 & 0x8000) == 0:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        i = (hw1 >> 10) & 1; imm3 = (hw2 >> 12) & 7; imm8 = hw2 & 0xFF
        imm12 = (i << 11) | (imm3 << 8) | imm8
        return f"subw {_reg(rd)}, {_reg(rn)}, #{imm12}"
    
    # === Data processing (shifted register) EAxx-EBxx ===
    if (hw1 & 0xFE00) == 0xEA00 and (hw2 & 0x8000) == 0:
        op = (hw1 >> 5) & 0xF
        s = (hw1 >> 4) & 1
        rn = hw1 & 0xF
        rd = (hw2 >> 8) & 0xF
        rm = hw2 & 0xF
        imm3 = (hw2 >> 12) & 7
        imm2 = (hw2 >> 6) & 3
        stype = (hw2 >> 4) & 3
        shift_amt = (imm3 << 2) | imm2
        sf = "s" if s else ""
        
        shift_str = ""
        if shift_amt > 0:
            shift_str = f", {_shift_type(stype)} #{shift_amt}"
        elif stype == 3:
            shift_str = ", rrx"
        
        if op == 0:
            if rd == 15 and s: return f"tst.w {_reg(rn)}, {_reg(rm)}{shift_str}"
            return f"and{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 1:
            return f"bic{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 2:
            if rn == 15:
                if shift_amt > 0: return f"{_shift_type(stype)}{sf}.w {_reg(rd)}, {_reg(rm)}, #{shift_amt}"
                return f"mov{sf}.w {_reg(rd)}, {_reg(rm)}{shift_str}"
            return f"orr{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 3:
            if rn == 15: return f"mvn{sf}.w {_reg(rd)}, {_reg(rm)}{shift_str}"
            return f"orn{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 4:
            if rd == 15 and s: return f"teq.w {_reg(rn)}, {_reg(rm)}{shift_str}"
            return f"eor{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 6:
            return f"pkh {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 8:
            if rd == 15 and s: return f"cmn.w {_reg(rn)}, {_reg(rm)}{shift_str}"
            return f"add{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 10:
            return f"adc{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 11:
            return f"sbc{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 13:
            if rd == 15 and s: return f"cmp.w {_reg(rn)}, {_reg(rm)}{shift_str}"
            return f"sub{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
        elif op == 14:
            return f"rsb{sf}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}{shift_str}"
    
    # === Load/Store single (F8xx-F9xx) ===
    if (hw1 & 0xFE00) == 0xF800:
        op1 = (hw1 >> 5) & 7  # size + sign
        rn = hw1 & 0xF
        rt = (hw2 >> 12) & 0xF
        
        # Determine operation
        size_names = {0: ("strb", 1), 1: ("strh", 2), 2: ("str", 4), 3: ("???", 0),
                      4: ("ldrb", 1), 5: ("ldrh", 2), 6: ("ldr", 4), 7: ("???", 0)}
        # Actually encode differently:
        # hw1[7:5] encodes the operation
        # But the encoding is more nuanced. Let me use hw1[6:4]
        pass  # handled below
    
    # More specific load/store patterns:
    # LDR.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF8D0:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        if rn == 15:
            target = ((addr + 4) & ~3) + imm12
            return f"ldr.w {_reg(rt)}, [pc, #{imm12}]", target
        return f"ldr.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # LDR.W Rt, [Rn, #-imm8] or with pre/post index
    if (hw1 & 0xFFF0) == 0xF850 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if rn == 13 and p == 0 and u == 1 and w == 1 and imm8 == 4:
            return f"pop.w {{{_reg(rt)}}}"
        if p and not w:
            return f"ldr.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w:
            return f"ldr.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else:
            return f"ldr.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # LDR.W Rt, [Rn, Rm, LSL #imm2]
    if (hw1 & 0xFFF0) == 0xF850 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"ldr.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"ldr.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # STR.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF8C0:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"str.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # STR.W Rt, [Rn, #-imm8] / pre/post
    if (hw1 & 0xFFF0) == 0xF840 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if rn == 13 and p == 1 and u == 0 and w == 1 and imm8 == 4:
            return f"push.w {{{_reg(rt)}}}"
        if p and not w:
            return f"str.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w:
            return f"str.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else:
            return f"str.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # STR.W Rt, [Rn, Rm, LSL #imm2]
    if (hw1 & 0xFFF0) == 0xF840 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"str.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"str.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # LDRB.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF890:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        if rn == 15:
            target = ((addr + 4) & ~3) + imm12
            return f"ldrb.w {_reg(rt)}, [pc, #{imm12}]", target
        return f"ldrb.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # LDRB.W Rt, [Rn, #-imm8]
    if (hw1 & 0xFFF0) == 0xF810 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if p and not w: return f"ldrb.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w: return f"ldrb.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else: return f"ldrb.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # LDRB.W Rt, [Rn, Rm, LSL #imm2]
    if (hw1 & 0xFFF0) == 0xF810 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"ldrb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"ldrb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # STRB.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF880:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"strb.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # STRB.W Rt, [Rn, #-imm8]
    if (hw1 & 0xFFF0) == 0xF800 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if p and not w: return f"strb.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w: return f"strb.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else: return f"strb.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # STRB.W Rt, [Rn, Rm, LSL #]
    if (hw1 & 0xFFF0) == 0xF800 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"strb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"strb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # LDRH.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF8B0:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"ldrh.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # LDRH.W Rt, [Rn, #-imm8]
    if (hw1 & 0xFFF0) == 0xF830 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if p and not w: return f"ldrh.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w: return f"ldrh.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else: return f"ldrh.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # LDRH.W Rt, [Rn, Rm, LSL #]
    if (hw1 & 0xFFF0) == 0xF830 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"ldrh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"ldrh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # STRH.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF8A0:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"strh.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # STRH.W Rt, [Rn, #-imm8]
    if (hw1 & 0xFFF0) == 0xF820 and (hw2 & 0x0800) == 0x0800:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF
        p = (hw2 >> 10) & 1; u = (hw2 >> 9) & 1; w = (hw2 >> 8) & 1
        imm8 = hw2 & 0xFF
        off = imm8 if u else -imm8
        if p and not w: return f"strh.w {_reg(rt)}, [{_reg(rn)}, #{off}]"
        elif p and w: return f"strh.w {_reg(rt)}, [{_reg(rn)}, #{off}]!"
        else: return f"strh.w {_reg(rt)}, [{_reg(rn)}], #{off}"
    
    # STRH.W Rt, [Rn, Rm, LSL #]
    if (hw1 & 0xFFF0) == 0xF820 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"strh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"strh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # LDRSB.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF990:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"ldrsb.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # LDRSB.W Rt, [Rn, Rm, LSL #]
    if (hw1 & 0xFFF0) == 0xF910 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"ldrsb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"ldrsb.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # LDRSH.W Rt, [Rn, #imm12]
    if (hw1 & 0xFFF0) == 0xF9B0:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; imm12 = hw2 & 0xFFF
        return f"ldrsh.w {_reg(rt)}, [{_reg(rn)}, #{imm12}]"
    
    # LDRSH.W Rt, [Rn, Rm, LSL #]
    if (hw1 & 0xFFF0) == 0xF930 and (hw2 & 0x0FC0) == 0x0000:
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rm = hw2 & 0xF
        shift = (hw2 >> 4) & 3
        if shift: return f"ldrsh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}, lsl #{shift}]"
        return f"ldrsh.w {_reg(rt)}, [{_reg(rn)}, {_reg(rm)}]"
    
    # === LDM / STM / PUSH.W / POP.W (E8xx-E9xx) ===
    
    # PUSH.W (STMDB SP!, {reglist})
    if (hw1 & 0xFFFF) == 0xE92D:
        regs = hw2
        rlist = [_reg(i) for i in range(16) if regs & (1 << i)]
        return f"push.w {{{', '.join(rlist)}}}"
    
    # POP.W (LDMIA SP!, {reglist})
    if (hw1 & 0xFFFF) == 0xE8BD:
        regs = hw2
        rlist = [_reg(i) for i in range(16) if regs & (1 << i)]
        return f"pop.w {{{', '.join(rlist)}}}"
    
    # STMIA.W Rn!, {reglist}
    if (hw1 & 0xFFF0) == 0xE880:
        rn = hw1 & 0xF
        w = (hw1 >> 5) & 1  # actually bit 5 of hw1
        regs = hw2
        rlist = [_reg(i) for i in range(16) if regs & (1 << i)]
        wb = "!" if (hw1 & 0x0020) else ""
        return f"stmia.w {_reg(rn)}{wb}, {{{', '.join(rlist)}}}"
    
    # STMDB Rn!, {reglist}
    if (hw1 & 0xFFF0) == 0xE900:
        rn = hw1 & 0xF
        regs = hw2
        rlist = [_reg(i) for i in range(16) if regs & (1 << i)]
        wb = "!" if (hw1 & 0x0020) else ""
        return f"stmdb {_reg(rn)}{wb}, {{{', '.join(rlist)}}}"
    
    # LDMIA.W Rn!, {reglist}  (non-SP)
    if (hw1 & 0xFFF0) == 0xE890:
        rn = hw1 & 0xF
        regs = hw2
        rlist = [_reg(i) for i in range(16) if regs & (1 << i)]
        wb = "!" if (hw1 & 0x0020) else ""
        return f"ldmia.w {_reg(rn)}{wb}, {{{', '.join(rlist)}}}"
    
    # LDRD Rt, Rt2, [Rn, #imm8*4]
    if (hw1 & 0xFE50) == 0xE850 and (hw2 & 0x0000) == 0:
        p = (hw1 >> 8) & 1; u = (hw1 >> 7) & 1; w = (hw1 >> 5) & 1
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rt2 = (hw2 >> 8) & 0xF
        imm8 = hw2 & 0xFF
        off = imm8 * 4
        if not u: off = -off
        if rn == 15:
            target = ((addr + 4) & ~3) + off
            return f"ldrd {_reg(rt)}, {_reg(rt2)}, [pc, #{off}]", target
        if p and not w:
            return f"ldrd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}, #{off}]"
        elif p and w:
            return f"ldrd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}, #{off}]!"
        else:
            return f"ldrd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}], #{off}"
    
    # STRD Rt, Rt2, [Rn, #imm8*4]
    if (hw1 & 0xFE50) == 0xE840 and (hw2 & 0x0000) == 0:
        p = (hw1 >> 8) & 1; u = (hw1 >> 7) & 1; w = (hw1 >> 5) & 1
        rn = hw1 & 0xF; rt = (hw2 >> 12) & 0xF; rt2 = (hw2 >> 8) & 0xF
        imm8 = hw2 & 0xFF
        off = imm8 * 4
        if not u: off = -off
        if p and not w:
            return f"strd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}, #{off}]"
        elif p and w:
            return f"strd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}, #{off}]!"
        else:
            return f"strd {_reg(rt)}, {_reg(rt2)}, [{_reg(rn)}], #{off}"
    
    # TBB / TBH
    if (hw1 & 0xFFF0) == 0xE8D0 and (hw2 & 0xFFE0) == 0xF000:
        rn = hw1 & 0xF; rm = hw2 & 0xF
        h = (hw2 >> 4) & 1
        if h: return f"tbh [{_reg(rn)}, {_reg(rm)}, lsl #1]"
        return f"tbb [{_reg(rn)}, {_reg(rm)}]"
    
    # === Multiply / Divide (FBxx) ===
    if (hw1 & 0xFFF0) == 0xFB00:
        rn = hw1 & 0xF; ra = (hw2 >> 12) & 0xF; rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        if ra == 0xF: return f"mul {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
        return f"mla {_reg(rd)}, {_reg(rn)}, {_reg(rm)}, {_reg(ra)}"
    if (hw1 & 0xFFF0) == 0xFB00 and (hw2 & 0x00F0) == 0x0010:
        rn = hw1 & 0xF; ra = (hw2 >> 12) & 0xF; rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        return f"mls {_reg(rd)}, {_reg(rn)}, {_reg(rm)}, {_reg(ra)}"
    if (hw1 & 0xFFF0) == 0xFB80:
        rn = hw1 & 0xF; rdlo = (hw2 >> 12) & 0xF; rdhi = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        return f"smull {_reg(rdlo)}, {_reg(rdhi)}, {_reg(rn)}, {_reg(rm)}"
    if (hw1 & 0xFFF0) == 0xFBA0:
        rn = hw1 & 0xF; rdlo = (hw2 >> 12) & 0xF; rdhi = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        return f"umull {_reg(rdlo)}, {_reg(rdhi)}, {_reg(rn)}, {_reg(rm)}"
    if (hw1 & 0xFFF0) == 0xFB90:
        rn = hw1 & 0xF; ra = (hw2 >> 12) & 0xF; rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        return f"sdiv {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
    if (hw1 & 0xFFF0) == 0xFBB0:
        rn = hw1 & 0xF; ra = (hw2 >> 12) & 0xF; rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        return f"udiv {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
    
    # === Bit manipulation (FAxx) ===
    # CLZ
    if (hw1 & 0xFFF0) == 0xFAB0 and (hw2 & 0xF0F0) == 0xF080:
        rm = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        return f"clz {_reg(rd)}, {_reg(rm)}"
    # RBIT
    if (hw1 & 0xFFF0) == 0xFA90 and (hw2 & 0xF0F0) == 0xF0A0:
        rm = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        return f"rbit {_reg(rd)}, {_reg(rm)}"
    # REV.W
    if (hw1 & 0xFFF0) == 0xFA90 and (hw2 & 0xF0F0) == 0xF080:
        rm = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        return f"rev.w {_reg(rd)}, {_reg(rm)}"
    # REV16.W
    if (hw1 & 0xFFF0) == 0xFA90 and (hw2 & 0xF0F0) == 0xF090:
        rm = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        return f"rev16.w {_reg(rd)}, {_reg(rm)}"
    # UXTB.W / UXTH.W / SXTB.W / SXTH.W
    if (hw1 & 0xFFF0) == 0xFA5F and (hw2 & 0xF080) == 0xF080:
        rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF; rot = ((hw2 >> 4) & 3) * 8
        rs = f", ror #{rot}" if rot else ""
        return f"uxtb.w {_reg(rd)}, {_reg(rm)}{rs}"
    if (hw1 & 0xFFF0) == 0xFA1F and (hw2 & 0xF080) == 0xF080:
        rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF; rot = ((hw2 >> 4) & 3) * 8
        rs = f", ror #{rot}" if rot else ""
        return f"uxth.w {_reg(rd)}, {_reg(rm)}{rs}"
    if (hw1 & 0xFFF0) == 0xFA4F and (hw2 & 0xF080) == 0xF080:
        rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF; rot = ((hw2 >> 4) & 3) * 8
        rs = f", ror #{rot}" if rot else ""
        return f"sxtb.w {_reg(rd)}, {_reg(rm)}{rs}"
    if (hw1 & 0xFFF0) == 0xFA0F and (hw2 & 0xF080) == 0xF080:
        rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF; rot = ((hw2 >> 4) & 3) * 8
        rs = f", ror #{rot}" if rot else ""
        return f"sxth.w {_reg(rd)}, {_reg(rm)}{rs}"
    
    # FAxx register shift ops: LSL.W, LSR.W, ASR.W, ROR.W
    if (hw1 & 0xFFE0) == 0xFA00 and (hw2 & 0xF0F0) == 0xF000:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF; rm = hw2 & 0xF
        op = (hw1 >> 1) & 3
        ops = ["lsl", "lsr", "asr", "ror"]
        return f"{ops[op]}.w {_reg(rd)}, {_reg(rn)}, {_reg(rm)}"
    
    # === Bitfield (F3xx) ===
    # BFC / BFI
    if (hw1 & 0xFBF0) == 0xF360:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        imm3 = (hw2 >> 12) & 7; imm2 = (hw2 >> 6) & 3
        lsb = (imm3 << 2) | imm2; msb = hw2 & 0x1F; width = msb - lsb + 1
        if rn == 15: return f"bfc {_reg(rd)}, #{lsb}, #{width}"
        return f"bfi {_reg(rd)}, {_reg(rn)}, #{lsb}, #{width}"
    # UBFX / SBFX
    if (hw1 & 0xFBF0) == 0xF3C0:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        imm3 = (hw2 >> 12) & 7; imm2 = (hw2 >> 6) & 3
        lsb = (imm3 << 2) | imm2; width = (hw2 & 0x1F) + 1
        return f"ubfx {_reg(rd)}, {_reg(rn)}, #{lsb}, #{width}"
    if (hw1 & 0xFBF0) == 0xF340:
        rn = hw1 & 0xF; rd = (hw2 >> 8) & 0xF
        imm3 = (hw2 >> 12) & 7; imm2 = (hw2 >> 6) & 3
        lsb = (imm3 << 2) | imm2; width = (hw2 & 0x1F) + 1
        return f"sbfx {_reg(rd)}, {_reg(rn)}, #{lsb}, #{width}"
    
    # === MRS / MSR ===
    if hw1 == 0xF3EF and (hw2 & 0xF000) == 0x8000:
        rd = (hw2 >> 8) & 0xF; sysm = hw2 & 0xFF
        sysregs = {0:"APSR", 1:"IAPSR", 2:"EAPSR", 3:"XPSR", 5:"IPSR", 6:"EPSR",
                   7:"IEPSR", 8:"MSP", 9:"PSP", 16:"PRIMASK", 17:"BASEPRI",
                   18:"BASEPRI_MAX", 19:"FAULTMASK", 20:"CONTROL"}
        sr = sysregs.get(sysm, f"sysreg_{sysm}")
        return f"mrs {_reg(rd)}, {sr}"
    if (hw1 & 0xFFF0) == 0xF380 and (hw2 & 0xFF00) == 0x8800:
        rn = hw1 & 0xF; sysm = hw2 & 0xFF
        sysregs = {0:"APSR_nzcvq", 1:"IAPSR", 2:"EAPSR", 3:"XPSR", 5:"IPSR",
                   8:"MSP", 9:"PSP", 16:"PRIMASK", 17:"BASEPRI",
                   18:"BASEPRI_MAX", 19:"FAULTMASK", 20:"CONTROL"}
        sr = sysregs.get(sysm, f"sysreg_{sysm}")
        return f"msr {sr}, {_reg(rn)}"
    
    # === Barriers ===
    if full == 0xF3BF8F4F: return "dsb sy"
    if full == 0xF3BF8F5F: return "dmb sy"
    if full == 0xF3BF8F6F: return "isb sy"
    if (hw1 == 0xF3BF) and (hw2 & 0xFFF0) == 0x8F40:
        return f"dsb #{hw2 & 0xF}"
    if (hw1 == 0xF3BF) and (hw2 & 0xFFF0) == 0x8F50:
        return f"dmb #{hw2 & 0xF}"
    
    # === VFP (Cortex-M4F) ===
    # VLDR Sd, [Rn, #imm]
    if (hw1 & 0xFF00) == 0xED00 and (hw2 & 0x0E00) == 0x0A00:
        u = (hw1 >> 7) & 1; rn = hw1 & 0xF
        d = ((hw2 >> 12) & 0xF) | (((hw2 >> 22) & 1) << 4) if (hw1 & 0x0100) == 0 else (((hw2 >> 12) & 0xF) << 1) | ((hw2 >> 22) & 1)
        # Simplified: single-precision
        vd = (hw2 >> 12) & 0xF
        dd = (hw2 >> 22) & 1
        sd = (vd << 1) | dd
        imm8 = hw2 & 0xFF
        off = imm8 * 4
        if not u: off = -off
        l = (hw1 >> 4) & 1  # L bit
        if l:
            return f"vldr s{sd}, [{_reg(rn)}, #{off}]"
        else:
            return f"vstr s{sd}, [{_reg(rn)}, #{off}]"
    
    # VLDR/VSTR double
    if (hw1 & 0xFF00) == 0xED00 and (hw2 & 0x0E00) == 0x0B00:
        u = (hw1 >> 7) & 1; rn = hw1 & 0xF
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        d = (dd << 4) | vd
        imm8 = hw2 & 0xFF
        off = imm8 * 4
        if not u: off = -off
        l = (hw1 >> 4) & 1
        if l:
            return f"vldr d{d}, [{_reg(rn)}, #{off}]"
        else:
            return f"vstr d{d}, [{_reg(rn)}, #{off}]"
    
    # VPUSH / VPOP (single)
    if (hw1 & 0xFFBF) == 0xED2D and (hw2 & 0x0E00) == 0x0A00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        sd = (vd << 1) | dd; count = hw2 & 0xFF
        regs = ", ".join(f"s{sd+i}" for i in range(count))
        return f"vpush {{{regs}}}"
    if (hw1 & 0xFFBF) == 0xECBD and (hw2 & 0x0E00) == 0x0A00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        sd = (vd << 1) | dd; count = hw2 & 0xFF
        regs = ", ".join(f"s{sd+i}" for i in range(count))
        return f"vpop {{{regs}}}"
    
    # VPUSH / VPOP (double)
    if (hw1 & 0xFFBF) == 0xED2D and (hw2 & 0x0E00) == 0x0B00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        d = (dd << 4) | vd; count = (hw2 & 0xFF) // 2
        regs = ", ".join(f"d{d+i}" for i in range(count))
        return f"vpush {{{regs}}}"
    if (hw1 & 0xFFBF) == 0xECBD and (hw2 & 0x0E00) == 0x0B00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        d = (dd << 4) | vd; count = (hw2 & 0xFF) // 2
        regs = ", ".join(f"d{d+i}" for i in range(count))
        return f"vpop {{{regs}}}"
    
    # VMOV (various)
    # VMOV Rd, Sn (ARM core ← VFP)
    if (hw1 & 0xFFE0) == 0xEE10 and (hw2 & 0x0F90) == 0x0A10:
        rt = (hw2 >> 12) & 0xF; vn = (hw1 & 0xF); n = (hw2 >> 7) & 1
        sn = (vn << 1) | n
        return f"vmov {_reg(rt)}, s{sn}"
    # VMOV Sn, Rd (VFP ← ARM core)
    if (hw1 & 0xFFE0) == 0xEE00 and (hw2 & 0x0F90) == 0x0A10:
        rt = (hw2 >> 12) & 0xF; vn = (hw1 & 0xF); n = (hw2 >> 7) & 1
        sn = (vn << 1) | n
        return f"vmov s{sn}, {_reg(rt)}"
    
    # VMOV.F32 Sd, Sm
    if (hw1 & 0xFFBF) == 0xEEB0 and (hw2 & 0x0FD0) == 0x0A40:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        return f"vmov.f32 s{sd}, s{sm}"
    
    # VADD.F32 / VSUB.F32 / VMUL.F32 / VDIV.F32 / VNMUL.F32
    if (hw1 & 0xFF00) == 0xEE00 and (hw2 & 0x0E10) == 0x0A00:
        op1 = (hw1 >> 4) & 0xF
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vn = hw1 & 0xF; n = (hw2 >> 7) & 1; sn = (vn << 1) | n
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        sz = (hw2 >> 8) & 1  # 0=single, 1=double
        
        opc1 = (hw1 >> 4) & 0xF
        opc2 = (hw2 >> 6) & 3
        
        # Decode VFP operation
        d_bit = (hw1 >> 6) & 1
        opc1_top = (hw1 >> 7) & 1
        if opc1_top == 0:
            op_code = ((hw1 >> 5) & 1) * 2 + ((hw2 >> 6) & 1)
            ops = {0: "vmla.f32", 1: "vmls.f32", 2: "vnmls.f32", 3: "vnmla.f32"}
            if op_code in ops:
                return f"{ops[op_code]} s{sd}, s{sn}, s{sm}"
        
        # VMUL / VNMUL / VADD / VSUB
        p = (hw1 >> 7) & 1  # bit 23
        q = (hw1 >> 5) & 1  # bit 21
        if not p:
            if q == 0:
                return f"vmla.f32 s{sd}, s{sn}, s{sm}"
            else:
                return f"vmls.f32 s{sd}, s{sn}, s{sm}"
    
    # More VFP: VADD/VSUB/VMUL/VDIV (simplified)
    if (hw1 & 0xFFA0) == 0xEE20 and (hw2 & 0x0E10) == 0x0A00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vn = hw1 & 0xF; n = (hw2 >> 7) & 1; sn = (vn << 1) | n
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        sub = (hw1 >> 6) & 1
        if sub: return f"vsub.f32 s{sd}, s{sn}, s{sm}"
        return f"vadd.f32 s{sd}, s{sn}, s{sm}"
    
    if (hw1 & 0xFFA0) == 0xEE00 and (hw2 & 0x0E10) == 0x0A00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vn = hw1 & 0xF; n = (hw2 >> 7) & 1; sn = (vn << 1) | n
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        mul = (hw1 >> 6) & 1
        if mul: return f"vnmul.f32 s{sd}, s{sn}, s{sm}"
        return f"vmul.f32 s{sd}, s{sn}, s{sm}"
    
    if (hw1 & 0xFFE0) == 0xEE80 and (hw2 & 0x0E10) == 0x0A00:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vn = hw1 & 0xF; n = (hw2 >> 7) & 1; sn = (vn << 1) | n
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        return f"vdiv.f32 s{sd}, s{sn}, s{sm}"
    
    # VCMP
    if (hw1 & 0xFFBF) == 0xEEB4 and (hw2 & 0x0ED0) == 0x0A40:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        e = (hw2 >> 7) & 1
        return f"vcmp{'e' if e else ''}.f32 s{sd}, s{sm}"
    # VCMP with 0
    if (hw1 & 0xFFBF) == 0xEEB5 and (hw2 & 0x0EFF) == 0x0A40:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        return f"vcmp.f32 s{sd}, #0.0"
    
    # VMRS (FPSCR -> APSR_nzcv)
    if full == 0xEEF1FA10:
        return "vmrs APSR_nzcv, FPSCR"
    if (hw1 & 0xFFFF) == 0xEEF1 and (hw2 & 0x0FF0) == 0x0A10:
        rt = (hw2 >> 12) & 0xF
        return f"vmrs {_reg(rt)}, FPSCR"
    
    # VCVT (various)
    if (hw1 & 0xFFBF) == 0xEEB8 and (hw2 & 0x0ED0) == 0x0AC0:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        signed = (hw1 >> 7) & 1
        op = (hw2 >> 7) & 1
        if signed: return f"vcvt.f32.s32 s{sd}, s{sm}"
        return f"vcvt.f32.u32 s{sd}, s{sm}"
    if (hw1 & 0xFFBE) == 0xEEBC and (hw2 & 0x0ED0) == 0x0AC0:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        signed = (hw1 >> 0) & 1
        return f"vcvt.{'s' if signed else 'u'}32.f32 s{sd}, s{sm}"
    
    # VCVT double<>single
    if (hw1 & 0xFFBF) == 0xEEB7 and (hw2 & 0x0ED0) == 0x0AC0:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1
        return f"vcvt.f32.f64 s{(vd<<1)|dd}, d{(m<<4)|vm}"
    if (hw1 & 0xFFBF) == 0xEEB7 and (hw2 & 0x0ED0) == 0x0A40:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1
        return f"vcvt.f64.f32 d{(dd<<4)|vd}, s{(vm<<1)|m}"
    
    # VNEG.F32 / VABS.F32 / VSQRT.F32
    if (hw1 & 0xFFBF) == 0xEEB1 and (hw2 & 0x0FD0) == 0x0A40:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        return f"vneg.f32 s{sd}, s{sm}"
    if (hw1 & 0xFFBF) == 0xEEB0 and (hw2 & 0x0FD0) == 0x0AC0:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        return f"vabs.f32 s{sd}, s{sm}"
    if (hw1 & 0xFFBF) == 0xEEB1 and (hw2 & 0x0FD0) == 0x0AC0:
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1; sd = (vd << 1) | dd
        vm = hw2 & 0xF; m = (hw2 >> 5) & 1; sm = (vm << 1) | m
        return f"vsqrt.f32 s{sd}, s{sm}"
    
    # VLDM / VSTM (EC/ED range with multiple regs)
    if (hw1 & 0xFE00) == 0xEC00 and (hw2 & 0x0E00) == 0x0A00:
        p = (hw1 >> 8) & 1; u = (hw1 >> 7) & 1; w = (hw1 >> 5) & 1; l = (hw1 >> 4) & 1
        rn = hw1 & 0xF
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        sd = (vd << 1) | dd; count = hw2 & 0xFF
        regs = ", ".join(f"s{sd+i}" for i in range(min(count, 32)))
        wb = "!" if w else ""
        if l:
            return f"vldmia {_reg(rn)}{wb}, {{{regs}}}"
        return f"vstmia {_reg(rn)}{wb}, {{{regs}}}"
    if (hw1 & 0xFE00) == 0xEC00 and (hw2 & 0x0E00) == 0x0B00:
        p = (hw1 >> 8) & 1; u = (hw1 >> 7) & 1; w = (hw1 >> 5) & 1; l = (hw1 >> 4) & 1
        rn = hw1 & 0xF
        vd = (hw2 >> 12) & 0xF; dd = (hw2 >> 22) & 1
        d = (dd << 4) | vd; count = (hw2 & 0xFF) // 2
        regs = ", ".join(f"d{d+i}" for i in range(min(count, 16)))
        wb = "!" if w else ""
        if l:
            return f"vldmia {_reg(rn)}{wb}, {{{regs}}}"
        return f"vstmdb {_reg(rn)}{wb}, {{{regs}}}"
    
    # === CPSID/CPSIE ===
    if hw1 == 0xF3AF and (hw2 & 0xFFE0) == 0x8600:
        return f"cpsie {'i' if hw2 & 2 else ''}{'f' if hw2 & 1 else ''}"
    if hw1 == 0xF3AF and (hw2 & 0xFFE0) == 0x8700:
        return f"cpsid {'i' if hw2 & 2 else ''}{'f' if hw2 & 1 else ''}"
    
    # === NOP.W ===
    if full == 0xF3AF8000: return "nop.w"
    
    # 0xFFFF = erased flash / data padding
    if hw1 == 0xFFFF:
        return f"<data:0x{hw1:04x}{hw2:04x}>"
    
    return None


def disassemble(bin_data, base, start_off, count=20, named_addrs=None):
    """Disassemble 'count' instructions starting at start_off.
    Returns list of (addr, size, text) tuples."""
    results = []
    pos = start_off
    for _ in range(count):
        if pos + 2 > len(bin_data):
            break
        addr = base + pos
        hw = struct.unpack_from('<H', bin_data, pos)[0]
        is32 = (hw >> 11) >= 0x1D
        
        if is32:
            if pos + 4 > len(bin_data):
                break
            hw2 = struct.unpack_from('<H', bin_data, pos + 2)[0]
            result = decode_32(hw1=hw, hw2=hw2, addr=addr)
            
            # Handle tuple returns (instruction + literal target)
            lit_target = None
            if isinstance(result, tuple):
                result, lit_target = result
            
            if result is None:
                result = f"T32:0x{(hw << 16) | hw2:08X}"
            
            # Resolve BL targets
            if named_addrs and result.startswith("bl "):
                target_str = result[3:]
                try:
                    target = int(target_str, 16)
                    if target in named_addrs:
                        result = f"bl {named_addrs[target]}"
                except ValueError:
                    pass
            
            # Resolve literal pool loads
            if lit_target and named_addrs:
                if 0 <= lit_target - base < len(bin_data):
                    val = struct.unpack_from('<I', bin_data, lit_target - base)[0]
                    if val in named_addrs:
                        result += f"  ; = {named_addrs[val]}"
                    elif 0x20000000 <= val <= 0x20020000:
                        result += f"  ; = 0x{val:08X} (RAM)"
                    elif 0x08000000 <= val <= 0x08100000:
                        result += f"  ; = 0x{val:08X} (flash)"
            
            results.append((addr, 4, result))
            pos += 4
        else:
            result = decode_16(hw, addr)
            
            lit_target = None
            if isinstance(result, tuple):
                result, lit_target = result
            
            if result is None:
                result = f"T16:0x{hw:04X}"
            
            # Resolve literal pool
            if lit_target and named_addrs:
                if 0 <= lit_target - base < len(bin_data):
                    val = struct.unpack_from('<I', bin_data, lit_target - base)[0]
                    if val in named_addrs:
                        result += f"  ; = {named_addrs[val]}"
                    elif 0x20000000 <= val <= 0x20020000:
                        result += f"  ; = 0x{val:08X} (RAM)"
                    elif 0x08000000 <= val <= 0x08100000:
                        result += f"  ; = 0x{val:08X} (flash)"
            
            results.append((addr, 2, result))
            pos += 2
    
    return results


import struct
