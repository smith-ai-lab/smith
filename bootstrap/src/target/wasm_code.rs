//! The instructions of WebAssembly, written as bytes. Only the ones the
//! target uses are here.

/// The type of a value in a module.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    I32,
    I64,
}

impl Kind {
    pub fn byte(self) -> u8 {
        match self {
            Kind::I32 => 0x7F,
            Kind::I64 => 0x7E,
        }
    }
}

pub const UNREACHABLE: u8 = 0x00;
pub const BLOCK: u8 = 0x02;
pub const LOOP: u8 = 0x03;
pub const IF: u8 = 0x04;
pub const ELSE: u8 = 0x05;
pub const END: u8 = 0x0B;
pub const BR: u8 = 0x0C;
pub const BR_IF: u8 = 0x0D;
pub const BR_TABLE: u8 = 0x0E;
pub const RETURN: u8 = 0x0F;
pub const CALL: u8 = 0x10;
pub const SELECT: u8 = 0x1B;
pub const LOCAL_GET: u8 = 0x20;
pub const LOCAL_SET: u8 = 0x21;
pub const GLOBAL_GET: u8 = 0x23;
pub const GLOBAL_SET: u8 = 0x24;
pub const I32_LOAD: u8 = 0x28;
pub const I64_LOAD: u8 = 0x29;
pub const I32_LOAD8_U: u8 = 0x2D;
pub const I32_LOAD16_U: u8 = 0x2F;
pub const I32_STORE: u8 = 0x36;
pub const I64_STORE: u8 = 0x37;
pub const I32_STORE8: u8 = 0x3A;
pub const I32_STORE16: u8 = 0x3B;
pub const MEMORY_SIZE: u8 = 0x3F;
pub const MEMORY_GROW: u8 = 0x40;
pub const I32_CONST: u8 = 0x41;
pub const I64_CONST: u8 = 0x42;
pub const I32_EQZ: u8 = 0x45;
pub const I32_EQ: u8 = 0x46;
pub const I32_LT_S: u8 = 0x48;
pub const I32_LT_U: u8 = 0x49;
pub const I64_EQZ: u8 = 0x50;
pub const I64_EQ: u8 = 0x51;
pub const I64_NE: u8 = 0x52;
pub const I64_LT_S: u8 = 0x53;
pub const I64_LT_U: u8 = 0x54;
pub const I64_GT_U: u8 = 0x56;
pub const I32_ADD: u8 = 0x6A;
pub const I32_SUB: u8 = 0x6B;
pub const I32_MUL: u8 = 0x6C;
pub const I32_DIV_S: u8 = 0x6D;
pub const I32_DIV_U: u8 = 0x6E;
pub const I32_REM_S: u8 = 0x6F;
pub const I32_REM_U: u8 = 0x70;
pub const I32_AND: u8 = 0x71;
pub const I32_OR: u8 = 0x72;
pub const I32_XOR: u8 = 0x73;
pub const I32_SHL: u8 = 0x74;
pub const I32_SHR_S: u8 = 0x75;
pub const I32_SHR_U: u8 = 0x76;
pub const I64_ADD: u8 = 0x7C;
pub const I64_SUB: u8 = 0x7D;
pub const I64_MUL: u8 = 0x7E;
pub const I64_DIV_S: u8 = 0x7F;
pub const I64_DIV_U: u8 = 0x80;
pub const I64_REM_S: u8 = 0x81;
pub const I64_REM_U: u8 = 0x82;
pub const I64_AND: u8 = 0x83;
pub const I64_OR: u8 = 0x84;
pub const I64_XOR: u8 = 0x85;
pub const I64_SHL: u8 = 0x86;
pub const I64_SHR_S: u8 = 0x87;
pub const I64_SHR_U: u8 = 0x88;
pub const I32_WRAP_I64: u8 = 0xA7;
pub const I64_EXTEND_I32_S: u8 = 0xAC;
pub const I64_EXTEND_I32_U: u8 = 0xAD;
pub const I32_EXTEND8_S: u8 = 0xC0;
pub const I32_EXTEND16_S: u8 = 0xC1;

/// The type of a block that gives nothing.
const NO_VALUE: u8 = 0x40;

/// The bytes of a unsigned number in LEB128.
pub fn unsigned(out: &mut Vec<u8>, mut n: u64) {
    loop {
        let byte = (n & 0x7F) as u8;
        n >>= 7;
        if n == 0 {
            out.push(byte);
            return;
        }
        out.push(byte | 0x80);
    }
}

/// The bytes of a signed number in LEB128.
pub fn signed(out: &mut Vec<u8>, mut n: i64) {
    loop {
        let byte = (n & 0x7F) as u8;
        n >>= 7;
        let done = (n == 0 && byte & 0x40 == 0) || (n == -1 && byte & 0x40 != 0);
        if done {
            out.push(byte);
            return;
        }
        out.push(byte | 0x80);
    }
}

/// The body of a function, written one instruction at a time.
#[derive(Default)]
pub struct Code {
    pub bytes: Vec<u8>,
}

impl Code {
    pub fn op(&mut self, op: u8) {
        self.bytes.push(op);
    }

    pub fn local_get(&mut self, local: u32) {
        self.op(LOCAL_GET);
        unsigned(&mut self.bytes, u64::from(local));
    }

    pub fn local_set(&mut self, local: u32) {
        self.op(LOCAL_SET);
        unsigned(&mut self.bytes, u64::from(local));
    }

    pub fn global_get(&mut self, global: u32) {
        self.op(GLOBAL_GET);
        unsigned(&mut self.bytes, u64::from(global));
    }

    pub fn global_set(&mut self, global: u32) {
        self.op(GLOBAL_SET);
        unsigned(&mut self.bytes, u64::from(global));
    }

    pub fn i32_const(&mut self, n: i32) {
        self.op(I32_CONST);
        signed(&mut self.bytes, i64::from(n));
    }

    pub fn i64_const(&mut self, n: i64) {
        self.op(I64_CONST);
        signed(&mut self.bytes, n);
    }

    pub fn call(&mut self, function: u32) {
        self.op(CALL);
        unsigned(&mut self.bytes, u64::from(function));
    }

    pub fn br(&mut self, depth: u32) {
        self.op(BR);
        unsigned(&mut self.bytes, u64::from(depth));
    }

    /// Leaves the structure `depth` out when the 32-bit number on the stack is
    /// not 0.
    pub fn br_if(&mut self, depth: u32) {
        self.op(BR_IF);
        unsigned(&mut self.bytes, u64::from(depth));
    }

    /// A block, a loop, or an `if`, which gives nothing.
    pub fn open(&mut self, op: u8) {
        self.op(op);
        self.bytes.push(NO_VALUE);
    }

    /// A choice between the first `labels.len()` blocks that enclose it, by
    /// the 32-bit number on the stack, and the last of them for any greater.
    pub fn br_table(&mut self, labels: u32) {
        self.op(BR_TABLE);
        unsigned(&mut self.bytes, u64::from(labels - 1));
        for label in 0..labels {
            unsigned(&mut self.bytes, u64::from(label));
        }
    }

    /// A read or a write through the address on the stack, at any alignment.
    pub fn memory(&mut self, op: u8) {
        self.op(op);
        self.bytes.push(0);
        self.bytes.push(0);
    }

    pub fn memory_size(&mut self) {
        self.op(MEMORY_SIZE);
        self.bytes.push(0);
    }

    pub fn memory_grow(&mut self) {
        self.op(MEMORY_GROW);
        self.bytes.push(0);
    }
}
