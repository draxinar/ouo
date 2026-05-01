#ifndef DAT_H_
#define DAT_H_

#define USED(x)  ((void)(x))
#define nelem(x) (sizeof(x) / sizeof((x)[0]))

extern char *GLOBAL_file_map0_mul;
extern char *GLOBAL_file_staidx0_mul;
extern char *GLOBAL_file_staidx0_bkp;
extern char *GLOBAL_file_statics0_mul;
extern char *GLOBAL_file_statics0_bkp;
extern char *GLOBAL_file_dynidx0_mul;
extern char *GLOBAL_file_dynidx0_bkp;
extern char *GLOBAL_file_dynamic0_mul;
extern char *GLOBAL_file_dynamic0_bkp;
extern char *GLOBAL_file_tempidx_mul;
extern char *GLOBAL_file_temp_mul;
extern char *GLOBAL_file_regions_txt;
extern int g_TerminateServerFlag;

#endif /* DAT_H_ */
