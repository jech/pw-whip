#if defined (__cplusplus)
extern "C" {
#endif

typedef void (*sendfunction)(const unsigned char*, size_t, uint32_t, void*);
void *pw_setup(const char *connect_target, int bitrate);
int pw_run(void *closure, sendfunction send, void *send_closure);
void pw_cleanup(void *closure);
#if defined (__cplusplus)
}
#endif
