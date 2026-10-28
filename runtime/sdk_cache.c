// The SDK path of `xcrun --show-sdk-path`, remembered (issue #639): the probe is a process of 5-11 ms and 35 million
// instructions, as much as the whole start of smith, and it ran at every `smith cc` whose caller had no SDKROOT. The
// answer is kept in <data>/smith/sdk, three lines: the key, the SDK path and the identity of that folder. It is used
// only for the real /usr/bin/xcrun (a stub on PATH is asked), only while the key (DEVELOPER_DIR and where xcode-select
// points) is the same and the folder is the same one (device, inode and modification time), so a new Xcode, a switch
// with xcode-select or a different SDK behind the same name is asked again. Any failure to read or write it asks xcrun
// as before. Included by smith.c, on macOS.
constexpr char smith_sdk_cache_xcrun[] = "/usr/bin/xcrun";
constexpr char smith_sdk_cache_select[] = "/var/db/xcode_select_link";
constexpr size_t smith_sdk_cache_max = 4 * (size_t)PATH_MAX;

// <data>/smith/sdk; false when there is no folder to keep it in.
static bool smith_sdk_cache_path(char *path) {
    const char *data = getenv("XDG_DATA_HOME");
    const char *home = getenv("HOME");
    if (data != nullptr && data[0] == '/') {
        snprintf(path, PATH_MAX, "%s/smith", data);
    } else if (home != nullptr && home[0] == '/') {
        snprintf(path, PATH_MAX, "%s/.local/share/smith", home);
    } else {
        return false;
    }
    if (strlen(path) + sizeof "/sdk" >= PATH_MAX) return false;
    strcat(path, "/sdk");
    return true;
}

// What the SDK answer depends on besides the folder itself: DEVELOPER_DIR and the target of xcode-select's link.
static void smith_sdk_cache_key(char *key, size_t size) {
    const char *developer = getenv("DEVELOPER_DIR");
    char selected[PATH_MAX] = "";
    ssize_t got = readlink(smith_sdk_cache_select, selected, sizeof selected - 1);
    if (got > 0) selected[got] = '\0';
    snprintf(key, size, "%s|%s", developer != nullptr ? developer : "", selected);
}

// The identity of the SDK folder (symbolic links followed): device, inode and modification time.
static bool smith_sdk_cache_identity(const char *sdk, char *identity, size_t size) {
    struct stat info = {};
    if (stat(sdk, &info) != 0 || !S_ISDIR(info.st_mode)) return false;
    snprintf(identity, size, "%llu %llu %lld.%ld", (unsigned long long)info.st_dev, (unsigned long long)info.st_ino,
             (long long)info.st_mtimespec.tv_sec, info.st_mtimespec.tv_nsec);
    return true;
}

// Reads the file into three lines; false when it is missing or not three lines.
static bool smith_sdk_cache_read(const char *path, char *text, char *lines[3]) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    ssize_t got = read(fd, text, smith_sdk_cache_max - 1);
    close(fd);
    if (got <= 0) return false;
    text[got] = '\0';
    char *cursor = text;
    for (size_t i = 0; i < 3; i++) { // three lines, each ends in a newline
        char *end = strchr(cursor, '\n');
        if (end == nullptr) return false;
        *end = '\0';
        lines[i] = cursor;
        cursor = end + 1;
    }
    return true;
}

// The remembered SDK path of the real xcrun, when its key and the folder still match.
static bool smith_sdk_cache_get(const char *xcrun, char *out) {
    char path[PATH_MAX];
    if (strcmp(xcrun, smith_sdk_cache_xcrun) != 0 || !smith_sdk_cache_path(path)) return false;
    char text[smith_sdk_cache_max];
    char *lines[3] = {};
    if (!smith_sdk_cache_read(path, text, lines)) return false;
    char key[PATH_MAX * 2];
    char identity[128];
    smith_sdk_cache_key(key, sizeof key);
    if (strcmp(lines[0], key) != 0 || strlen(lines[1]) >= PATH_MAX || lines[1][0] != '/') return false;
    if (!smith_sdk_cache_identity(lines[1], identity, sizeof identity) || strcmp(lines[2], identity) != 0) return false;
    snprintf(out, PATH_MAX, "%s", lines[1]);
    return true;
}

// Remembers the answer of the real xcrun, written beside its place and renamed into it so that a reader sees a whole
// file. Silent when it cannot: the next start asks again.
static void smith_sdk_cache_put(const char *xcrun, const char *sdk) {
    char path[PATH_MAX];
    char identity[128];
    if (strcmp(xcrun, smith_sdk_cache_xcrun) != 0 || !smith_sdk_cache_path(path)) return;
    if (!smith_sdk_cache_identity(sdk, identity, sizeof identity)) return;
    char folder[PATH_MAX];
    snprintf(folder, sizeof folder, "%s", path);
    *strrchr(folder, '/') = '\0';
    (void)mkdir(folder, 0755);
    char temporary[PATH_MAX + 32];
    snprintf(temporary, sizeof temporary, "%s.%d", path, (int)getpid());
    char key[PATH_MAX * 2];
    smith_sdk_cache_key(key, sizeof key);
    char text[smith_sdk_cache_max];
    int length = snprintf(text, sizeof text, "%s\n%s\n%s\n", key, sdk, identity);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    bool ok = length > 0 && (size_t)length < sizeof text && write(fd, text, (size_t)length) == length;
    close(fd);
    if (!ok || rename(temporary, path) != 0) (void)unlink(temporary);
}
