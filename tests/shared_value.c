#ifdef _WIN32
__declspec(dllexport)
#endif
int cbox_shared_value(void) {
    return 42;
}
