// Entry for `pio run -e native` only. Host unit tests (`pio test`) define
// PIO_UNIT_TESTING and supply their own main() via Unity suites.
#ifndef PIO_UNIT_TESTING
int main(int, char**) {
    return 0;
}
#endif
