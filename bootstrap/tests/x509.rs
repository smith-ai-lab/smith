//! `std.x509` reads one certificate with `std.der`, as `knowledge/library/der.md`
//! says.
//!
//! A certificate is kept in `tests/certs` as bytes beside what
//! `openssl x509 -text` printed for it. A program is built from the bytes of each
//! case and prints what the library gives, and the test compares that with what
//! OpenSSL printed, and with the model of DER of `support/der_model.rs`.

use smith::{lower, scalar};

use crate::Test;
use crate::der_model::{Node, bytes, literal, model_children, model_line, model_read, run, tlv, values_program};
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_certificate_is_read_to_what_openssl_printed_for_it",
        a_certificate_is_read_to_what_openssl_printed_for_it,
    ),
    (
        "a_certificate_with_an_extension_marked_critical_that_is_not_read_is_an_error",
        a_certificate_with_an_extension_marked_critical_that_is_not_read_is_an_error,
    ),
    (
        "a_time_is_read_as_the_seconds_since_1970_in_both_forms_and_nothing_else_is",
        a_time_is_read_as_the_seconds_since_1970_in_both_forms_and_nothing_else_is,
    ),
    (
        "a_certificate_that_is_damaged_in_any_way_is_a_value_or_an_error_and_nothing_else",
        a_certificate_that_is_damaged_in_any_way_is_a_value_or_an_error_and_nothing_else,
    ),
    (
        "pem_reads_a_file_of_several_certificates_to_the_bytes_of_each",
        pem_reads_a_file_of_several_certificates_to_the_bytes_of_each,
    ),
    (
        "the_certificates_that_rfc_7468_gives_as_examples_are_read_to_their_bytes",
        the_certificates_that_rfc_7468_gives_as_examples_are_read_to_their_bytes,
    ),
    (
        "a_file_that_is_not_pem_of_certificates_is_an_error_and_pem_reads_what_the_model_reads",
        a_file_that_is_not_pem_of_certificates_is_an_error_and_pem_reads_what_the_model_reads,
    ),
    (
        "a_certificate_of_a_version_other_than_3_is_an_error",
        a_certificate_of_a_version_other_than_3_is_an_error,
    ),
    (
        "a_serial_number_is_read_to_20_octets_and_is_an_error_below_one",
        a_serial_number_is_read_to_20_octets_and_is_an_error_below_one,
    ),
    (
        "an_extension_that_is_written_twice_is_an_error_whichever_it_is",
        an_extension_that_is_written_twice_is_an_error_whichever_it_is,
    ),
    (
        "an_extension_that_is_read_and_is_not_written_right_is_an_error_and_one_that_is_not_read_is_passed_over",
        an_extension_that_is_read_and_is_not_written_right_is_an_error_and_one_that_is_not_read_is_passed_over,
    ),
    (
        "the_identifiers_of_the_extensions_that_are_read_are_those_of_appendix_a_of_the_standard",
        the_identifiers_of_the_extensions_that_are_read_are_those_of_appendix_a_of_the_standard,
    ),
    (
        "reading_a_certificate_asks_for_memory_that_does_not_depend_on_the_bytes_of_its_names",
        reading_a_certificate_asks_for_memory_that_does_not_depend_on_the_bytes_of_its_names,
    ),
];

/// Source that prints a line for each certificate that `certificate` reads.
fn certificates_program(inputs: &[Vec<u8>]) -> String {
    let mut source = String::from(
        "use std.bits (bit_and, shift_right)
use std.console (console, print)
use std.int (wrapping_add)
use std.list (freeze, push)
use std.utf8 (utf8)
use std.x509 (certificate)

fn digit(d: Byte) -> Byte {
  if d < 10 { wrapping_add(d, '0') } else { wrapping_add(d, 87) }
}

fn hex(b: Slice[Byte]) -> str ! alloc {
  var built: List[Byte] = []
  loop i in 0..b.size {
    built = push(built, digit(shift_right(b[i], 4)))
    built = push(built, digit(bit_and(b[i], 15)))
  }
  match utf8(freeze(built)) {
    .ok(t) { t }
    .err(e) { \"\" }
  }
}

fn dotted(arcs: List[Nat]) -> str ! alloc {
  var s = \"\"
  loop i in 0..arcs.size {
    if i == 0 { s = \"{arcs[i]}\" } else { s = \"{s}.{arcs[i]}\" }
  }
  s
}

fn joined(names: List[str]) -> str ! alloc {
  var s = \"\"
  loop i in 0..names.size {
    if i == 0 { s = names[i] } else { s = \"{s},{names[i]}\" }
  }
  s
}

fn show(b: Slice[Byte]) ! console, alloc {
  match certificate(b) {
    .err(e) { print(\"err\") }
    .ok(c) {
      authority = match c.authority {
        .some(a) { \"{a}\" }
        .none { \"none\" }
      }
      path = match c.path_length {
        .some(n) { \"{n}\" }
        .none { \"none\" }
      }
      print(\"ok serial={hex(c.serial)} not_before={c.not_before} not_after={c.not_after} algorithm={dotted(c.algorithm)} key_algorithm={dotted(c.key_algorithm)} key={hex(c.key)} names={joined(c.names)} authority={authority} path={path} signed={hex(c.signed)} signature={hex(c.signature)} issuer={hex(c.issuer)} subject={hex(c.subject)}\")
    }
  }
}

",
    );
    for (i, input) in inputs.iter().enumerate() {
        source += &format!("fn input{i}() -> Slice[Byte] {{ {} }}\n\n", literal(input));
    }
    source += "fn main() ! console, alloc {\n";
    for i in 0..inputs.len() {
        source += &format!("  show(input{i}())\n");
    }
    source + "}\n"
}

// The tests of certificates.

const CERTIFICATES: &[&str] = &[
    "ISRG_Root_X1",
    "ISRG_Root_X2",
    "DigiCert_Global_Root_G3",
    "smith_test_ca",
    "smith_test_leaf",
];

fn certificate_file(name: &str, extension: &str) -> Vec<u8> {
    let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
        .join("tests/certs")
        .join(format!("{name}.{extension}"));
    std::fs::read(&path).unwrap_or_else(|e| panic!("{}: {e}", path.display()))
}

/// What `openssl x509 -text` printed, read for the facts the test compares.
struct Printed {
    serial: String,
    not_before: u64,
    not_after: u64,
    key_algorithm: String,
    key: Option<String>,
    names: Vec<String>,
    authority: String,
    path: String,
    issuer: String,
    subject: String,
}

/// The seconds since 1970 of a time as OpenSSL writes it, `Nov 19 16:56:09 2026 GMT`.
fn printed_time(text: &str) -> u64 {
    let parts: Vec<&str> = text.split_whitespace().collect();
    let month = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
        .iter()
        .position(|m| *m == parts[0])
        .unwrap() as u64
        + 1;
    let day: u64 = parts[1].parse().unwrap();
    let clock: Vec<u64> = parts[2].split(':').map(|n| n.parse().unwrap()).collect();
    let year: u64 = parts[3].parse().unwrap();
    days_from_civil(year, month, day) * 86400 + clock[0] * 3600 + clock[1] * 60 + clock[2]
}

/// Days from 1970-01-01 to the date, by the algorithm of Howard Hinnant.
fn days_from_civil(year: u64, month: u64, day: u64) -> u64 {
    let y = if month <= 2 { year - 1 } else { year };
    let era = y / 400;
    let yoe = y - era * 400;
    let m = if month > 2 { month - 3 } else { month + 9 };
    let doy = (153 * m + 2) / 5 + day - 1;
    let doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    era * 146097 + doe - 719468
}

fn printed(name: &str) -> Printed {
    let text = String::from_utf8(certificate_file(name, "txt")).unwrap();
    let lines: Vec<&str> = text.lines().map(str::trim).collect();
    let after = |label: &str| -> &str {
        lines
            .iter()
            .find_map(|l| l.strip_prefix(label))
            .unwrap_or_else(|| panic!("{name}: no `{label}`"))
            .trim()
    };
    let serial_at = lines.iter().position(|l| *l == "Serial Number:").unwrap();
    let serial: String = lines[serial_at + 1].replace(':', "");
    let key_at = lines.iter().position(|l| l.starts_with("pub:")).map(|i| {
        let mut hex = String::new();
        for l in &lines[i + 1..] {
            if !l.chars().all(|c| c.is_ascii_hexdigit() || c == ':') {
                break;
            }
            hex += &l.replace(':', "");
        }
        hex
    });
    let names = lines
        .iter()
        .find(|l| l.contains("DNS:"))
        .map(|l| {
            l.split(", ")
                .filter_map(|p| p.strip_prefix("DNS:"))
                .map(str::to_string)
                .collect()
        })
        .unwrap_or_default();
    let constraints = lines.iter().find_map(|l| l.strip_prefix("CA:"));
    let (authority, path) = match constraints {
        None => ("none".to_string(), "none".to_string()),
        Some(c) => {
            let ca = if c.starts_with("TRUE") { "true" } else { "false" };
            let path = c.split("pathlen:").nth(1).unwrap_or("none");
            (ca.to_string(), path.to_string())
        }
    };
    let algorithm = after("Public Key Algorithm:").to_string();
    Printed {
        serial,
        not_before: printed_time(after("Not Before:")),
        not_after: printed_time(after("Not After :")),
        key_algorithm: algorithm,
        key: key_at,
        names,
        authority,
        path,
        issuer: after("Issuer:").to_string(),
        subject: after("Subject:").to_string(),
    }
}

/// The value of `key=` and the other fields of a line a certificate gave.
fn field<'a>(line: &'a str, name: &str) -> &'a str {
    line.split(' ')
        .find_map(|f| f.strip_prefix(&format!("{name}=")))
        .unwrap_or_else(|| panic!("no `{name}` in {line}"))
}

fn hex_of(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// A name of DER as OpenSSL prints it: `C = US, O = Smith Test, CN = Smith Test CA`.
fn print_name(name: &[u8]) -> String {
    let set = model_read(name).unwrap();
    let mut parts = Vec::new();
    for rdn in model_children(&name[set.start..set.end]).unwrap() {
        let rdn_bytes = &name[set.start..set.end][rdn.start..rdn.end];
        for attribute in model_children(rdn_bytes).unwrap() {
            let a = &rdn_bytes[attribute.start..attribute.end];
            let kids = model_children(a).unwrap();
            let oid = &a[kids[0].start..kids[0].end];
            let value = &a[kids[1].start..kids[1].end];
            let short = match oid {
                [0x55, 0x04, 3] => "CN",
                [0x55, 0x04, 6] => "C",
                [0x55, 0x04, 7] => "L",
                [0x55, 0x04, 8] => "ST",
                [0x55, 0x04, 10] => "O",
                [0x55, 0x04, 11] => "OU",
                other => panic!("an attribute {other:?} the test does not name"),
            };
            parts.push(format!("{short} = {}", String::from_utf8_lossy(value)));
        }
    }
    parts.join(", ")
}

fn dotted(oid: &[u8]) -> String {
    let mut arcs = vec![u64::from(oid[0] / 40), u64::from(oid[0] % 40)];
    let mut n = 0u64;
    for b in &oid[1..] {
        n = n << 7 | u64::from(b & 127);
        if b & 128 == 0 {
            arcs.push(n);
            n = 0;
        }
    }
    arcs.iter().map(u64::to_string).collect::<Vec<_>>().join(".")
}

/// The value of the bytes that is at the path of indexes, with its tag and length.
fn part<'a>(b: &'a [u8], path: &[usize]) -> &'a [u8] {
    let mut at = b;
    for i in path {
        let v = model_read(at).unwrap();
        let kids = model_children(&at[v.start..v.end]).unwrap();
        at = &at[v.start + kids[*i].from..v.start + kids[*i].end];
    }
    at
}

/// The contents of a value.
fn contents(b: &[u8]) -> &[u8] {
    let v = model_read(b).unwrap();
    &b[v.start..v.end]
}

fn a_certificate_is_read_to_what_openssl_printed_for_it() {
    let inputs: Vec<Vec<u8>> = CERTIFICATES.iter().map(|n| certificate_file(n, "der")).collect();
    let lines = run(&certificates_program(&inputs));
    assert_eq!(lines.len(), inputs.len());
    for ((name, der), line) in CERTIFICATES.iter().zip(&inputs).zip(&lines) {
        let p = printed(name);
        assert!(line.starts_with("ok "), "{name}: {line}");
        assert_eq!(field(line, "serial").trim_start_matches('0'), p.serial.trim_start_matches('0'), "{name} serial");
        assert_eq!(field(line, "not_before"), p.not_before.to_string(), "{name} not_before");
        assert_eq!(field(line, "not_after"), p.not_after.to_string(), "{name} not_after");
        let names = field(line, "names");
        let names: Vec<String> = if names.is_empty() { vec![] } else { names.split(',').map(str::to_string).collect() };
        assert_eq!(names, p.names, "{name} names");
        assert_eq!(field(line, "authority"), p.authority, "{name} authority");
        assert_eq!(field(line, "path"), p.path, "{name} path");
        // The names and the part that is signed are the bytes of the certificate.
        let tbs = part(der, &[0]);
        assert_eq!(field(line, "signed"), hex_of(tbs), "{name} signed");
        let issuer_bytes = part(der, &[0, 3]);
        let subject_bytes = part(der, &[0, 5]);
        assert_eq!(field(line, "issuer"), hex_of(issuer_bytes), "{name} issuer");
        assert_eq!(field(line, "subject"), hex_of(subject_bytes), "{name} subject");
        assert_eq!(print_name(issuer_bytes), p.issuer, "{name} issuer printed");
        assert_eq!(print_name(subject_bytes), p.subject, "{name} subject printed");
        // The key, and the algorithms that name it and the signature.
        let info = part(der, &[0, 6]);
        let oid = contents(part(info, &[0, 0]));
        let family = if dotted(oid) == "1.2.840.10045.2.1" { "id-ecPublicKey" } else { "rsaEncryption" };
        assert!(p.key_algorithm.contains(family), "{name}: {} for {}", p.key_algorithm, dotted(oid));
        assert_eq!(field(line, "key_algorithm"), dotted(oid), "{name} key algorithm");
        assert_eq!(field(line, "key"), hex_of(&contents(part(info, &[1]))[1..]), "{name} key");
        if let Some(key) = p.key {
            assert_eq!(field(line, "key"), key, "{name} key as printed");
        }
        assert_eq!(field(line, "algorithm"), dotted(contents(part(der, &[1, 0]))), "{name} algorithm");
        assert_eq!(field(line, "signature"), hex_of(&contents(part(der, &[2]))[1..]), "{name} signature");
    }
}

/// A DER tree read from bytes, which the tests change and write again.
fn tree(b: &[u8]) -> Node {
    let c = contents(b);
    if b[0] & 32 != 0 {
        Node::Parts(b[0], model_children(c).unwrap().iter().map(|p| tree(&c[p.from..p.end])).collect())
    } else {
        Node::Leaf(b[0], c.to_vec())
    }
}

/// The certificate with the tree changed by `edit`.
fn changed(name: &str, edit: impl Fn(&mut Node)) -> Vec<u8> {
    let mut t = tree(&certificate_file(name, "der"));
    edit(&mut t);
    t.encode()
}

fn parts_of(n: &mut Node) -> &mut Vec<Node> {
    match n {
        Node::Parts(_, p) => p,
        Node::Leaf(..) => panic!("a leaf has no parts"),
    }
}

/// The extension of the certificate that the edit adds: critical or not.
fn with_extension(oid: &[u8], critical: bool, value: Vec<u8>) -> impl Fn(&mut Node) {
    let extension = {
        let mut parts = vec![Node::Leaf(6, oid.to_vec())];
        if critical {
            parts.push(Node::Leaf(1, vec![255]));
        }
        parts.push(Node::Leaf(4, value));
        Node::Parts(0x30, parts)
    };
    move |cert| {
        let tbs = &mut parts_of(cert)[0];
        let fields = parts_of(tbs);
        let wrapper = fields.last_mut().unwrap();
        let list = &mut parts_of(wrapper)[0];
        parts_of(list).push(extension.clone());
    }
}

fn a_certificate_with_an_extension_marked_critical_that_is_not_read_is_an_error() {
    let unknown = [0x2b, 0x06, 1, 4, 1, 0x82, 0x37, 2, 4, 3];
    let inputs = vec![
        changed("smith_test_leaf", |_| {}),
        changed("smith_test_leaf", with_extension(&unknown, false, tlv(5, &[]))),
        changed("smith_test_leaf", with_extension(&unknown, true, tlv(5, &[]))),
        // The identifier of key usage, which is read, in a second extension.
        changed("smith_test_leaf", with_extension(&[0x55, 29, 19], true, tlv(0x30, &[]))),
        changed("smith_test_leaf", with_extension(&[0x55, 29, 17], false, tlv(0x30, &[]))),
        // The key usage extension, which is read, and which is critical.
        changed("smith_test_leaf", with_extension(&[0x55, 29, 15], false, tlv(5, &[]))),
    ];
    let lines = run(&certificates_program(&inputs));
    assert!(lines[0].starts_with("ok "), "{}", lines[0]);
    assert!(lines[1].starts_with("ok "), "an extension that is not critical is passed over: {}", lines[1]);
    assert_eq!(lines[2], "err", "a critical extension that is not read");
    assert_eq!(lines[3], "err", "an extension that is written twice");
    assert_eq!(lines[4], "err", "a name extension that is written twice");
    assert_eq!(lines[5], "err", "a key usage that is not a bit string");
}

/// A time as it is written in each of the two forms, and what it is.
fn time_cases(g: &mut Gen) -> (Vec<u8>, Option<u64>) {
    let year = 1950 + g.below(150) as u64;
    let month = 1 + g.below(12) as u64;
    let leap = year.is_multiple_of(4) && (!year.is_multiple_of(100) || year.is_multiple_of(400));
    let days = [31, if leap { 29 } else { 28 }, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
    let in_month = days[month as usize - 1];
    // The last day of the month is the one that is most often got wrong.
    let day = if g.one_in(3) { in_month as u64 } else { 1 + g.below(in_month) as u64 };
    let (h, m, s) = (g.below(24) as u64, g.below(60) as u64, g.below(60) as u64);
    let rest = format!("{month:02}{day:02}{h:02}{m:02}{s:02}Z");
    // A year before 1970 has no seconds since 1970 that are a whole number.
    let seconds = (year >= 1970).then(|| days_from_civil(year, month, day) * 86400 + h * 3600 + m * 60 + s);
    let utc = (1950..2050).contains(&year);
    match g.below(4) {
        0 if utc => (format!("{:02}{rest}", year % 100).into_bytes(), seconds),
        1 => (format!("{year:04}{rest}").into_bytes(), seconds),
        // A time that is not written as DER writes it, or is not a time.
        2 => {
            let mut t = format!("{year:04}{rest}").into_bytes();
            let at = g.below(t.len());
            t[at] = *g.pick(&[b'x', b' ', b'+', 0]);
            (t, None)
        }
        _ => {
            let mut t = format!("{year:04}{rest}").into_bytes();
            match g.below(4) {
                0 => {
                    t.pop();
                }
                1 => {
                    t.pop();
                    t.extend(b"+0100");
                }
                2 => {
                    // The day after the last of the month.
                    t[6..8].copy_from_slice(format!("{:02}", in_month + 1).as_bytes());
                }
                _ => {
                    // A month, a day, or a time of day that there is not.
                    let bad = *g.pick(&[(4, "13"), (4, "00"), (6, "32"), (6, "00"), (8, "24"), (10, "60"), (12, "60")]);
                    t[bad.0..bad.0 + 2].copy_from_slice(bad.1.as_bytes());
                }
            }
            (t, None)
        }
    }
}

/// A time as DER writes it, and the seconds since 1970 it stands for.
type Time = (Vec<u8>, Option<u64>);

/// The times where the calendar and the two forms are easiest to get wrong,
/// each as the before time, with a good time after it.
fn fixed_times() -> Vec<(Time, Time)> {
    let good = (b"20300101000000Z".to_vec(), Some(days_from_civil(2030, 1, 1) * 86400));
    let at = |year: u64, month: u64, day: u64| Some(days_from_civil(year, month, day) * 86400);
    [
        // The year of a two-digit time is 19 from 50 and 20 below it.
        ("491231000000Z", at(2049, 12, 31)),
        ("500101000000Z", None),
        ("691231000000Z", None),
        ("700101000000Z", at(1970, 1, 1)),
        ("991231000000Z", at(1999, 12, 31)),
        ("000101000000Z", at(2000, 1, 1)),
        // February in the years that are leap, and the years that are not.
        ("960229000000Z", at(1996, 2, 29)),
        ("000229000000Z", at(2000, 2, 29)),
        ("990229000000Z", None),
        ("000230000000Z", None),
        ("19000229000000Z", None),
        ("21000229000000Z", None),
        ("20000229000000Z", at(2000, 2, 29)),
        ("20240430000000Z", at(2024, 4, 30)),
        ("20240431000000Z", None),
        ("20241231235959Z", Some(days_from_civil(2024, 12, 31) * 86400 + 86399)),
        // No fraction of a second, and no zone but Z.
        ("20240101000000.5Z", None),
        ("20240101000000", None),
        ("20240101000000+0000", None),
    ]
    .into_iter()
    .map(|(t, s)| ((t.as_bytes().to_vec(), s), good.clone()))
    .collect()
}

fn a_time_is_read_as_the_seconds_since_1970_in_both_forms_and_nothing_else_is() {
    generate::check(
        5,
        4,
        |g| {
            let mut cases = fixed_times();
            cases.extend((0..10).map(|_| (time_cases(g), time_cases(g))));
            cases
        },
        |cases| {
            let mut inputs = Vec::new();
            for ((before, _), (after, _)) in cases {
                inputs.push(changed("smith_test_leaf", |cert| {
                    let validity = &mut parts_of(&mut parts_of(cert)[0])[4];
                    let kind = |t: &Vec<u8>| if t.len() == 13 { 23 } else { 24 };
                    *parts_of(validity) = vec![Node::Leaf(kind(before), before.clone()), Node::Leaf(kind(after), after.clone())];
                }));
            }
            let lines = run(&certificates_program(&inputs));
            for (((_, b), (_, a)), line) in cases.iter().zip(&lines) {
                match (b, a) {
                    (Some(b), Some(a)) => {
                        assert_eq!(field(line, "not_before"), b.to_string(), "{line}");
                        assert_eq!(field(line, "not_after"), a.to_string(), "{line}");
                    }
                    _ => assert_eq!(line, "err"),
                }
            }
        },
    );
}

fn a_certificate_that_is_damaged_in_any_way_is_a_value_or_an_error_and_nothing_else() {
    generate::check(
        6,
        3,
        |g| {
            let mut damaged: Vec<Vec<u8>> = (0..12)
                .map(|_| {
                    let mut b = certificate_file(g.pick(CERTIFICATES), "der");
                    for _ in 0..1 + g.below(3) {
                        match g.below(4) {
                            0 => {
                                let at = g.below(b.len());
                                b[at] = g.below(256) as u8;
                            }
                            1 => b.truncate(g.below(b.len())),
                            2 => {
                                let at = g.below(b.len());
                                b.remove(at);
                            }
                            _ => b.extend(bytes(g, 4)),
                        }
                        if b.is_empty() {
                            b.push(0);
                        }
                    }
                    b
                })
                .collect();
            damaged.extend((0..4).map(|_| bytes(g, 40)));
            damaged
        },
        |inputs| {
            let lines = run(&certificates_program(inputs));
            assert_eq!(lines.len(), inputs.len());
            for line in &lines {
                assert!(line == "err" || line.starts_with("ok "), "{line}");
            }
            // The bytes the same program is given as bytes of any content are read by `value` as the model reads them.
            let model = run(&values_program(inputs));
            let expected: Vec<String> = inputs.iter().map(|b| model_line(b)).collect();
            assert_eq!(model, expected);
        },
    );
}

fn reading_a_certificate_asks_for_memory_that_does_not_depend_on_the_bytes_of_its_names() {
    // The same certificate, with the name of its subject made longer: the names
    // are slices of the bytes, so reading asks for the same memory.
    let longer = |extra: usize| {
        changed("smith_test_leaf", |cert| {
            let subject = &mut parts_of(&mut parts_of(cert)[0])[5];
            let attribute = &mut parts_of(&mut parts_of(subject)[0])[0];
            if let Node::Leaf(_, c) = &mut parts_of(attribute)[1] {
                c.extend(std::iter::repeat_n(b'a', extra));
            }
        })
    };
    let requests = |extra: usize| {
        let bytes = longer(extra);
        let source = format!(
            "use std.console (console, print)\nuse std.x509 (certificate)\n\nfn input() -> Slice[Byte] {{ {} }}\n\nfn main() ! console, alloc {{\n  match certificate(input()) {{\n    .ok(c) {{ print(\"{{c.subject.size}}\") }}\n    .err(e) {{ print(e) }}\n  }}\n}}\n",
            literal(&bytes)
        );
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
        let printed = String::from_utf8(outcome.stdout).unwrap();
        assert!(printed.trim().parse::<usize>().is_ok(), "{printed}");
        (printed, requests)
    };
    let ((short_size, short), (long_size, long)) = (requests(0), requests(4000));
    assert_ne!(short_size, long_size);
    assert_eq!(short, long, "the memory asked for depends on how long the names are");
}

/// The extension of the certificate that the edit puts at the end: the others
/// are taken away first, so that no extension is there twice.
fn with_only_extensions(extensions: Vec<Node>) -> impl Fn(&mut Node) {
    move |cert| {
        let tbs = &mut parts_of(cert)[0];
        let wrapper = parts_of(tbs).last_mut().unwrap();
        *parts_of(&mut parts_of(wrapper)[0]) = extensions.clone();
    }
}

fn extension(oid: &[u8], critical: bool, value: Vec<u8>) -> Node {
    let mut parts = vec![Node::Leaf(6, oid.to_vec())];
    if critical {
        parts.push(Node::Leaf(1, vec![255]));
    }
    parts.push(Node::Leaf(4, value));
    Node::Parts(0x30, parts)
}

fn a_certificate_of_a_version_other_than_3_is_an_error() {
    let version = |number: Option<u8>| {
        changed("smith_test_leaf", |cert| {
            let fields = parts_of(&mut parts_of(cert)[0]);
            match number {
                Some(n) => fields[0] = Node::Parts(0xa0, vec![Node::Leaf(2, vec![n])]),
                // Version 1 is the one that is not written at all.
                None => {
                    fields.remove(0);
                }
            }
        })
    };
    let inputs = vec![
        version(Some(2)),
        version(None),
        version(Some(0)),
        version(Some(1)),
        version(Some(3)),
        version(Some(255)),
    ];
    let lines = run(&certificates_program(&inputs));
    assert!(lines[0].starts_with("ok "), "version 3 is written 2: {}", lines[0]);
    assert!(lines[1..].iter().all(|l| l == "err"), "{lines:?}");
}

fn a_serial_number_is_read_to_20_octets_and_is_an_error_below_one() {
    let serial = |bytes: Vec<u8>| {
        changed("smith_test_leaf", |cert| {
            parts_of(&mut parts_of(cert)[0])[1] = Node::Leaf(2, bytes.clone());
        })
    };
    let inputs = vec![
        serial(vec![1]),
        serial([vec![0x7f], vec![0xab; 19]].concat()),
        // With the zero in front that keeps a number of 20 full octets above zero.
        serial([vec![0, 0x80], vec![0xcd; 19]].concat()),
        serial(vec![0]),
        serial(vec![0xff]),
        serial(vec![0x80, 1, 2]),
    ];
    let lines = run(&certificates_program(&inputs));
    assert_eq!(field(&lines[0], "serial"), "01");
    assert_eq!(field(&lines[1], "serial"), format!("7f{}", "ab".repeat(19)));
    assert_eq!(field(&lines[2], "serial"), format!("80{}", "cd".repeat(19)));
    assert!(lines[3..].iter().all(|l| l == "err"), "{lines:?}");
}

/// The identifier of an extension of `id-ce`, as DER writes it.
const ID_CE: [u8; 2] = [0x55, 29];

fn id_ce(n: u8) -> Vec<u8> {
    [ID_CE.to_vec(), vec![n]].concat()
}

fn an_extension_that_is_written_twice_is_an_error_whichever_it_is() {
    let empty = tlv(0x30, &[]);
    let names = tlv(0x30, &tlv(0x82, b"a.example"));
    let usage = tlv(3, &[7, 0x80]);
    let uses = tlv(0x30, &tlv(6, &[0x2b, 6, 1, 5, 5, 7, 3, 1]));
    let unknown = [0x2b, 0x06, 1, 4, 1, 0x82, 0x37, 2, 4, 3];
    let once = [
        (id_ce(17), names.clone()),
        (id_ce(19), empty.clone()),
        (id_ce(15), usage.clone()),
        (id_ce(37), uses.clone()),
        (unknown.to_vec(), empty.clone()),
        (id_ce(14), tlv(4, &[1])),
    ];
    let mut inputs = vec![changed(
        "smith_test_leaf",
        with_only_extensions(once.iter().map(|(o, v)| extension(o, false, v.clone())).collect()),
    )];
    for (oid, value) in &once {
        let mut all: Vec<Node> = once.iter().map(|(o, v)| extension(o, false, v.clone())).collect();
        all.push(extension(oid, false, value.clone()));
        inputs.push(changed("smith_test_leaf", with_only_extensions(all)));
    }
    let lines = run(&certificates_program(&inputs));
    assert!(lines[0].starts_with("ok "), "{}", lines[0]);
    assert!(lines[1..].iter().all(|l| l == "err"), "{lines:?}");
}

fn an_extension_that_is_read_and_is_not_written_right_is_an_error_and_one_that_is_not_read_is_passed_over() {
    let bad = |oid: Vec<u8>, value: Vec<u8>| changed("smith_test_leaf", with_only_extensions(vec![extension(&oid, false, value)]));
    let negative = tlv(0x30, &[tlv(1, &[255]), tlv(2, &[0xff])].concat());
    let inputs = vec![
        // Names: none, a name that is not a name, and a name of another kind that is passed over.
        bad(id_ce(17), tlv(0x30, &[])),
        bad(id_ce(17), tlv(5, &[])),
        bad(id_ce(17), tlv(0x30, &tlv(0x82, &[0x80, b'a']))),
        // Basic constraints: a length below zero, and a part that is not a boolean or a number.
        bad(id_ce(19), negative),
        bad(id_ce(19), tlv(0x30, &tlv(4, &[]))),
        bad(id_ce(19), tlv(0x30, &tlv(1, &[0]))),
        // Key usage and extended key usage: not a bit string, left over bits that are not zero, no purpose, and a purpose that is not an identifier.
        bad(id_ce(15), tlv(5, &[])),
        bad(id_ce(15), tlv(3, &[1, 0x81])),
        bad(id_ce(37), tlv(0x30, &[])),
        bad(id_ce(37), tlv(0x30, &tlv(2, &[1]))),
        // One that is not read is passed over whatever it holds.
        bad(id_ce(14), tlv(5, &[])),
        bad(id_ce(32), tlv(2, &[1, 2, 3])),
    ];
    let lines = run(&certificates_program(&inputs));
    let (wrong, passed) = lines.split_at(10);
    assert!(wrong.iter().all(|l| l == "err"), "{lines:?}");
    assert!(passed.iter().all(|l| l.starts_with("ok ")), "{lines:?}");
}

/// The numbers of an object identifier of the ASN.1 of Appendix A of the text,
/// found by following the lines `name OBJECT IDENTIFIER ::= { parent n }` back
/// to the line that begins at the top, and written as DER writes them.
fn identifier_of(rfc: &str, name: &str) -> Vec<u8> {
    let appendix = &rfc[rfc.find("Appendix A.  Pseudo-ASN.1").unwrap()..];
    let mut arcs: Vec<u64> = Vec::new();
    let mut at = name.to_string();
    loop {
        let line = appendix
            .lines()
            .find(|l| l.starts_with(&format!("{at} ")) && l.contains("OBJECT IDENTIFIER"))
            .unwrap_or_else(|| panic!("no line of the appendix gives {at}"));
        let inside = line.split_once('{').unwrap().1.split_once('}').unwrap().0;
        let words: Vec<&str> = inside.split_whitespace().collect();
        if words[0].contains('(') {
            // The top: `joint-iso-ccitt(2) ds(5) 29`.
            let numbers = words.iter().map(|w| w.trim_end_matches(')').rsplit(['(']).next().unwrap().parse::<u64>().unwrap());
            arcs.splice(0..0, numbers);
            break;
        }
        arcs.insert(0, words[1].parse().unwrap());
        at = words[0].to_string();
    }
    let mut out = vec![(arcs[0] * 40 + arcs[1]) as u8];
    for arc in &arcs[2..] {
        let mut groups = vec![(arc & 127) as u8];
        let mut rest = arc >> 7;
        while rest > 0 {
            groups.push(128 | (rest & 127) as u8);
            rest >>= 7;
        }
        groups.reverse();
        out.extend(groups);
    }
    out
}

fn the_identifiers_of_the_extensions_that_are_read_are_those_of_appendix_a_of_the_standard() {
    let rfc = crate::standard::read("rfc5280.txt");
    let named = [
        ("id-ce-subjectAltName", true),
        ("id-ce-basicConstraints", true),
        ("id-ce-keyUsage", true),
        ("id-ce-extKeyUsage", true),
        // Others of the appendix, which are not read.
        ("id-ce-subjectKeyIdentifier", false),
        ("id-ce-certificatePolicies", false),
        ("id-ce-nameConstraints", false),
    ];
    // An extension whose value is a null is not written right for any that is read.
    let inputs: Vec<Vec<u8>> = named
        .iter()
        .map(|(name, _)| changed("smith_test_leaf", with_only_extensions(vec![extension(&identifier_of(&rfc, name), false, tlv(5, &[]))])))
        .collect();
    let lines = run(&certificates_program(&inputs));
    for (((name, read), line), input) in named.iter().zip(&lines).zip(&inputs) {
        assert_eq!(line == "err", *read, "{name}: {line} for {input:?}");
    }
    // The first four are those of the text and no others of `id-ce`.
    assert_eq!(identifier_of(&rfc, "id-ce-subjectAltName"), id_ce(17));
    assert_eq!(identifier_of(&rfc, "id-ce-basicConstraints"), id_ce(19));
    assert_eq!(identifier_of(&rfc, "id-ce-keyUsage"), id_ce(15));
    assert_eq!(identifier_of(&rfc, "id-ce-extKeyUsage"), id_ce(37));
}

// The tests of `pem`.

const ALPHABET: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// The base64 of RFC 4648 with padding, written here for the tests.
fn base64(bytes: &[u8]) -> String {
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let n = chunk.iter().enumerate().fold(0u32, |n, (i, b)| n | u32::from(*b) << (16 - 8 * i));
        for i in 0..=chunk.len() {
            out.push(ALPHABET[(n >> (18 - 6 * i) & 63) as usize] as char);
        }
        out.extend(std::iter::repeat_n('=', 3 - chunk.len()));
    }
    out
}

/// Source that prints, for each text, `err` or the number of certificates `pem` gave and each as hex.
fn pem_program(inputs: &[Vec<u8>]) -> String {
    let mut source = String::from(
        "use std.bits (bit_and, shift_right)
use std.console (console, print)
use std.int (wrapping_add)
use std.list (freeze, push)
use std.utf8 (utf8)
use std.x509 (pem)

fn digit(d: Byte) -> Byte {
  if d < 10 { wrapping_add(d, '0') } else { wrapping_add(d, 87) }
}

fn hex(b: List[Byte]) -> str ! alloc {
  var built: List[Byte] = []
  loop i in 0..b.size {
    built = push(built, digit(shift_right(b[i], 4)))
    built = push(built, digit(bit_and(b[i], 15)))
  }
  match utf8(freeze(built)) {
    .ok(t) { t }
    .err(e) { \"\" }
  }
}

fn show(bytes: Slice[Byte]) ! console, alloc {
  match utf8(bytes) {
    .err(e) { print(\"not text\") }
    .ok(t) {
      match pem(t) {
        .err(e) { print(\"err\") }
        .ok(cs) {
          var line = \"ok {cs.size}\"
          loop i in 0..cs.size { line = \"{line} {hex(cs[i])}\" }
          print(line)
        }
      }
    }
  }
}

",
    );
    for (i, input) in inputs.iter().enumerate() {
        source += &format!("fn input{i}() -> Slice[Byte] {{ {} }}\n\n", literal(input));
    }
    source += "fn main() ! console, alloc {\n";
    for i in 0..inputs.len() {
        source += &format!("  show(input{i}())\n");
    }
    source + "}\n"
}

/// What the standard grammar of RFC 7468 (Figure 1) says `pem` gives for a text, written again here from
/// the grammar and not from the library. A line ends at CRLF, CR, or LF. A line that is
/// `-----BEGIN CERTIFICATE-----` and blanks begins a certificate, after which lines that are nothing but blanks
/// may come, and blanks may come before the first line of base64, which is `1*base64char *WSP`: no blank is
/// before or inside it, and a line of nothing may follow the last of them and only the end line after
/// that. `-----END CERTIFICATE-----` and blanks ends it, and what the lines held is one certificate. Lines
/// before, between, and after certificates are not read. The grammar leaves out a label that does not match, a
/// byte that is not base64, and base64 that is not written as RFC 4648 writes it, so those are errors here.
fn model_pem(text: &[u8]) -> Result<Vec<Vec<u8>>, ()> {
    let mut lines: Vec<&[u8]> = Vec::new();
    let mut from = 0;
    let mut i = 0;
    while i < text.len() {
        if text[i] == b'\n' || text[i] == 13 {
            lines.push(&text[from..i]);
            if text[i] == 13 && text.get(i + 1) == Some(&b'\n') {
                i += 1;
            }
            from = i + 1;
        }
        i += 1;
    }
    lines.push(&text[from..]);
    let blank = |c: &u8| *c == b' ' || *c == b'\t';
    let mut found = Vec::new();
    // `None` is outside a certificate, and the rest is the base64 so far and whether a line of nothing ended it.
    let mut inside: Option<(String, bool, bool)> = None;
    for raw in lines {
        let mut end = raw.len();
        while end > 0 && blank(&raw[end - 1]) {
            end -= 1;
        }
        let line = &raw[..end];
        match &mut inside {
            None => {
                if line == b"-----BEGIN CERTIFICATE-----" {
                    inside = Some((String::new(), false, false));
                }
            }
            Some((body, started, nothing)) => {
                if line == b"-----END CERTIFICATE-----" {
                    found.push(model_base64(body)?);
                    inside = None;
                    continue;
                }
                // Before the first base64 line, blanks at its start are not part of it.
                let line = if *started { line } else { &line[line.iter().take_while(|c| blank(c)).count()..] };
                if line.is_empty() {
                    if *started {
                        if *nothing {
                            return Err(());
                        }
                        *nothing = true;
                    }
                } else if *nothing || line.iter().any(|c| *c >= 128) {
                    return Err(());
                } else {
                    *started = true;
                    body.push_str(std::str::from_utf8(line).map_err(|_| ())?);
                }
            }
        }
    }
    if inside.is_some() || found.is_empty() {
        return Err(());
    }
    Ok(found)
}

/// RFC 4648 base64 with padding, which refuses what is not written as `encode` writes it.
fn model_base64(text: &str) -> Result<Vec<u8>, ()> {
    let t = text.as_bytes();
    if !t.len().is_multiple_of(4) {
        return Err(());
    }
    let mut out = Vec::new();
    for (i, group) in t.chunks(4).enumerate() {
        let last = i == t.len() / 4 - 1;
        let padding = group.iter().rev().take_while(|c| **c == b'=').count();
        if padding > 2 || (padding > 0 && !last) {
            return Err(());
        }
        let mut n = 0u32;
        for c in &group[..4 - padding] {
            n = n << 6 | ALPHABET.iter().position(|a| a == c).ok_or(())? as u32;
        }
        n <<= 6 * padding as u32;
        let bytes = [(n >> 16) as u8, (n >> 8) as u8, n as u8];
        if padding > 0 && n & ((1 << (8 * padding)) - 1) != 0 {
            return Err(());
        }
        out.extend(&bytes[..3 - padding]);
    }
    Ok(out)
}

fn pem_line(result: Result<Vec<Vec<u8>>, ()>) -> String {
    match result {
        Err(()) => "err".to_string(),
        Ok(cs) => {
            let mut line = format!("ok {}", cs.len());
            for c in &cs {
                line += &format!(" {}", hex_of(c));
            }
            line
        }
    }
}

/// A certificate as PEM writes it, with a line of `width` characters and the line ending given.
fn block(der: &[u8], width: usize, ending: &str, label: &str) -> String {
    let text = base64(der);
    let mut out = format!("-----BEGIN {label}-----{ending}");
    for line in text.as_bytes().chunks(width) {
        out += &format!("{}{ending}", std::str::from_utf8(line).unwrap());
    }
    out + &format!("-----END {label}-----{ending}")
}

fn pem_reads_a_file_of_several_certificates_to_the_bytes_of_each() {
    let ders: Vec<Vec<u8>> = CERTIFICATES.iter().map(|n| certificate_file(n, "der")).collect();
    let mut inputs = Vec::new();
    for (width, ending) in [(64, "\n"), (64, "\r\n"), (76, "\r"), (1000, "\n")] {
        // Every certificate, with text between them and a block of another kind that is not read.
        let mut text = String::from("Bag attributes\n    friendlyName: test\n");
        for der in &ders {
            text += &block(der, width, ending, "CERTIFICATE");
            text += &format!("some words between{ending}");
        }
        text += &block(b"not a certificate", width, ending, "PRIVATE KEY");
        inputs.push(text.into_bytes());
    }
    // One certificate, with no ending to the last line, and with blanks at the ends of lines.
    let one = block(&ders[0], 64, " \t\n", "CERTIFICATE");
    inputs.push(one.trim_end().as_bytes().to_vec());
    let lines = run(&pem_program(&inputs));
    let expected: Vec<String> = inputs.iter().map(|_| pem_line(Ok(ders.clone()))).collect();
    assert_eq!(lines[..4], expected[..4]);
    assert_eq!(lines[4], pem_line(Ok(vec![ders[0].clone()])));
    // What `pem` gives is what `certificate` reads.
    let reads = run(&certificates_program(&ders));
    assert!(reads.iter().all(|l| l.starts_with("ok ")), "{reads:?}");
}

/// The certificates that the text of RFC 7468 gives as examples, with the lines before the first begin line of
/// each, as the text has them, and the bytes that the base64 of each is.
fn examples_of_the_standard() -> Vec<(String, Vec<u8>)> {
    let rfc = crate::standard::read("rfc7468.txt");
    let lines: Vec<&str> = rfc.lines().collect();
    let mut examples = Vec::new();
    for (at, line) in lines.iter().enumerate() {
        if line.trim_end() != "-----BEGIN CERTIFICATE-----" {
            continue;
        }
        let end = at + lines[at..].iter().position(|l| l.trim_end() == "-----END CERTIFICATE-----").unwrap();
        // The explanatory text of Figure 7 is the three lines before the begin line.
        let before = if lines[at - 1].starts_with("Validity:") { &lines[at - 3..at] } else { &lines[at..at] };
        let body: String = lines[at + 1..end].concat();
        let mut text = before.join("\n");
        if !before.is_empty() {
            text += "\n";
        }
        text += &lines[at..=end].join("\n");
        examples.push((text + "\n", model_base64(&body).unwrap()));
    }
    examples
}

fn the_certificates_that_rfc_7468_gives_as_examples_are_read_to_their_bytes() {
    let examples = examples_of_the_standard();
    assert_eq!(examples.len(), 2, "the figures of sections 5.1 and 5.2");
    let inputs: Vec<Vec<u8>> = examples.iter().map(|e| e.0.clone().into_bytes()).collect();
    let lines = run(&pem_program(&inputs));
    for ((_, der), line) in examples.iter().zip(&lines) {
        assert_eq!(line, &pem_line(Ok(vec![der.clone()])));
        // A certificate of the text of the standard, which `std.x509` reads as it reads any.
        assert!(der[0] == 0x30, "a sequence");
    }
}

fn a_file_that_is_not_pem_of_certificates_is_an_error_and_pem_reads_what_the_model_reads() {
    generate::check(
        7,
        4,
        |g| {
            let ders: Vec<Vec<u8>> = (0..3).map(|_| bytes(g, 150)).collect();
            (0..16)
                .map(|case| {
                    let ending = *g.pick(&["\n", "\r\n", "\r"]);
                    let mut text = String::new();
                    for _ in 0..g.below(4) {
                        let der = &ders[g.below(3)];
                        let mut b = block(der, 1 + g.below(80), ending, "CERTIFICATE");
                        // What the grammar allows that a text seldom has, in some of them.
                        if g.below(3) == 0 {
                            b = b.replacen(ending, &format!(" \t{ending}{ending}  "), 1);
                        }
                        if g.below(3) == 0 {
                            b = b.replace(&format!("{ending}-----END"), &format!("{ending}{ending}-----END"));
                        }
                        // A fault of one kind in some of them, chosen by the case.
                        if g.below(3) == 0 {
                            match (case + g.below(6)) % 7 {
                                5 => b = b.replacen(ending, &format!("{ending} "), 2),
                                6 => b = b.replace(&format!("{ending}-----END"), &format!("{ending}{ending}{ending}-----END")),
                                0 => b = b.replace("-----END", "-----EN"),
                                1 => b = b.replacen(ending, &format!("{ending}-----BEGIN CERTIFICATE-----{ending}"), 2),
                                2 => b = b.replacen("A", "!", 1),
                                3 => b = b.replacen(ending, &format!("é{ending}"), 2),
                                _ => b = b.replacen("=", "==", 1),
                            }
                        }
                        text += &b;
                        text += &format!("text{ending}");
                    }
                    text.into_bytes()
                })
                .collect::<Vec<_>>()
        },
        |inputs| {
            // Texts of a kind that a draw may not make: a certificate that is begun and not ended, after one that is, and texts with none.
            let good = block(&[1, 2, 3], 64, "\n", "CERTIFICATE");
            let mut inputs = inputs.clone();
            inputs.push(format!("{good}-----BEGIN CERTIFICATE-----\nAAAA\n").into_bytes());
            inputs.push(format!("{good}-----BEGIN CERTIFICATE-----\n").into_bytes());
            inputs.push(Vec::new());
            inputs.push(b"no certificate here\n".to_vec());
            inputs.push(block(&[1, 2, 3], 64, "\n", "PRIVATE KEY").into_bytes());
            // Where lines of nothing and blanks may be and where they may not: before the base64 and after it, but not
            // between its lines, and blanks at the start of the first line and not at the start of another.
            for text in [
                "-----BEGIN CERTIFICATE-----\nAAAA\n\nAAAA\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\nAAAA\nAAAA\n\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\nAAAA\nAAAA\n\n\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\n\n  \n  AAAA\nAAAA\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\nAAAA\n AAAA\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\nAA AA\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE----- \t\r\nAAAA\r-----END CERTIFICATE----- \t",
                "-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n",
                "-----BEGIN CERTIFICATE-----\nAAAA\n-----END X509 CERTIFICATE-----\n",
            ] {
                inputs.push(text.as_bytes().to_vec());
            }
            let inputs = &inputs;
            let lines = run(&pem_program(inputs));
            let expected: Vec<String> = inputs.iter().map(|b| pem_line(model_pem(b))).collect();
            assert_eq!(lines, expected);
            assert!(expected.iter().any(|l| l.starts_with("ok")) && expected.iter().any(|l| l == "err"));
        },
    );
}
