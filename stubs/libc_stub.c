/* Solo proveen los NOMBRES para que dlopen() resuelva; las funciones reales se enganchan en hook.c */
int __errno(void) { return 0; }
unsigned long __stack_chk_guard = 0;
int __aeabi_atexit(void) { return 0; }
void __cxa_begin_cleanup(void) {}
void __cxa_call_unexpected(void) {}
void __cxa_type_match(void) {}
void *__gnu_Unwind_Find_exidx(void) { return 0; }
