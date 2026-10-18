#ifndef LOG_H
#define LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void log_init(const char *path);
void log_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* LOG_H */
