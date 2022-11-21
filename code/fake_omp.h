https://powcoder.com
代写代考加微信 powcoder
Assignment Project Exam Help
Add WeChat powcoder
/* Versions of OMP functions that can be used in sequential version */
#ifndef FAKE_OMP_H
int omp_get_max_threads();
int omp_get_num_threads();
int omp_get_thread_num();
void omp_set_num_threads();
#define FAKE_OMP_H 1
#endif
