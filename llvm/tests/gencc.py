import sys, re


class Label(object):
    def __init__(self, name):
        self.instruction_lines = []
        self.name = name

    @staticmethod
    def IsLabel(line):
        return ':' in line

    @staticmethod
    def ParseLabel(line):
        name = line[0:line.index(':')]
        name = Label.Filter(name)
        return Label(name)

    @staticmethod
    def Filter(name):
        return name.translate(None, '@ %.')

class Location(object):
    def __init__(self, kind, location_size, reg_no, offset_or_constant):
        self.kind = kind
        self.location_size = location_size
        self.reg_no = reg_no
        self.offset_or_constant = offset_or_constant

    def __repr__(self):
        return 'Location kind: %s, size: %d, regno: %d, offset_or_constant: %d' % (Location.KindToString(self.kind), self.location_size, self.reg_no, self.offset_or_constant)

    @staticmethod
    def KindToString(kind):
        if kind == 1:
            return 'Register'
        elif kind == 2:
            return 'Direct'
        elif kind == 3:
            return 'Indirect'
        elif kind == 4:
            return 'Constant'
        elif kind == 5:
            return 'ConstantIndex'
        else:
            raise Exception('unknown kind')

class Patchpoint(object):
    def __init__(self, id, label_name, locations):
        self.id = id
        self.label_name = label_name 
        self.locations = locations
        pass


    @staticmethod
    def Parse(lines, start_index):
        index = start_index
        patch_id = Patchpoint.GetLong(lines, index)
        index += 2 # ignore the higher bits
        label_name = Patchpoint.GetLabelName(lines, index)
        index += 1
        index += 1 # Reserve
        NumLocations = Patchpoint.GetShort(lines, index)
        index += 1
        locations = []
        for i in range(NumLocations):
            kind = Patchpoint.GetByte(lines, index)
            index += 2 # Reserve one byte
            location_size = Patchpoint.GetShort(lines, index)
            index += 1
            reg_no = Patchpoint.GetShort(lines, index)
            index += 2 # Reserve one short
            offset_or_constant = Patchpoint.GetLong(lines, index)
            index += 1
            location = Location(kind, location_size, reg_no, offset_or_constant)
            locations.append(location)
        return (Patchpoint(patch_id, label_name, locations), index)

    @staticmethod
    def AssertElement(a, b, line, index):
        if a != b:
            raise Exception('expecting %s for line %s, at %d' % (b, line, index + 1))

    @staticmethod
    def GetByte(lines, index):
        line = lines[index]
        TwoElement = line.split()
        if len(TwoElement) != 2:
            raise Exception('expecting two element for line %s, at %d' % (line, index + 1))
        Patchpoint.AssertElement(TwoElement[0], '.byte', line, index)
        return int(TwoElement[1])

    @staticmethod
    def GetShort(lines, index):
        line = lines[index]
        TwoElement = line.split()
        if len(TwoElement) != 2:
            raise Exception('expecting two element for line %s, at %d' % (line, index + 1))
        Patchpoint.AssertElement(TwoElement[0], '.short', line, index)
        return int(TwoElement[1])
            
    @staticmethod
    def GetLong(lines, index):
        line = lines[index]
        TwoElement = line.split()
        if len(TwoElement) != 2:
            raise Exception('expecting two element for line %s, at %d' % (line, index + 1))
        Patchpoint.AssertElement(TwoElement[0], '.long', line, index)
        return int(TwoElement[1])

    @staticmethod
    def GetLabelName(lines, index):
        line = lines[index]
        TwoElement = line.split()
        if len(TwoElement) != 2:
            raise Exception('expecting two element for line %s, at %d' % (line, index + 1))
        Patchpoint.AssertElement(TwoElement[0], '.long', line, index)
        TwoElement = TwoElement[1].split('-')
        if len(TwoElement) != 2:
            raise Exception('expecting two element for line %s, at %d' % (line, index + 1))
        return Label.Filter(TwoElement[0])

    def __repr__(self):
        return 'PatchPoint ID: %d, label_name: %s, locations: %s' % (self.id, self.label_name, str(self.locations))

class Processor(object):
    def __init__(self):
        self.labels_ = []
        self.results = []
        self.patchpoint_map_ = {}
        self.code_lines_ = []
        self.label_decls_ = []
        pass

    def ProcessLines(self, lines):
        self.SplitByLabels(lines)
        self.ParseStackMapsLocations()
        self.ProcessFunctionLabels()

    def SplitByLabels(self, lines):
        current_label = None
        for line in lines:
            if Label.IsLabel(line):
                if current_label:
                    self.labels_.append(current_label)
                current_label = Label.ParseLabel(line)
            if current_label:
                current_label.instruction_lines.append(line)
        if current_label:
            self.labels_.append(current_label)

    def ParseStackMapsLocations(self):
        stack_map_label = self.FindLabel('__LLVM_StackMaps')
        current_line_no = 1 # ignore __LLVM_StackMaps
        while True:
            index = Processor.FindNextPatchPointIndex(stack_map_label.instruction_lines, current_line_no)
            if index == -1:
                break
            current_line_no = index
            (patch_point, current_line_no) = Patchpoint.Parse(stack_map_label.instruction_lines, current_line_no)
            self.patchpoint_map_[patch_point.label_name] = patch_point

    def ProcessFunctionLabels(self):
        inline_asm = False
        for label in self.labels_:
            self.AddLabel(label)
            if label.name in self.patchpoint_map_:
                self.HandlePatchpoint(self.patchpoint_map_[label.name])
                continue
            for instruction_line in label.instruction_lines[1:]:
                if not instruction_line.strip():
                    continue
                if instruction_line == '.fnend':
                    break
                if instruction_line.startswith('.'):
                    continue # ignore directive
                if instruction_line == '@APP':
                    inline_asm = True
                    continue
                if instruction_line == '@NO_APP':
                    inline_asm = False
                    continue
                if instruction_line.startswith('mov') and inline_asm:
                    self.AddMagicObjectHandling(instruction_line)
                    continue
                self.ProcessInstructionLine(instruction_line)

    def AddLabel(self, label):
        self.AddLabelDecl('Label %s' % label.name)
        self.AddCodeLine('__ bind(&%s);' % label.name)

    def AddLabelDecl(self, label_decl):
        self.label_decls_.append(label_decl)

    def AddCodeLine(self, line):
        self.code_lines_.append(line)

    def AddMagicObjectHandling(self, instruction_line):
        self.AddCodeLine('// needs to handle magic : %s' % instruction_line)
    
    def ProcessInstructionLine(self, instruction_line):
        until_src_operands = instruction_line.find(',')
        cond = 'al'
        if until_src_operands == -1:
            if instruction_line.startswith('b'):
                TwoElement = instruction_line.split()
                if len(TwoElement) != 2:
                    raise Exception('unable to handle instruction_line: %s' % instruction_line)
                (mnemonic, cond) = Processor.ExtractMnemonic(TwoElement[0])
                label_name = Label.Filter(TwoElement[1])
                self.AddCodeLine('__ %s(&%s, %s);' % (mnemonic, label_name, cond))
                return
            else:
                TwoElement = instruction_line.split()
                if len(TwoElement) != 2:
                    raise Exception('unable to handle instruction_line: %s' % instruction_line)
                (mnemonic, cond) = Processor.ExtractMnemonic(TwoElement[0])
                if mnemonic != 'push' and mnemonic != 'pop':
                    raise Exception('unable to handle instruction_line: %s %s %d' % (instruction_line, mnemonic))
                if mnemonic == 'push':
                    self.AddCodeLine('__ stm(db, sp, %s, %s);' % (Processor.GetOperandString(TwoElement[1]), cond))
                else:
                    self.AddCodeLine('__ ldm(ia, sp, %s, %s);' % (Processor.GetOperandString(TwoElement[1]), cond))
                return
        TwoElement = instruction_line[0:until_src_operands].split()
        if len(TwoElement) != 2:
            raise Exception('unable to handle instruction_line: %s' % instruction_line)
        (mnemonic, cond) = Processor.ExtractMnemonic(TwoElement[0])
        operand_string = Processor.GetOperandString(instruction_line[until_src_operands + 1:].strip())
        if cond != 'al':
            self.AddCodeLine('__ %s(%s, %s, %s);' % (mnemonic, TwoElement[1], operand_string, cond))
        else:
            self.AddCodeLine('__ %s(%s, %s);' % (mnemonic, TwoElement[1], operand_string))

#    def ProcessInstructionLine(self, instruction_line):
#        until_src_operands = instruction_line.find(' ')
#        mnemonic_and_cond = instruction_line[0:until_src_operands]
#        (mnemonic, cond) = Processor.ExtractMnemonic(mnemonic_and_cond)
#        operands_data = instruction_line[until_src_operands + 1:]
#        operands = []
#        while True:
#            operands_data, operand, addr_mode = Processor.ConsumeOneOperand(operands_data)
#            operands.append(operand)
#            if not operands_data:
#                break



    def FindLabel(self, label_name):
        for label in self.labels_:
            if label.name == label_name:
                return label
        return None

    def HandlePatchpoint(self, patch_point):
        offset_set = set()
        self.AddCodeLine('// FIXME: handle patch point %s here!' % patch_point.label_name)
        self.AddCodeLine('{')
        self.AddCodeLine('\tSafepoint safepoint = safepoint_builder->DefineSafepoint(asm, Safepoint::kSimple, 0, Safepoint::kLazyDeopt /* assume need frame */);')
        for location in patch_point.locations:
            if location.kind == 3:
                if location.reg_no != 13:
                    raise Exception('unknown how to handle %s' % str(location))
                if not location.offset_or_constant in offset_set:
                    self.AddCodeLine('\tsafepoint.DefinePointerSlot(%d, zone);' % (location.offset_or_constant / 4))
                    offset_set.add(location.offset_or_constant)
        self.AddCodeLine('}')

    def Print(self):
        decl_labels = 'Label ' + ', '.join(self.label_decls_) + ';\n'
        print(decl_labels + '\n'.join(self.code_lines_))

    @staticmethod
    def FindNextPatchPointIndex(instruction_lines, line_no):
        limit = len(instruction_lines)
        for i in range(line_no, limit):
            line = instruction_lines[i]
            TwoElement = line.split()
            if len(TwoElement) != 2:
                return -1
            if TwoElement[0] != '.long':
                continue
            if not TwoElement[1].startswith('.Ltmp'):
                continue
            return i - 2
        return -1
    cond_list = ('eq', 'ne', 'cs', 'cc', 'mi',
        'pl', 'vs', 'vc', 'hi', 'ls', 'ge', 'lt', 'gt', 'le')
    @staticmethod
    def ExtractMnemonic(mnemonic):
        mnemonic = mnemonic.strip()
        for cond in Processor.cond_list:
            if mnemonic.endswith(cond):
                return (mnemonic[0:len(mnemonic) - len(cond)], cond)
        return (mnemonic, 'al')

    @staticmethod
    def GetOperandString(operands):
        operands = operands.strip()
        if operands.startswith('['):
            operands = operands[1:operands.index(']')]
            elements = operands.split(',')
            if len(elements) == 1:
                return 'MemOperand(%s)' % elements[0]
            elif len(elements) == 2:
                second = elements[1].strip()
                if second.startswith('#'):
                    second = second[1:]
                return 'MemOperand(%s, %s)' % (elements[0], second)
            else:
                (shift_name, offset) = Processor.GetShiftOperation(elements[2])
                return 'MemOperand(%s, %s, %s, %s)' % (elements[0], elements[1], shift_name, offset)
        elif operands.startswith('#'):
            return 'Operand(%s)' % (operands[1:])
        elif operands.startswith('{'):
            operands = operands[1:-1]
            return '(' + '|'.join(['(1 << %s.code())' % reg for reg in operands.split(',')]) + ')'
        else:
            elements = operands.split(',')
            if len(elements) == 1:
                return 'Operand(%s)' % (elements[0])
            elif len(elements) == 2:
                second = elements[1].strip()
                if second.startswith('#'):
                    second = second[1:]
                if Processor.IsShiftOperand(second):
                    shift_name, shift_imm  = Processor.GetShiftOperation(second)
                    return 'Operand(%s, %s, %s)' % (elements[0], shift_name, shift_imm)
                return '%s, Operand(%s)' % (elements[0], second)
            else:
                shift_operand = elements[2].strip()
                if shift_operand.startswith('#'):
                    return '%s, %s, %s' %(elements[0], elements[1], elements[2])
                if not Processor.IsShiftOperand(shift_operand):
                    raise Exception('unexpected operand: %s, for %s' % (operands, shift_operand))
                shift_name, shift_imm = Processor.GetShiftOperation(shift_operand)
                return '%s, Operand(%s, %s, %s)' % (elements[0], elements[1], shift_name, shift_imm)

    @staticmethod
    def GetShiftOperation(shift):
        TwoElement = shift.split()
        shift_name = TwoElement[0].upper()
        return (shift_name, TwoElement[1][1:])

    shift_re = re.compile('^(?:lsl|lsr|asr|ror).*')

    @staticmethod
    def IsShiftOperand(maybe):
        if Processor.shift_re.match(maybe):
            return True
        return False

def Process(f):
    lines = f.readlines()
    lines = [line.strip() for line in lines]
    processor = Processor()
    processor.ProcessLines(lines)
    processor.Print()

def main():
    with open(sys.argv[1], 'r') as f:
        Process(f)    

if __name__ == '__main__':
    main()
