# The guard of the test split (#3283): every test of the suite is run or served by exactly one part.
#
# Input, one line each, tab separated: `suite<TAB>part<TAB>name` for each test of the suite that part `part` was dealt
# from (every part lists the whole suite, which each computed by itself), and `part<TAB>part<TAB>name` for each test
# `part` ran or took from the store. Run with `-v parts=8` (how many parts the run has). It prints what is wrong and
# exits 1, or prints one line and exits 0. A split that disagrees between the parts, loses a test or runs it twice is
# a red run, never a test that nobody ran.
BEGIN { FS = "\t"; bad = 0 }
$1 == "suite" { listed[$3]++; sizes[$2]++ }
$1 == "part"  { dealt[$3]++; where[$3] = where[$3] " " $2; mine[$2]++; total++ }
function complain(text) {
    bad++
    if (bad <= 20) print "split guard: " text
}
END {
    suite = 0
    for (name in listed) {
        suite++
        if (listed[name] != parts) complain(name " is in the suite of " listed[name] " of " parts " parts")
        if (!(name in dealt)) complain(name " was dealt to no part")
        else if (dealt[name] > 1) complain(name " was dealt to " dealt[name] " parts (" substr(where[name], 2) ")")
    }
    for (name in dealt)
        if (!(name in listed)) complain(name " is in a part and not in the suite")
    for (part in sizes)
        seen++
    if (seen != parts) complain(seen + 0 " parts listed the suite, the run has " parts)
    if (bad > 20) print "split guard: ... and " (bad - 20) " more"
    if (suite == 0) complain("the suite is empty")
    if (bad > 0) exit 1
    print "split guard: " suite " tests, each in exactly one of " parts " parts (" total " dealt)"
}
