int cbox_shared_value(void);

int main(void) {
    return cbox_shared_value() == 42 ? 0 : 1;
}
