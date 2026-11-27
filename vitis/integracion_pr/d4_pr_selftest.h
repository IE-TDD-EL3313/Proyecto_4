#ifndef D4_PR_SELFTEST_H
#define D4_PR_SELFTEST_H

/* Devuelve 0 si los ensayos pasan; otro valor si falla alguno.
 * Se ejecuta una sola vez, ANTES de habilitar el AXI Timer. */
int d4_pr_selftest_run(void);

#endif
