//! The examples of RFC 7541 appendix C: header lists given to a decoder one
//! block after another, with the headers each gives and the dynamic table it
//! leaves, as the appendix writes them.

/// One example of RFC 7541 appendix C: the bytes of a block, the headers it
/// gives, and the dynamic table after it, newest first, with the size of each
/// entry as the appendix gives it.
pub struct Example {
    pub block: &'static str,
    pub headers: &'static [(&'static str, &'static str)],
    pub table: &'static [(&'static str, &'static str, usize)],
}

/// A series of examples that share one decoder, with the size of its table.
pub struct Series {
    pub name: &'static str,
    pub table: usize,
    pub examples: &'static [Example],
}

pub const SERIES: &[Series] = &[
    Series {
        name: "request examples without huffman coding",
        table: 4096,
        examples: &[
            Example {
                block: "828684410f7777772e6578616d706c652e636f6d",
                headers: &[(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com")],
                table: &[(":authority", "www.example.com", 57)],
            },
            Example {
                block: "828684be58086e6f2d6361636865",
                headers: &[(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com"), ("cache-control", "no-cache")],
                table: &[("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
            },
            Example {
                block: "828785bf400a637573746f6d2d6b65790c637573746f6d2d76616c7565",
                headers: &[(":method", "GET"), (":scheme", "https"), (":path", "/index.html"), (":authority", "www.example.com"), ("custom-key", "custom-value")],
                table: &[("custom-key", "custom-value", 54), ("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
            },
        ],
    },
    Series {
        name: "request examples with huffman coding",
        table: 4096,
        examples: &[
            Example {
                block: "828684418cf1e3c2e5f23a6ba0ab90f4ff",
                headers: &[(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com")],
                table: &[(":authority", "www.example.com", 57)],
            },
            Example {
                block: "828684be5886a8eb10649cbf",
                headers: &[(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com"), ("cache-control", "no-cache")],
                table: &[("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
            },
            Example {
                block: "828785bf408825a849e95ba97d7f8925a849e95bb8e8b4bf",
                headers: &[(":method", "GET"), (":scheme", "https"), (":path", "/index.html"), (":authority", "www.example.com"), ("custom-key", "custom-value")],
                table: &[("custom-key", "custom-value", 54), ("cache-control", "no-cache", 53), (":authority", "www.example.com", 57)],
            },
        ],
    },
    Series {
        name: "response examples without huffman coding",
        table: 256,
        examples: &[
            Example {
                block: "4803333032580770726976617465611d4d6f6e2c203231204f637420323031332032303a31333a323120474d546e1768747470733a2f2f7777772e6578616d706c652e636f6d",
                headers: &[(":status", "302"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:21 GMT"), ("location", "https://www.example.com")],
                table: &[("location", "https://www.example.com", 63), ("date", "Mon, 21 Oct 2013 20:13:21 GMT", 65), ("cache-control", "private", 52), (":status", "302", 42)],
            },
            Example {
                block: "4803333037c1c0bf",
                headers: &[(":status", "307"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:21 GMT"), ("location", "https://www.example.com")],
                table: &[(":status", "307", 42), ("location", "https://www.example.com", 63), ("date", "Mon, 21 Oct 2013 20:13:21 GMT", 65), ("cache-control", "private", 52)],
            },
            Example {
                block: "88c1611d4d6f6e2c203231204f637420323031332032303a31333a323220474d54c05a04677a69707738666f6f3d4153444a4b48514b425a584f5157454f50495541585157454f49553b206d61782d6167653d333630303b2076657273696f6e3d31",
                headers: &[(":status", "200"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:22 GMT"), ("location", "https://www.example.com"), ("content-encoding", "gzip"), ("set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1")],
                table: &[("set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1", 98), ("content-encoding", "gzip", 52), ("date", "Mon, 21 Oct 2013 20:13:22 GMT", 65)],
            },
        ],
    },
    Series {
        name: "response examples with huffman coding",
        table: 256,
        examples: &[
            Example {
                block: "488264025885aec3771a4b6196d07abe941054d444a8200595040b8166e082a62d1bff6e919d29ad171863c78f0b97c8e9ae82ae43d3",
                headers: &[(":status", "302"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:21 GMT"), ("location", "https://www.example.com")],
                table: &[("location", "https://www.example.com", 63), ("date", "Mon, 21 Oct 2013 20:13:21 GMT", 65), ("cache-control", "private", 52), (":status", "302", 42)],
            },
            Example {
                block: "4883640effc1c0bf",
                headers: &[(":status", "307"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:21 GMT"), ("location", "https://www.example.com")],
                table: &[(":status", "307", 42), ("location", "https://www.example.com", 63), ("date", "Mon, 21 Oct 2013 20:13:21 GMT", 65), ("cache-control", "private", 52)],
            },
            Example {
                block: "88c16196d07abe941054d444a8200595040b8166e084a62d1bffc05a839bd9ab77ad94e7821dd7f2e6c7b335dfdfcd5b3960d5af27087f3672c1ab270fb5291f9587316065c003ed4ee5b1063d5007",
                headers: &[(":status", "200"), ("cache-control", "private"), ("date", "Mon, 21 Oct 2013 20:13:22 GMT"), ("location", "https://www.example.com"), ("content-encoding", "gzip"), ("set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1")],
                table: &[("set-cookie", "foo=ASDJKHQKBZXOQWEOPIUAXQWEOIU; max-age=3600; version=1", 98), ("content-encoding", "gzip", 52), ("date", "Mon, 21 Oct 2013 20:13:22 GMT", 65)],
            },
        ],
    },
];

/// The static table of RFC 7541 appendix A, from the index 1.
pub const STATIC: [(&str, &str); 61] = [
    (":authority", ""),
    (":method", "GET"),
    (":method", "POST"),
    (":path", "/"),
    (":path", "/index.html"),
    (":scheme", "http"),
    (":scheme", "https"),
    (":status", "200"),
    (":status", "204"),
    (":status", "206"),
    (":status", "304"),
    (":status", "400"),
    (":status", "404"),
    (":status", "500"),
    ("accept-charset", ""),
    ("accept-encoding", "gzip, deflate"),
    ("accept-language", ""),
    ("accept-ranges", ""),
    ("accept", ""),
    ("access-control-allow-origin", ""),
    ("age", ""),
    ("allow", ""),
    ("authorization", ""),
    ("cache-control", ""),
    ("content-disposition", ""),
    ("content-encoding", ""),
    ("content-language", ""),
    ("content-length", ""),
    ("content-location", ""),
    ("content-range", ""),
    ("content-type", ""),
    ("cookie", ""),
    ("date", ""),
    ("etag", ""),
    ("expect", ""),
    ("expires", ""),
    ("from", ""),
    ("host", ""),
    ("if-match", ""),
    ("if-modified-since", ""),
    ("if-none-match", ""),
    ("if-range", ""),
    ("if-unmodified-since", ""),
    ("last-modified", ""),
    ("link", ""),
    ("location", ""),
    ("max-forwards", ""),
    ("proxy-authenticate", ""),
    ("proxy-authorization", ""),
    ("range", ""),
    ("referer", ""),
    ("refresh", ""),
    ("retry-after", ""),
    ("server", ""),
    ("set-cookie", ""),
    ("strict-transport-security", ""),
    ("transfer-encoding", ""),
    ("user-agent", ""),
    ("vary", ""),
    ("via", ""),
    ("www-authenticate", ""),
];

/// The Huffman code of RFC 7541 appendix B: the code as a number and how many bits it
/// has, for each byte and then for the end of a text.
pub const HUFFMAN: [(u32, u32); 257] = [
    (0x1ff8, 13), (0x7fffd8, 23), (0xfffffe2, 28), (0xfffffe3, 28), (0xfffffe4, 28),
    (0xfffffe5, 28), (0xfffffe6, 28), (0xfffffe7, 28), (0xfffffe8, 28), (0xffffea, 24),
    (0x3ffffffc, 30), (0xfffffe9, 28), (0xfffffea, 28), (0x3ffffffd, 30), (0xfffffeb, 28),
    (0xfffffec, 28), (0xfffffed, 28), (0xfffffee, 28), (0xfffffef, 28), (0xffffff0, 28),
    (0xffffff1, 28), (0xffffff2, 28), (0x3ffffffe, 30), (0xffffff3, 28), (0xffffff4, 28),
    (0xffffff5, 28), (0xffffff6, 28), (0xffffff7, 28), (0xffffff8, 28), (0xffffff9, 28),
    (0xffffffa, 28), (0xffffffb, 28), (0x14, 6), (0x3f8, 10), (0x3f9, 10), (0xffa, 12),
    (0x1ff9, 13), (0x15, 6), (0xf8, 8), (0x7fa, 11), (0x3fa, 10), (0x3fb, 10), (0xf9, 8),
    (0x7fb, 11), (0xfa, 8), (0x16, 6), (0x17, 6), (0x18, 6), (0x0, 5), (0x1, 5), (0x2, 5),
    (0x19, 6), (0x1a, 6), (0x1b, 6), (0x1c, 6), (0x1d, 6), (0x1e, 6), (0x1f, 6), (0x5c, 7),
    (0xfb, 8), (0x7ffc, 15), (0x20, 6), (0xffb, 12), (0x3fc, 10), (0x1ffa, 13), (0x21, 6),
    (0x5d, 7), (0x5e, 7), (0x5f, 7), (0x60, 7), (0x61, 7), (0x62, 7), (0x63, 7), (0x64, 7),
    (0x65, 7), (0x66, 7), (0x67, 7), (0x68, 7), (0x69, 7), (0x6a, 7), (0x6b, 7), (0x6c, 7),
    (0x6d, 7), (0x6e, 7), (0x6f, 7), (0x70, 7), (0x71, 7), (0x72, 7), (0xfc, 8), (0x73, 7),
    (0xfd, 8), (0x1ffb, 13), (0x7fff0, 19), (0x1ffc, 13), (0x3ffc, 14), (0x22, 6), (0x7ffd, 15),
    (0x3, 5), (0x23, 6), (0x4, 5), (0x24, 6), (0x5, 5), (0x25, 6), (0x26, 6), (0x27, 6),
    (0x6, 5), (0x74, 7), (0x75, 7), (0x28, 6), (0x29, 6), (0x2a, 6), (0x7, 5), (0x2b, 6),
    (0x76, 7), (0x2c, 6), (0x8, 5), (0x9, 5), (0x2d, 6), (0x77, 7), (0x78, 7), (0x79, 7),
    (0x7a, 7), (0x7b, 7), (0x7ffe, 15), (0x7fc, 11), (0x3ffd, 14), (0x1ffd, 13),
    (0xffffffc, 28), (0xfffe6, 20), (0x3fffd2, 22), (0xfffe7, 20), (0xfffe8, 20),
    (0x3fffd3, 22), (0x3fffd4, 22), (0x3fffd5, 22), (0x7fffd9, 23), (0x3fffd6, 22),
    (0x7fffda, 23), (0x7fffdb, 23), (0x7fffdc, 23), (0x7fffdd, 23), (0x7fffde, 23),
    (0xffffeb, 24), (0x7fffdf, 23), (0xffffec, 24), (0xffffed, 24), (0x3fffd7, 22),
    (0x7fffe0, 23), (0xffffee, 24), (0x7fffe1, 23), (0x7fffe2, 23), (0x7fffe3, 23),
    (0x7fffe4, 23), (0x1fffdc, 21), (0x3fffd8, 22), (0x7fffe5, 23), (0x3fffd9, 22),
    (0x7fffe6, 23), (0x7fffe7, 23), (0xffffef, 24), (0x3fffda, 22), (0x1fffdd, 21),
    (0xfffe9, 20), (0x3fffdb, 22), (0x3fffdc, 22), (0x7fffe8, 23), (0x7fffe9, 23),
    (0x1fffde, 21), (0x7fffea, 23), (0x3fffdd, 22), (0x3fffde, 22), (0xfffff0, 24),
    (0x1fffdf, 21), (0x3fffdf, 22), (0x7fffeb, 23), (0x7fffec, 23), (0x1fffe0, 21),
    (0x1fffe1, 21), (0x3fffe0, 22), (0x1fffe2, 21), (0x7fffed, 23), (0x3fffe1, 22),
    (0x7fffee, 23), (0x7fffef, 23), (0xfffea, 20), (0x3fffe2, 22), (0x3fffe3, 22),
    (0x3fffe4, 22), (0x7ffff0, 23), (0x3fffe5, 22), (0x3fffe6, 22), (0x7ffff1, 23),
    (0x3ffffe0, 26), (0x3ffffe1, 26), (0xfffeb, 20), (0x7fff1, 19), (0x3fffe7, 22),
    (0x7ffff2, 23), (0x3fffe8, 22), (0x1ffffec, 25), (0x3ffffe2, 26), (0x3ffffe3, 26),
    (0x3ffffe4, 26), (0x7ffffde, 27), (0x7ffffdf, 27), (0x3ffffe5, 26), (0xfffff1, 24),
    (0x1ffffed, 25), (0x7fff2, 19), (0x1fffe3, 21), (0x3ffffe6, 26), (0x7ffffe0, 27),
    (0x7ffffe1, 27), (0x3ffffe7, 26), (0x7ffffe2, 27), (0xfffff2, 24), (0x1fffe4, 21),
    (0x1fffe5, 21), (0x3ffffe8, 26), (0x3ffffe9, 26), (0xffffffd, 28), (0x7ffffe3, 27),
    (0x7ffffe4, 27), (0x7ffffe5, 27), (0xfffec, 20), (0xfffff3, 24), (0xfffed, 20),
    (0x1fffe6, 21), (0x3fffe9, 22), (0x1fffe7, 21), (0x1fffe8, 21), (0x7ffff3, 23),
    (0x3fffea, 22), (0x3fffeb, 22), (0x1ffffee, 25), (0x1ffffef, 25), (0xfffff4, 24),
    (0xfffff5, 24), (0x3ffffea, 26), (0x7ffff4, 23), (0x3ffffeb, 26), (0x7ffffe6, 27),
    (0x3ffffec, 26), (0x3ffffed, 26), (0x7ffffe7, 27), (0x7ffffe8, 27), (0x7ffffe9, 27),
    (0x7ffffea, 27), (0x7ffffeb, 27), (0xffffffe, 28), (0x7ffffec, 27), (0x7ffffed, 27),
    (0x7ffffee, 27), (0x7ffffef, 27), (0x7fffff0, 27), (0x3ffffee, 26), (0x3fffffff, 30),
];

/// The examples of appendix C.2, each of a decoder of its own.
pub const SINGLES: &[Series] = &[
    Series {
        name: "a literal header field with indexing",
        table: 4096,
        examples: &[Example {
            block: "400a637573746f6d2d6b65790d637573746f6d2d686561646572",
            headers: &[("custom-key", "custom-header")],
            table: &[("custom-key", "custom-header", 55)],
        }],
    },
    Series {
        name: "a literal header field without indexing",
        table: 4096,
        examples: &[Example {
            block: "040c2f73616d706c652f70617468",
            headers: &[(":path", "/sample/path")],
            table: &[],
        }],
    },
    Series {
        name: "a literal header field never indexed",
        table: 4096,
        examples: &[Example {
            block: "100870617373776f726406736563726574",
            headers: &[("password", "secret")],
            table: &[],
        }],
    },
    Series {
        name: "an indexed header field",
        table: 4096,
        examples: &[Example {
            block: "82",
            headers: &[(":method", "GET")],
            table: &[],
        }],
    },
];
