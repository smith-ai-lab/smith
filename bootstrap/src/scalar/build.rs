//! Builds a function of the scalar form one instruction at a time.

use super::{
    Binary, Bits, Block, Compare, Constant, End, FloatBinary, FloatBits, FloatCompare, Function,
    Instruction, Jump, Op, System, Type, Value,
};

/// A function being built. Its first block is where it starts.
pub struct Builder {
    types: Vec<Type>,
    blocks: Vec<Block>,
    current: usize,
    cold: bool,
}

impl Default for Builder {
    fn default() -> Self {
        Self::new()
    }
}

impl Builder {
    pub fn new() -> Self {
        let mut b = Builder {
            types: Vec::new(),
            blocks: Vec::new(),
            current: 0,
            cold: false,
        };
        b.block(&[]);
        b
    }

    /// New values of the types, which are the parameters of the first block.
    pub fn params(&mut self, types: &[Type]) -> Vec<Value> {
        let values: Vec<Value> = types.iter().map(|t| self.fresh(*t)).collect();
        self.blocks[0].params.extend(values.iter().copied());
        values
    }

    /// The type of a value made so far.
    pub fn type_of(&self, v: Value) -> Type {
        self.types[v.0]
    }

    pub fn fresh(&mut self, ty: Type) -> Value {
        self.types.push(ty);
        Value(self.types.len() - 1)
    }

    /// A new block that takes values of the types, and gives them.
    pub fn block(&mut self, params: &[Type]) -> (usize, Vec<Value>) {
        let values: Vec<Value> = params.iter().map(|t| self.fresh(*t)).collect();
        self.blocks.push(Block {
            params: values.clone(),
            instructions: Vec::new(),
            end: End::Return(Vec::new()),
        });
        (self.blocks.len() - 1, values)
    }

    /// Makes the block the one being filled.
    pub fn at(&mut self, block: usize) {
        self.current = block;
    }

    /// Adds an instruction to the block being filled, and gives its results.
    pub fn emit(&mut self, op: Op, results: &[Type]) -> Vec<Value> {
        let results: Vec<Value> = results.iter().map(|t| self.fresh(*t)).collect();
        self.blocks[self.current].instructions.push(Instruction {
            results: results.clone(),
            op,
        });
        results
    }

    /// An integer of the size that holds the bits `n`.
    pub fn int(&mut self, bits: Bits, n: u64) -> Value {
        self.emit(Op::Constant(Constant::Int(n)), &[Type::Int(bits)])[0]
    }

    /// A float of the size that holds the bits `n`.
    pub fn float(&mut self, bits: FloatBits, n: u64) -> Value {
        self.emit(Op::Constant(Constant::Float(n)), &[Type::Float(bits)])[0]
    }

    pub fn float_binary(&mut self, op: FloatBinary, a: Value, b: Value) -> Value {
        let ty = self.types[a.0];
        self.emit(Op::FloatBinary { op, a, b }, &[ty])[0]
    }

    pub fn float_negate(&mut self, value: Value) -> Value {
        let ty = self.types[value.0];
        self.emit(Op::FloatNegate(value), &[ty])[0]
    }

    pub fn float_compare(&mut self, op: FloatCompare, a: Value, b: Value) -> Value {
        self.emit(Op::FloatCompare { op, a, b }, &[Type::Int(Bits::B8)])[0]
    }

    pub fn float_from_int(&mut self, signed: bool, value: Value, to: FloatBits) -> Value {
        self.emit(Op::FloatFromInt { signed, value }, &[Type::Float(to)])[0]
    }

    pub fn int_from_float(&mut self, signed: bool, value: Value, to: Bits) -> Value {
        self.emit(Op::IntFromFloat { signed, value }, &[Type::Int(to)])[0]
    }

    pub fn float_resize(&mut self, value: Value, to: FloatBits) -> Value {
        self.emit(Op::FloatResize(value), &[Type::Float(to)])[0]
    }

    pub fn null(&mut self) -> Value {
        self.emit(Op::Constant(Constant::Null), &[Type::Address])[0]
    }

    pub fn binary(&mut self, op: Binary, a: Value, b: Value) -> Value {
        let ty = self.types[a.0];
        self.emit(Op::Binary { op, a, b }, &[ty])[0]
    }

    /// The number `op` gives of `a` and `b` with the bits that do not fit left
    /// off, and whether it did not fit.
    pub fn overflows(&mut self, op: Binary, signed: bool, a: Value, b: Value) -> (Value, Value) {
        let ty = self.types[a.0];
        let [result, overflowed] = self.emit(
            Op::Overflows { op, signed, a, b },
            &[ty, Type::Int(Bits::B8)],
        )[..] else {
            unreachable!()
        };
        (result, overflowed)
    }

    pub fn compare(&mut self, op: Compare, a: Value, b: Value) -> Value {
        self.emit(Op::Compare { op, a, b }, &[Type::Int(Bits::B8)])[0]
    }

    pub fn widen(&mut self, signed: bool, value: Value, to: Bits) -> Value {
        self.emit(Op::Widen { signed, value }, &[Type::Int(to)])[0]
    }

    pub fn narrow(&mut self, value: Value, to: Bits) -> Value {
        self.emit(Op::Narrow(value), &[Type::Int(to)])[0]
    }

    pub fn choose(&mut self, on: Value, then: Value, otherwise: Value) -> Value {
        let ty = self.types[then.0];
        self.emit(
            Op::Choose {
                on,
                then,
                otherwise,
            },
            &[ty],
        )[0]
    }

    pub fn load(&mut self, ty: Type, address: Value) -> Value {
        self.emit(Op::Load { ty, address }, &[ty])[0]
    }

    pub fn store(&mut self, address: Value, value: Value) {
        let ty = self.types[value.0];
        self.emit(Op::Store { ty, address, value }, &[]);
    }

    pub fn call(&mut self, function: usize, args: Vec<Value>, results: &[Type]) -> Vec<Value> {
        self.emit(Op::Call { function, args }, results)
    }

    /// Asks the system for as many bytes as the 64-bit integer says.
    pub fn allocate(&mut self, count: Value) -> Value {
        let call = System::Allocate;
        self.emit(
            Op::System {
                call,
                args: vec![count],
            },
            &[Type::Address],
        )[0]
    }

    /// Writes the bytes from the address, as many as the 64-bit integer
    /// says, to standard error.
    pub fn write_error(&mut self, address: Value, count: Value) {
        let call = System::Write(super::Stream::Stderr);
        self.emit(
            Op::System {
                call,
                args: vec![address, count],
            },
            &[],
        );
    }

    /// Ends the program with the status, a byte.
    pub fn exit(&mut self, status: Value) {
        let call = System::Exit;
        self.emit(
            Op::System {
                call,
                args: vec![status],
            },
            &[],
        );
    }

    /// Ends the block being filled by a jump.
    pub fn jump(&mut self, block: usize, args: Vec<Value>) {
        self.blocks[self.current].end = End::Jump(Jump { block, args });
    }

    /// Ends the block being filled by a choice between jumps.
    pub fn switch(&mut self, on: Value, jumps: Vec<(usize, Vec<Value>)>) {
        let jumps = jumps
            .into_iter()
            .map(|(block, args)| Jump { block, args })
            .collect();
        self.blocks[self.current].end = End::Switch { on, jumps };
    }

    /// Ends the block being filled by returning the values.
    pub fn ret(&mut self, values: Vec<Value>) {
        self.blocks[self.current].end = End::Return(values);
    }

    /// Says the function being built is run rarely, as `Function::cold` says.
    pub fn cold(&mut self) {
        self.cold = true;
    }

    pub fn finish(self, results: &[Type]) -> Function {
        Function {
            types: self.types,
            results: results.to_vec(),
            blocks: self.blocks,
            cold: self.cold,
        }
    }
}
