// gzip files for `smith_package`: the members of a .gz one after the other, pushed to a sink. The DEFLATE, the gzip
// header and trailer and the CRC-32 are smith::inflate's (lib/smith/inflate.smith, which smith_package includes in its
// lowering before this file); this is the loop over members. Included by runtime/package.c after its SmithPackageFault
// and smith_package_fail; see runtime/untar.c for what uses it.
//
// The decoder reads an archive that is already in memory (a mapped file) and pushes what it produces to a sink in
// pieces of up to 32 KiB, the window. It allocates nothing: the window is one static buffer.
#include <stdint.h>
#include <string.h>

// What a decoder pushes its output to: false stops the decoder, and the sink has already set the error.
typedef bool (*SmithArchiveSink)(void *context, const uint8_t *data, size_t size);

constexpr size_t smith_gzip_members_max = 1000000;

// Decodes a gzip file, which is one or more members one after the other (as `cat a.gz b.gz` makes), to the sink.
// Zero bytes after the last member are padding and ignored; anything else is an error.
static bool smith_gzip_decode(const uint8_t *in, size_t size, SmithArchiveSink sink, void *context,
                              SmithPackageFault *error) {
    static uint8_t window[smith_inflate_window];
    size_t at = 0;
    for (size_t member = 0; member < smith_gzip_members_max; member++) {
        size_t used = 0;
        uint64_t produced = 0;
        SmithInflateStatus status =
            smith_inflate_stream(SmithInflateFormat_Gzip, in + at, size - at, window, sink, context, &used, &produced);
        if (status == SmithInflateStatus_Stopped) return false; // the sink has said why
        if (status != SmithInflateStatus_Ok)
            return smith_package_fail(error, "gzip: %s", smith_inflate_status_message(status));
        at += used;
        while (at < size && in[at] == 0)
            at++;
        if (at == size) return true;
    }
    return smith_package_fail(error, "gzip: too many members");
}
