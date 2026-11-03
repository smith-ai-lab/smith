// C++ whose runtime entry points the pass has no table row for: new, delete and throw.
int *make(int x) {
    return new int(x);
}

void drop(int *p) {
    delete p;
}

int give_up(int x) {
    if (x < 0) throw x;
    return x;
}

int plain(int x) {
    return x * 2;
}
