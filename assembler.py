from typing import Dict, List, Union
import re, argparse, os

parser = argparse.ArgumentParser()
parser.add_argument("input", help="input filename for the assembly program to assemble")
parser.add_argument("-o", "--output", default="out.txt", help="optional output filename")
args = parser.parse_args()

assert os.path.exists(args.input), f"assembler.py: error: {args.input}: No such file or directory"

func = {"add": 0, "and": 1, "or": 2, "sll": 3, "ld": 0, "lb": 1, "sd": 2, "sb": 3}
opc = {"R": 0, "B": 1, "I": 2, "L": 3}

re_comment = r"//.*"

re_register = r"(?:x(?:[0-9]|1[0-1])|ip|op|pc|sp)"
re_imm = r"(?P<immediate>-?\d+)"

re_label = r"[a-zA-Z_][a-zA-Z0-9_]*"
re_label_ref = fr"(?P<label_ref_name>{re_label})"
re_label_dec = fr"(?P<label_dec_name>{re_label}):"

re_delim = r"(\s*,\s*|\s+)"
re_R_instr = fr"(?P<R_instr>add|and|or|sll){re_delim}(?P<Rr1>{re_register}){re_delim}(?P<Rr2>{re_register}){re_delim}(?P<Rr3>{re_register})"
re_B_instr = fr"(?P<B_instr>beq){re_delim}(?P<Br1>{re_register}){re_delim}(?P<Br2>{re_register}){re_delim}{re_label_ref}"
re_I_instr = fr"(?P<I_instr>ld|lb|sd|sb){re_delim}(?P<Ir1>{re_register}){re_delim}(?P<Ir2>{re_register})"
re_L_instr = fr"(?P<L_instr>li){re_delim}(?P<Lr1>{re_register}){re_delim}{re_imm}"
re_instr = fr"(?P<instr>{re_R_instr}|{re_B_instr}|{re_I_instr}|{re_L_instr})"

re_valid_line = re.compile(fr"^\s*({re_label_dec})?\s*({re_instr})?\s*({re_comment})?$")

def validate_lines(assembly_lines: List[str]) -> List[re.Match]:
    line_matches = []
    for i, line in enumerate(assembly_lines):
        m = re.match(re_valid_line, line)
        if not m:
            raise SyntaxError(f'File "{args.input}", line {i+1}:\n{line.strip()}\ninvalid syntax')
        line_matches.append(m)
    return line_matches

def get_instructions(line_matches: List[re.Match]) -> List[Dict[str, Union[int, re.Match]]]:
    instructions = []
    for i, m in enumerate(line_matches):
        m = line_matches[i]
        if m.group("instr"):
            instructions.append({
                "lineno": i + 1,
                "match": m
            })
    return instructions

def get_next_instr_no_from(instructions: List[Dict[str, Union[int, re.Match]]], label_lineno: int) -> int:
    for instrno, instr in enumerate(instructions):
        if instr.get("lineno") >= label_lineno:
            return instrno
    return len(instructions)

def get_label_linenos(line_matches: List[re.Match], instructions: List[Dict[str, Union[int, re.Match]]]) -> Dict[str, int]:
    label_instrno = {}
    for i, m in enumerate(line_matches):
        if m.group("label_dec_name"):
            label_instrno[m.group("label_dec_name")] = get_next_instr_no_from(instructions, i + 1)
    return label_instrno

def regnum(regstr: str) -> int:
    regstr = regstr.lower()
    m = re.match(r"x(\d+)$", regstr)
    if m:
        return int(m.group(1))
    return { "ip": 12, "op": 13, "pc": 14, "sp": 15 }[regstr]

def get_offset(instrno: int, label_instrno: int) -> int:
    return 2*(label_instrno - instrno)

def translate_instruction(instrno: int, instr: Dict[str, Union[int, re.Match]], label_instrno: Dict[str, int]) -> str:
    m = instr["match"]

    # | 4 (d1) | 4 (d2) | 4 (d3) | 2 (d4) | 2 (d5) |

    if m.group("R_instr"):
        d1 = regnum(m.group("Rr1"))
        d2 = regnum(m.group("Rr2"))
        d3 = regnum(m.group("Rr3"))
        d4 = func[m.group("R_instr")]
        d5 = opc["R"]
    elif m.group("B_instr"):
        d1 = regnum(m.group("Br1"))
        d2 = regnum(m.group("Br2"))
        label = m.group("label_ref_name")
        imm = get_offset(instrno, label_instrno[label])
        if (imm > 31*2): print(f"Warning: Instruction at line {instr['lineno']}:\n  {m.group('instr')}\nmakes a jump to label '{label}' of {imm >> 1} instructions. The maximum is 31.")
        if (imm < -32*2): print(f"Warning: Instruction at line {instr['lineno']}:\n  {m.group('instr')}\nmakes a jump to label '{label}' of {imm >> 1} instructions. The minimum is -32.")
        d3 = (imm >> 3) & 0xF
        d4 = (imm >> 1) & 0x3
        d5 = opc["B"]
    elif m.group("I_instr"):
        d1 = regnum(m.group("Ir1"))
        d2 = regnum(m.group("Ir2"))
        d3 = 0
        d4 = func[m.group("I_instr")]
        d5 = opc["I"]
    elif m.group("L_instr"):
        d1 = regnum(m.group("Lr1"))
        imm = int(m.group("immediate"))
        d2 = (imm >> 4) & 0xF
        d3 = imm & 0xF
        d4 = 0
        d5 = opc["L"]
    else:
        raise LookupError("No valid instruction found in " + m.group(0))
    
    return f"0x%04X" % ((d1 << 12) | (d2 << 8) | (d3 << 4) | (d4 << 2) | d5)

def assemble_instructions(instructions: List[Dict[str, Union[int, re.Match]]], label_instrno: Dict[str, int]) -> List[str]:
    out = []
    for instrno, instr in enumerate(instructions):
        out.append(translate_instruction(instrno, instr, label_instrno))
    return out

if __name__ == "__main__":

    # Read assembly code
    with open(args.input, "r") as file:
        assembly_lines = file.readlines()

    line_matches = validate_lines(assembly_lines)
    instructions = get_instructions(line_matches)
    label_instrno = get_label_linenos(line_matches, instructions)
    assembled_instr = assemble_instructions(instructions, label_instrno)

    # Write output
    with open(args.output, "w") as file:
        for hex_instr in assembled_instr:
            file.write(hex_instr + "\n")
        file.write("0xFFFF\n")
