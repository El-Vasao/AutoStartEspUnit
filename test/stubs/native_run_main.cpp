// Точка входа только для `pio run -e native`. Нативные тесты (`pio test`) задают
// PIO_UNIT_TESTING и свой main() через наборы Unity.
#ifndef PIO_UNIT_TESTING
int main(int, char**) {
    return 0;
}
#endif
