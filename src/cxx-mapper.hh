
extern int mapper_enabled (void);
extern int mapper_setup (const char *option);
extern void mapper_clear (void);
extern int mapper_pre_pselect (int, fd_set *);
extern int mapper_post_pselect (int, fd_set *);
extern pid_t mapper_wait (int *);
extern char *mapper_ident (void *);

